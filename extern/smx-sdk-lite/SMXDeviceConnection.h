#ifndef SMXDeviceConnection_h
#define SMXDeviceConnection_h

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <hidapi/hidapi.h>

namespace SMX {

/// Immutable device information retrieved from the hardware on connection.
/// This struct holds the device metadata that doesn't change during normal operation.
struct SMXDeviceInfo
{
    /// True if this device's physical jumper is set to Player 2 mode.
    bool m_bP2 = false;

    /// Serial number as a null-terminated hex string (32 chars + null terminator).
    /// Format: 32 lowercase hex characters representing 16 bytes of device serial.
    char m_Serial[33] = {};

    /// Device firmware version number.
    uint16_t m_iFirmwareVersion = 0;
};

/// Low-level USB communication abstraction for a single StepManiaX device.
///
/// This class handles:
/// - Opening HID connections to the device
/// - Sending commands and receiving responses asynchronously
/// - Reading input state (pressed panels) from the device
/// - Requesting and parsing device information
/// - Buffering and fragmenting data across HID packets (64 bytes max)
///
/// The class is non-copyable but movable to support device reordering in the manager.
/// All operations are nonblocking; commands are queued and processed in the background.
///
/// ============================================================================
/// THREADING MODEL: Split Packet Handling for Low-Latency Input State
/// ============================================================================
///
/// This class is accessed by TWO independent threads simultaneously:
///
/// 1. USB Polling Thread (every ~1ms, non-blocking):
///    - Calls PollUSBData() to read raw HID packets
///    - Parses Report 3 (input state) packets completely and inline
///    - Updates m_iInputState atomically (no lock needed)
///    - Extracts Report 6 (command/config) packets and appends to m_sReport6Buffer
///    - File reporting: see USBPollingThreadMain() in SMX.cpp
///
/// 2. Main I/O Thread (every ~50ms, holds m_pLock):
///    - Calls CheckReads() to process Report 6 packets from m_sReport6Buffer
///    - Handles fragmentation (START/END flags), command callbacks, timeouts
///    - Never touches Report 3 or m_iInputState
///    - File reporting: see ThreadMain() in SMX.cpp
///
/// MEMBER VARIABLE THREADING GUARANTEES:
///
///   Thread-Safe Atomics (lock-free):
///   - m_iInputState: std::atomic<uint16_t>, updated by USB thread, read by main thread
///
///   Protected by m_Report6BufferMutex (USB thread writes, main thread reads):
///   - m_sReport6Buffer: Report 6 packets accumulated by USB thread, consumed by CheckReads()
///
///   Protected by External Lock (m_pLock held when called):
///   - m_sReadBuffers: completed packets queued for application
///   - m_sCurrentReadBuffer: fragment accumulation for Report 6
///   - m_aPendingCommands, m_pCurrentCommand: command queue state
///   - m_bActive, m_bGotInfo: connection state
///
///   Main Thread Only (no synchronization needed):
///   - m_pDevice, m_sPath, m_DeviceInfo: immutable after Open()
///   - m_pInputStateChangedCallback: only modified during setup
///
/// PROTOCOL DETAILS:
///
///   Report 3 (Input State): 3 bytes [ID=3][low byte][high byte]
///   - Parsed inline in PollUSBData(), never buffered
///   - Updates m_iInputState atomically with full 16-bit value
///   - Bit layout: 0-8 = panels, 9-15 = unused
///
///   Report 6 (Commands/Config): [ID=6][flags][size][payload...]
///   - Variable length: 3-byte header + 0-61 bytes payload
///   - Fragmentation flags (cf. PACKET_FLAG_*):
///     • 0x04 (START_OF_COMMAND): clears buffered fragment
///     • 0x01 (END_OF_COMMAND): queues complete packet to m_sReadBuffers
///     • 0x02 (HOST_CMD_FINISHED): invokes command callback
///   - Buffered in m_sReport6Buffer by USB thread, processed by main thread
///
class SMXDeviceConnection
{
public:
    SMXDeviceConnection();
    ~SMXDeviceConnection();

    // Non-copyable (prevents accidental duplicate connections)
    SMXDeviceConnection(const SMXDeviceConnection &) = delete;
    SMXDeviceConnection &operator=(const SMXDeviceConnection &) = delete;

    // Movable (required for device reordering by pad index)
    SMXDeviceConnection(SMXDeviceConnection &&other) noexcept;
    SMXDeviceConnection &operator=(SMXDeviceConnection &&other) noexcept;

    /// Opens a HID connection to the device at the given path.
    /// Automatically requests device info and enters a pending state until the info arrives.
    /// @param sPath HID device path string.
    /// @param sError [out] Error message if open fails.
    /// @return True if the device was successfully opened, false otherwise.
    bool Open(const std::string &sPath, std::string &sError);

