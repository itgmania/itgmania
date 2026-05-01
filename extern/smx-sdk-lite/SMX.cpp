// SMX SDK Lite — consolidated implementation.
// This file contains the core implementation of the StepManiaX SDK lite, consolidated
// into a single file for easier compilation and distribution. It includes:
// - Helper utility functions (logging, time, formatting, binary conversion)
// - SMXDevice: high-level per-controller logic and state management
// - SMXManager: device enumeration, background I/O thread, and orchestration
// - Public C API: initialization, shutdown, and device queries

#include "SMX.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <hidapi/hidapi.h>

#include "SMXConfigPacket.h"
#include "SMXDeviceConnection.h"

using namespace std;

#define SMX_VERSION "0.1.0"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// This section contains utility functions used throughout the SDK for logging,
// timing, string formatting, and binary data conversion.

namespace SMX {

static function<void(const string &log)> g_LogCallback;

/// Returns the elapsed time in seconds since program start using a high-resolution
/// monotonic clock. Used for timing commands and logging timestamps.
/// @return Elapsed time in seconds as a double.
double GetMonotonicTime()
{
    static auto start = chrono::steady_clock::now();
    return chrono::duration<double>(chrono::steady_clock::now() - start).count();
}

/// Logs a message with a timestamp prefix. If a custom log callback is set,
/// it will be used; otherwise, logs to stdout with the current monotonic time.
/// @param s The message to log.
void Log(const string &s)
{
    if(g_LogCallback)
        g_LogCallback(s);
    else
        printf("%6.3f: %s\n", GetMonotonicTime(), s.c_str());
}

/// Sets a custom callback function to handle all log messages from the SDK.
/// This allows applications to redirect logging to files, custom streams, etc.
/// If not set, logs will be printed to stdout.
/// @param callback Function that receives log messages as strings.
void SetLogCallback(function<void(const string &log)> callback)
{
    g_LogCallback = std::move(callback);
}

/// Formatted string printing using printf-style arguments. Returns a std::string
/// instead of printing directly, useful for building log messages and debug output.
/// @param fmt Printf-style format string.
/// @param ... Variable arguments to format.
/// @return The formatted string.
string ssprintf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    const int n = vsnprintf(nullptr, 0, fmt, va);
    va_end(va);
    if(n < 0) return string("Error formatting: ") + fmt;

    string s(n, '\0');
    va_start(va, fmt);
    vsnprintf(&s[0], n + 1, fmt, va);
    va_end(va);
    return s;
}

/// Converts binary data to a hexadecimal string representation.
/// Each byte is converted to two hex digits (lowercase).
/// @param pData Pointer to the binary data.
/// @param iNumBytes Number of bytes to convert.
/// @return Hexadecimal string representation of the binary data.
string BinaryToHex(const void *pData, const int iNumBytes)
{
    const auto *p = static_cast<const unsigned char*>(pData);
    string s;
    for(int i = 0; i < iNumBytes; i++)
        s += ssprintf("%02x", p[i]);
    return s;
}

string BinaryToHex(const string &sString)
{
    return BinaryToHex(sString.data(), static_cast<int>(sString.size()));
}

/// Generates a random serial number (16 random bytes).
/// Used to assign unique identifiers to devices that don't have a serial number.
/// @param pOut Pointer to a 16-byte buffer to receive the generated serial.
static void GenerateSerial(uint8_t *pOut)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(0, 255);
    for(int i = 0; i < SERIAL_SIZE; i++)
        pOut[i] = static_cast<uint8_t>(dist(gen));
}

} // namespace SMX

// ---------------------------------------------------------------------------
// SMXDevice — high-level per-controller logic
// ---------------------------------------------------------------------------
// Represents a single StepManiaX controller and manages its connection state,
// configuration, and communication. Each instance handles one physical device
// and maintains synchronized state that can be queried by the application.
//
// This class is non-copyable but movable (to support storage in arrays and
// transfer between threads). Access to mutable state is protected by a shared
// mutex to ensure thread-safe queries and updates from the I/O thread.

namespace {

using namespace SMX;

class SMXDevice
{
public:
    SMXDevice() = default;
    ~SMXDevice() = default;

    // Non-copyable (prevents accidental duplication of device handles)
    SMXDevice(const SMXDevice &) = delete;
    SMXDevice &operator=(const SMXDevice &) = delete;

    // Movable (required for storage in arrays and manager reordering)
    SMXDevice(SMXDevice &&other) noexcept:
        m_pLock(other.m_pLock),
        m_pUpdateCallback(std::move(other.m_pUpdateCallback)),
        m_Connection(std::move(other.m_Connection)),
        m_Config(other.m_Config),
        m_bHaveConfig(other.m_bHaveConfig)
    {
        other.m_bHaveConfig = false;
    }

    SMXDevice &operator=(SMXDevice &&other) noexcept
    {
        if(this != &other)
        {
            m_pLock = other.m_pLock;
            m_pUpdateCallback = std::move(other.m_pUpdateCallback);
            m_Connection = std::move(other.m_Connection);
            m_Config = other.m_Config;
            m_bHaveConfig = other.m_bHaveConfig;
            other.m_bHaveConfig = false;
        }
        return *this;
    }

    /// Sets the recursive mutex used for synchronizing access to this device's state.
    /// Called during initialization to point to the manager's lock.
    /// @param pLock Pointer to the recursive mutex.
    void SetLock(recursive_mutex *pLock) { m_pLock = pLock; }

    /// Sets the callback function to be invoked when this device's state changes
    /// (e.g., connection, disconnection, input state updates).
    /// @param cb Callback function with signature (int pad, SMXUpdateCallbackReason reason).
    void SetUpdateCallback(function<void(int, SMXUpdateCallbackReason)> cb) { m_pUpdateCallback = std::move(cb); }

    SMXDeviceConnection *GetConnection() { return &m_Connection; }

    /// Returns the HID path of this device.
    string GetDevicePath() const { return m_Connection.GetPath(); }

    bool IsConnected() const
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        return IsConnectedLocked();
    }

    bool OpenDevice(const string &sPath, string &sError) { return m_Connection.Open(sPath, sError); }

    void SetConnectionCallbacks()
    {
        m_Connection.SetInputStateChangedCallback([this]() {
            CallUpdateCallback(static_cast<SMXUpdateCallbackReason>(SMXUpdateCallback_Updated | SMXUpdateCallback_InputState));
        });
    }

    void CloseDevice()
    {
        m_Connection.Close();
        m_bHaveConfig = false;
        CallUpdateCallback(static_cast<SMXUpdateCallbackReason>(SMXUpdateCallback_Updated | SMXUpdateCallback_Disconnected));
    }

    bool PollUSBData(string &sError)
    {
        if(!m_Connection.IsConnected())
            return false;
        return m_Connection.PollUSBData(sError);
    }