    /// Closes the connection and cancels all pending commands.
    /// Invokes completion callbacks with empty strings to notify of cancellation.
    void Close();

    /// Returns true if the HID connection is open (though device info may not be retrieved yet).
    bool IsConnected() const { return m_pDevice != nullptr; }

    /// Returns true if the connection is open AND device info has been received.
    bool IsConnectedWithDeviceInfo() const { return m_pDevice != nullptr && m_bGotInfo; }

    /// Returns the HID device path.
    std::string GetPath() const { return m_sPath; }

    /// Retrieves the cached device information.
    /// Only valid after IsConnectedWithDeviceInfo() returns true.
    SMXDeviceInfo GetDeviceInfo() const { return m_DeviceInfo; }

    /// Processes I/O operations. Called once per frame from the I/O thread.
    /// Performs nonblocking reads from the HID device, writes pending commands,
    /// and handles command timeouts.
    /// @param sError [out] Error message if an error occurs.
    void Update(std::string &sError);

    /// Sets whether the device should actively send input state updates.
    /// When active, the device continuously sends input packets; when inactive,
    /// it only responds to commands.
    void SetActive(const bool bActive) { m_bActive = bActive; }

    /// Returns whether the device is actively sending input updates.
    bool GetActive() const { return m_bActive; }

    /// Reads a completed packet from the internal buffer.
    /// Packets are queued as they are fully received from the device.
    /// @param out [out] String containing the packet data if available.
    /// @return True if a packet was available and has been dequeued, false if empty.
    bool ReadPacket(std::string &out);

    /// Queues a command to be sent to the device asynchronously.
    /// The command is automatically fragmented into 64-byte HID packets.
    /// @param cmd Command string to send.
    /// @param pComplete Optional callback invoked when the device responds (or on error).
    void SendCommand(const std::string &cmd, std::function<void(std::string response)> pComplete = nullptr);

    /// Retrieves the current input state (pressed panels) bitmask.
    uint16_t GetInputState() const { return m_iInputState.load(); }

    /// Sets a callback to be invoked when input state (Report 3) changes from the USB polling thread.
    /// @param cb Callback function with no parameters. Called immediately when input state changes.
    void SetInputStateChangedCallback(std::function<void()> cb) { m_pInputStateChangedCallback = std::move(cb); }

    /// Polls for available USB data, called by the USB polling thread.
    /// Parses Report 3 (input state) inline and buffers Report 6 for the main thread.
    /// @param sError [out] Error message if a read fails.
    /// @return True if Report 6 data was buffered.
    bool PollUSBData(std::string &sError);

private:
    /// Sends a device info request packet to the device.
    /// The response is handled asynchronously in HandleUsbPacket() and sets m_bGotInfo.
    /// @param pComplete Optional callback for the device info response.
    void RequestDeviceInfo(std::function<void(std::string response)> pComplete = nullptr);

    /// Processes all available data from the HID device.
    /// Reads packets until no more data is available, handling command timeouts.
    /// @param sError [out] Error message if a read fails.
    void CheckReads(std::string &sError);

    /// Sends the next pending command to the device if no command is currently in flight.
    /// Breaks the command into 64-byte HID packets and sends them sequentially.
    /// @param sError [out] Error message if a write fails.
    void CheckWrites(std::string &sError);

    /// Processes a single Report 6 USB packet received from the device.
    /// Handles command/config packets with fragmentation flags.
    /// @param buf Packet data including report ID as first byte.
    void HandleUsbPacket(const std::string &buf);

    hid_device *m_pDevice = nullptr;
    std::string m_sPath;
    bool m_bActive = false;
    bool m_bGotInfo = false;

    std::list<std::string> m_sReadBuffers;
    std::string m_sCurrentReadBuffer;

    std::atomic<uint16_t> m_iInputState{0};

    std::string m_sReport6Buffer;
    std::mutex m_Report6BufferMutex;

    SMXDeviceInfo m_DeviceInfo;
    std::function<void()> m_pInputStateChangedCallback;

    /// Represents a command pending transmission or awaiting response.
    /// Commands may be fragmented into multiple 64-byte HID packets.
    struct PendingCommand {
        std::string sData;                                        // Raw command data (all HID packets combined)
        std::function<void(std::string response)> m_pComplete;    // Callback when response received
        bool m_bIsDeviceInfoCommand = false;                      // True if this is a device info request
        bool m_bSent = false;                                     // True if sent to device and awaiting response
        double m_fSentAt = 0;                                     // Time when command was sent (for timeout detection)
    };

    std::list<std::shared_ptr<PendingCommand>> m_aPendingCommands; // Queue of commands not yet sent
    std::shared_ptr<PendingCommand> m_pCurrentCommand;             // Command currently awaiting response
};

}

#endif