    /// Queues a command to be sent to this device asynchronously.
    /// The command is sent in the background I/O thread.
    /// @param cmd The command string to send.
    /// @param pComplete Optional callback invoked when the command response is received.
    void SendCommand(const string &cmd, const function<void(string)>& pComplete = nullptr)
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        if(!m_Connection.IsConnected())
        {
            if(pComplete)
                pComplete("");
            return;
        }
        m_Connection.SendCommand(cmd, pComplete);
    }

    /// Retrieves the current device information (connection status, player number, serial).
    /// This is thread-safe; it acquires the lock and calls GetInfoLocked.
    /// @param info [out] SMXInfo structure to be filled with device information.
    void GetInfo(SMXInfo &info) const
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        GetInfoLocked(info);
    }

    /// Internal version of GetInfo that assumes the lock is already held.
    /// Populates the SMXInfo structure with current device state.
    /// @param info [out] SMXInfo structure to be filled.
    void GetInfoLocked(SMXInfo &info) const
    {
        info = SMXInfo();
        info.m_bConnected = IsConnectedLocked();
        if(!info.m_bConnected)
            return;

        const SMXDeviceInfo di = m_Connection.GetDeviceInfo();
        info.m_bIsPlayer2 = di.m_bP2;
        memcpy(info.m_Serial, di.m_Serial, sizeof(info.m_Serial));
        info.m_iFirmwareVersion = di.m_iFirmwareVersion;

        // Check if a serial number has been assigned. An unassigned serial
        // will be all zeros or all 0xFF in the raw bytes, which shows up as
        // "00000000000000000000000000000000" or "ffffffffffffffffffffffffffffffff".
        info.m_bHasSerialNumber = false;
        for(int i = 0; i < 32; i++)
        {
            if(info.m_Serial[i] != '0' && info.m_Serial[i] != 'f')
            {
                info.m_bHasSerialNumber = true;
                break;
            }
        }
    }

    /// Returns whether the device's physical jumper is set to Player 2 mode.
    /// Lock must be held by the caller.
    /// Only meaningful if the device is fully connected.
    /// Used internally for device ordering logic.
    /// @return True if this device's P2 jumper is set.
    bool IsPlayer2Locked() const
    {
        return IsConnectedLocked() && m_Connection.GetDeviceInfo().m_bP2;
    }

    /// Returns the current input state (pressed panels) for this device.
    /// The input state is a 16-bit mask where each bit represents a panel.
    /// @return The input state bitmask.
    uint16_t GetInputState() const
    {
        // No lock needed: m_iInputState is std::atomic<uint16_t>.
        return m_Connection.GetInputState();
    }

    /// Updates the device state, called from the I/O thread each frame.
    /// Checks for input changes, processes received packets, and manages the
    /// connection lifecycle. Called with the manager's lock already held.
    /// @param sError [out] Error message if an update fails.
    void Update(string &sError)
    {
        if(!m_Connection.IsConnected())
            return;

        CheckActive();

        m_Connection.Update(sError);
        if(!sError.empty())
            return;

        HandlePackets();
    }

private:
    /// Checks if the device is fully connected (has valid connection, device info, and config).
    /// @return True if all required state has been initialized.
    bool IsConnectedLocked() const
    {
        return m_Connection.IsConnectedWithDeviceInfo() && m_bHaveConfig;
    }

    /// Activates the device after initial connection to begin receiving input updates.
    /// Sends an "activate" command to the device (format depends on firmware version).
    void CheckActive()
    {
        if(!m_Connection.IsConnectedWithDeviceInfo() || m_Connection.GetActive())
            return;

        m_Connection.SetActive(true);
        const SMXDeviceInfo di = m_Connection.GetDeviceInfo();
        m_Connection.SendCommand(di.m_iFirmwareVersion >= 5 ? "G" : "g\n");
    }

    /// Processes received packets from the device, extracting configuration data.
    /// Reads packets from the connection and updates the cached config when a
    /// complete config packet is received. Invokes the update callback on config change.
    void HandlePackets()
    {
        string buf;
        while(m_Connection.ReadPacket(buf))
        {
            if(buf.empty())
                continue;

            // We currently only handle g/G packets.
            if(buf[0] != 'g' && buf[0] != 'G')
                continue;

            if(buf.size() < 2)
            {
                Log("Invalid config packet");
                continue;
            }
            const auto iSize = static_cast<uint8_t>(buf[1]);
            if(static_cast<int>(buf.size()) < iSize + 2)
            {
                Log("Invalid config packet size");
                continue;
            }

            if(buf[0] == 'g')
            {
                vector<uint8_t> raw(buf.begin() + 2, buf.begin() + 2 + iSize);
                ConvertToNewConfig(raw, m_Config);
            }
            else
            {
                memcpy(&m_Config, buf.data() + 2, min(static_cast<int>(iSize), static_cast<int>(sizeof(m_Config))));
            }

            const bool bFirstConfig = !m_bHaveConfig;
            m_bHaveConfig = true;
            SMXUpdateCallbackReason reason = static_cast<SMXUpdateCallbackReason>(
                SMXUpdateCallback_Updated | SMXUpdateCallback_ConfigUpdated |
                (bFirstConfig ? SMXUpdateCallback_Connected : 0));
            CallUpdateCallback(reason);
        }
    }

    /// Invokes the update callback if one is set, passing device index and reason.
    /// @param reason The reason for the callback (e.g., SMXUpdateCallback_Updated).
    void CallUpdateCallback(SMXUpdateCallbackReason const reason) const
    {
        if(!m_pUpdateCallback)
            return;
        const SMXDeviceInfo di = m_Connection.GetDeviceInfo();
        m_pUpdateCallback(di.m_bP2 ? 1 : 0, reason);
    }

    recursive_mutex *m_pLock = nullptr;
    function<void(int, SMXUpdateCallbackReason)> m_pUpdateCallback;
    SMXDeviceConnection m_Connection;
    SMXConfig m_Config;
    bool m_bHaveConfig = false;
};

// ---------------------------------------------------------------------------
// SMXManager — device search, I/O thread, orchestration
// ---------------------------------------------------------------------------
// Manages the lifecycle of all connected StepManiaX devices. This class is
// responsible for:
// - Enumerating and discovering SMX devices via HID
// - Running a USB polling thread for low-latency input state updates
// - Running a main I/O thread for device connections, commands, and config
// - Ensuring proper device ordering (Player 1 and Player 2)
// - Notifying the application of device state changes via callbacks

class SMXManager
{
public:
    /// Constructor initializes the manager and starts the background I/O thread.
    /// @param callback Function to be invoked when device state changes.
    explicit SMXManager(const function<void(int, SMXUpdateCallbackReason)>& callback):
        m_Callback(callback)
    {
        for(auto & m_Device : m_Devices)
        {
            m_Device.SetLock(&m_Lock);
            m_Device.SetUpdateCallback(callback);
            m_Device.SetConnectionCallbacks();
        }
        m_Thread = thread([this] { ThreadMain(); });
        m_USBPollingThread = thread([this] { USBPollingThreadMain(); });
    }

    /// Destructor signals the I/O thread to stop and waits for it to finish.
    ~SMXManager()
    {
        m_bShutdown = true;
        m_Cond.notify_all();
        if(m_Thread.joinable())
        {
            m_Thread.join();
        }
        if(m_USBPollingThread.joinable())
        {
            m_USBPollingThread.join();
        }
    }

    /// Retrieves a pointer to a device by pad index (0 or 1).
    /// @param pad Device index (0 for Player 1, 1 for Player 2).
    /// @return Pointer to the SMXDevice, or nullptr if pad is invalid.
    SMXDevice *GetDevice(const int pad)
    {
        if(pad < 0 || pad > 1)
            return nullptr;
        return &m_Devices[pad];
    }

    /// Generates and assigns random serial numbers to all connected devices
    /// that don't already have one. Called via the public API function SMX_SetSerialNumbers.
    void SetSerialNumbers()
    {
        lock_guard<recursive_mutex> lock(m_Lock);
        for(auto & m_Device : m_Devices)
        {
            string sData = "s";
            uint8_t serial[SERIAL_SIZE];
            GenerateSerial(serial);
            sData.append(reinterpret_cast<char*>(serial), sizeof(serial));
            sData.append(1, '\n');
            m_Device.SendCommand(sData);
        }
    }

    void SetPollingRate(int iMainThreadMs, int iUSBPollingUs)
    {
        m_iMainThreadSleepMs.store(iMainThreadMs);
        m_iUSBPollingSleepUs.store(iUSBPollingUs);
    }

private:
    /// Main loop for the USB polling thread. Runs continuously, checking both devices
    /// for available USB data and signaling the main I/O thread when data is found.
    ///
    /// The USB polling thread:
    /// - Continuously reads from both HID devices (non-blocking)
    /// - Parses Report 3 (input state) packets inline and updates m_iInputState atomically
    /// - Buffers Report 6 (command/config) packets for main thread processing
    /// - Signals the main thread if Report 6 packets are found
    /// - Sleep interval is configurable via SMX_SetPollingRate (default: 1000us)
    void USBPollingThreadMain()
    {
        while(!m_bShutdown)
        {
            bool bHasReport6Data = false;

            {
                lock_guard<recursive_mutex> lock(m_Lock);

                // Check both devices for available USB data
                for(int i = 0; i < 2; i++)
                {
                    string sError;
                    if(m_Devices[i].PollUSBData(sError))
                        bHasReport6Data = true;

                    if(!sError.empty())
                        Log(ssprintf("USB polling error on device %i: %s", i, sError.c_str()));
                }
            }

            // Signal the main thread if we found any Report 6 data to process
            if(bHasReport6Data)
            {
                m_Cond.notify_all();
            }

            // Configurable sleep between poll cycles
            this_thread::sleep_for(chrono::microseconds(m_iUSBPollingSleepUs.load(memory_order_relaxed)));
        }
    }
    /// Main loop for the background I/O thread. Runs continuously until shutdown.
    /// Each iteration:
    /// - Attempts to connect to any newly discovered devices
    /// - Updates each connected device's state
    /// - Ensures devices are in the correct order (Player 1, then Player 2)
    /// - Waits for Report 6 data or timeout before the next iteration
    void ThreadMain()
    {
        m_Lock.lock();
        while(!m_bShutdown)
        {
            AttemptConnections();

            for(int i = 0; i < 2; i++)
            {
                string sError;
                m_Devices[i].Update(sError);
                if(!sError.empty())
                {
                    Log(ssprintf("Device %i error: %s", i, sError.c_str()));
                    m_Devices[i].CloseDevice();
                }
            }

            CorrectDeviceOrder();

            // Wait for Report 6 data from USB polling thread, or timeout.
            m_Cond.wait_for(m_Lock, chrono::milliseconds(m_iMainThreadSleepMs.load(memory_order_relaxed)));
        }
        m_Lock.unlock();
    }

    /// Enumerates all HID devices matching the SMX vendor ID and product ID.
    /// For each discovered device not already connected, attempts to open it
    /// in an available slot. Called each I/O thread iteration.
    void AttemptConnections()
    {
        // Skip enumeration if both device slots are already occupied.
        if(!m_Devices[0].GetDevicePath().empty() && !m_Devices[1].GetDevicePath().empty())
            return;

        // Enumerate SMX devices via hidapi.
        hid_device_info *devs = hid_enumerate(0x2341, 0x8037);
        for(const hid_device_info *cur = devs; cur; cur = cur->next)
        {
            if(!cur->product_string || wcscmp(cur->product_string, L"StepManiaX") != 0)
                continue;
            if(!cur->path)
                continue;

            string sPath = cur->path;

            // Skip if already open.
            bool bOpen = false;
            for(const auto & m_Device : m_Devices)
                if(m_Device.GetDevicePath() == sPath) { bOpen = true; break; }
            if(bOpen) continue;

            // Find an empty slot.
            SMXDevice *pSlot = nullptr;
            for(auto & m_Device : m_Devices)
                if(m_Device.GetDevicePath().empty()) { pSlot = &m_Device; break; }

            if(!pSlot) { Log("No available slots for device."); break; }

            Log("Opening SMX device: " + sPath);
            string sError;
            pSlot->OpenDevice(sPath, sError);
            if(!sError.empty())
                Log("Error opening device: " + sError);
        }
        hid_free_enumeration(devs);
    }

    /// Ensures devices are in the correct order, swapping them if necessary.
    /// The correct order is:
    /// - Slot 0: Player 1 device (if connected)
    /// - Slot 1: Player 2 device (if connected)
    ///
    /// This function is called each I/O thread iteration to maintain proper
    /// device ordering as devices are connected and disconnected.
    void CorrectDeviceOrder()
    {
        SMXInfo info[2];
        m_Devices[0].GetInfoLocked(info[0]);
        m_Devices[1].GetInfoLocked(info[1]);

        if(info[0].m_bConnected && info[1].m_bConnected &&
           m_Devices[0].IsPlayer2Locked() == m_Devices[1].IsPlayer2Locked())
            return;

        const bool bSwap = (info[0].m_bConnected && m_Devices[0].IsPlayer2Locked()) ||
                     (info[1].m_bConnected && !m_Devices[1].IsPlayer2Locked());
        if(bSwap)
        {
            SMXDevice temp(std::move(m_Devices[0]));
            m_Devices[0] = std::move(m_Devices[1]);
            m_Devices[1] = std::move(temp);
        }
    }

    recursive_mutex m_Lock;
    thread m_Thread;
    thread m_USBPollingThread;
    condition_variable_any m_Cond;
    atomic<bool> m_bShutdown{false};
    atomic<int> m_iMainThreadSleepMs{50};
    atomic<int> m_iUSBPollingSleepUs{1000};
    SMXDevice m_Devices[2];
    function<void(int, SMXUpdateCallbackReason)> m_Callback;
};

// File-static singleton. No global variable visible outside this file.
shared_ptr<SMXManager> g_pSMX;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
// This section contains the C API functions exported to applications using the SDK.

/// Initializes the SMX SDK and starts searching for connected devices.
/// Must be called once before using any other SDK functions.
/// The background I/O thread will automatically discover connected devices and
/// invoke the update callback when their state changes.
/// @param callback Function to be called asynchronously when devices are connected,
///                  disconnected, or their input state changes.
/// @param pUser Application-defined pointer passed to all callbacks for context.
SMX_API void SMX_Start(SMXUpdateCallback callback, void *pUser)
{
    if(g_pSMX) return;
    hid_init();

    auto cb = [callback, pUser](const int pad, const SMXUpdateCallbackReason reason) {
        callback(pad, reason, pUser);
    };
    g_pSMX = make_shared<SMXManager>(cb);
}

/// Shuts down the SMX SDK and disconnects from all devices.
/// Stops the background I/O thread and cleans up resources.
/// IMPORTANT: This must not be called from within an update callback.
SMX_API void SMX_Stop()
{
    g_pSMX.reset();
    hid_exit();
}

/// Sets a custom callback function to receive diagnostic log messages.
/// If not set, log messages are printed to stdout with timestamps.
/// This can be called before SMX_Start to capture initialization logs.
/// @param callback Function that receives log strings. Can be nullptr to disable.
SMX_API void SMX_SetLogCallback(SMXLogCallback callback)
{
    SetLogCallback([callback](const string &log) {
        callback(log.c_str());
    });
}

/// Queries information about a connected device.
/// Use this to detect which pads are connected and retrieve their properties
/// (serial number, firmware version, player number).
/// @param pad Device index (0 for Player 1, 1 for Player 2).
/// @param info [out] Pointer to SMXInfo structure to be filled with device info.
SMX_API void SMX_GetInfo(const int pad, SMXInfo *info)
{
    if(!g_pSMX) return;
    const auto *dev = g_pSMX->GetDevice(pad);
    if(dev) dev->GetInfo(*info);
}

/// Retrieves the current input state (pressed panels) for a device.
/// The returned value is a 16-bit bitmask where each bit corresponds to a panel.
/// @param pad Device index (0 for Player 1, 1 for Player 2).
/// @return 16-bit input state bitmask. Returns 0 if the device is not connected.
SMX_API uint16_t SMX_GetInputState(const int pad)
{
    if(!g_pSMX) return 0;
    const auto *dev = g_pSMX->GetDevice(pad);
    return dev ? dev->GetInputState() : 0;
}

/// Assigns random serial numbers to all connected devices that don't have one.
/// The serial numbers are permanently written to the device's non-volatile memory.
/// This is an asynchronous operation; the actual programming happens in the
/// background I/O thread.
SMX_API void SMX_SetSerialNumbers()
{
    if(g_pSMX) g_pSMX->SetSerialNumbers();
}

SMX_API void SMX_SetPollingRate(int iMainThreadMs, int iUSBPollingUs)
{
    if(iMainThreadMs > 100)
        Log(ssprintf("Warning: main thread sleep of %dms may delay device connections and cause missed serial numbers. Recommended: 50ms or below.", iMainThreadMs));
    if(g_pSMX) g_pSMX->SetPollingRate(iMainThreadMs, iUSBPollingUs);
}

/// Returns the SDK version string.
/// @return C-string containing the version (e.g., "0.1.0").
SMX_API const char *SMX_Version()
{
    return SMX_VERSION;
}

/// Returns the elapsed time in seconds since the SDK was initialized.
/// This is useful for logging timestamps and measuring elapsed time.
/// @return Elapsed time in seconds as a double.
SMX_API double SMX_GetMonotonicTime()
{
    return GetMonotonicTime();
}

