#ifndef SMX_H
#define SMX_H

#include <cstdint>

#ifdef SMX_EXPORTS
    #ifdef _WIN32
        #define SMX_API extern "C" __declspec(dllexport)
    #else
        #define SMX_API extern "C" __attribute__((visibility("default")))
    #endif
#else
    #ifdef _WIN32
        #define SMX_API extern "C" __declspec(dllimport)
    #else
        #define SMX_API extern "C"
    #endif
#endif

#define SERIAL_SIZE 16

struct SMXInfo;

// All functions are nonblocking. Getters return the most recent state.
// Setters return immediately and do their work in the background.

/// Reason codes for device state change callbacks.
/// When a device is connected, disconnected, or its input state changes,
/// this reason is passed to the update callback to indicate what happened.
/// Multiple reasons can be combined using bitwise OR to handle multiple state changes.
enum SMXUpdateCallbackReason : uint32_t {
    /// Always set on every callback firing. Applications can use this as a catch-all.
    SMXUpdateCallback_Updated = 1 << 0,

    /// Input state (pressed panels) has changed.
    /// When this is fired, SMX_GetInputState() will return the new state.
    SMXUpdateCallback_InputState = 1 << 1,

    /// Device has become fully connected (device info and config received).
    SMXUpdateCallback_Connected = 1 << 2,

    /// Device has been disconnected.
    SMXUpdateCallback_Disconnected = 1 << 3,

    /// Device configuration has been received or updated.
    SMXUpdateCallback_ConfigUpdated = 1 << 4,
};

// Helper macro for checking if a reason includes a specific flag
#define SMX_REASON_IS(reason, flag) (((reason) & (flag)) != 0)

/// Callback function type for device state changes.
/// Called asynchronously from the I/O thread when a device is connected, disconnected,
/// or its state changes (input, configuration, etc.).
///
/// IMPORTANT: This callback should return quickly to avoid stalling the I/O thread.
/// Do not call SMX_Stop() from within this callback. Long-running operations should be
/// queued for processing on the main application thread.
///
/// @param pad Device index (0 for Player 1, 1 for Player 2).
/// @param reason Reason for the callback (SMXUpdateCallback_Updated, SMXUpdateCallback_InputState, or combination).
///               Use SMX_REASON_IS(reason, SMXUpdateCallback_InputState) to check for input changes.
/// @param pUser Application context pointer passed to SMX_Start().
typedef void SMXUpdateCallback(int pad, SMXUpdateCallbackReason reason, void *pUser);

/// Initializes the SMX SDK and starts searching for connected devices.
/// Must be called once before using any other SDK functions.
/// The background I/O thread will automatically discover connected devices and
/// invoke the update callback when their state changes.
///
/// @param callback Function to be called asynchronously when devices are connected,
///                  disconnected, or their input state changes.
/// @param pUser Application-defined pointer passed to all callbacks for context.
SMX_API void SMX_Start(SMXUpdateCallback callback, void *pUser);

/// Shuts down the SMX SDK and disconnects from all devices.
/// Stops the background I/O thread, closes all device connections, and cleans up resources.
/// This function waits for the I/O thread to finish, so it may block briefly.
///
/// IMPORTANT: This must not be called from within an update callback.
/// If you need to shut down in response to a device event, queue it for later.
SMX_API void SMX_Stop();

typedef void SMXLogCallback(const char *log);

/// Sets a custom callback function to receive diagnostic log messages.
/// If not set, log messages are printed to stdout with timestamps.
/// This can be called before SMX_Start to capture initialization logs.
///
/// The log callback is invoked from both the main thread and the background I/O thread,
/// so it should be thread-safe if logging to shared resources.
///
/// @param callback Function that receives log strings. Pass nullptr to disable custom logging.
SMX_API void SMX_SetLogCallback(SMXLogCallback callback);

/// Queries information about a connected device.
/// Use this to detect which pads are connected and retrieve their properties
/// (serial number, firmware version, player number).
///
/// @param pad Device index (0 for Player 1, 1 for Player 2).
/// @param info [out] Pointer to SMXInfo structure to be filled with device info.
///            If pad is invalid or device is not connected, all fields are zeroed.
SMX_API void SMX_GetInfo(int pad, SMXInfo *info);

/// Retrieves the current input state (pressed panels) for a device.
/// The returned value is a 16-bit bitmask where each bit corresponds to a panel.
/// Bit positions correspond to the 16 panels on an SMX device.
///
/// @param pad Device index (0 for Player 1, 1 for Player 2).
/// @return 16-bit input state bitmask. Returns 0 if the device is not connected.
SMX_API uint16_t SMX_GetInputState(int pad);

/// Assigns random serial numbers to all connected devices that don't have one.
/// The serial numbers are permanently written to the device's non-volatile memory
/// and identify the physical controller.
/// This is an asynchronous operation; the actual programming happens in the
/// background I/O thread.
///
/// Note: Devices without a serial number show a hex string of all zeros or all F's.
SMX_API void SMX_SetSerialNumbers();

/// Configures the polling rates for the SDK's background threads.
/// Can be called at any time after SMX_Start().
///
/// @param iMainThreadMs Sleep time in milliseconds for the main I/O thread (default: 50).
///                      Controls how often device connections and command responses are processed.
///                      Values above ~100ms may delay device connection handshakes, causing
///                      devices to appear uninitialized or missing serial numbers.
/// @param iUSBPollingUs Sleep time in microseconds for the USB polling thread (default: 1000).
///                      Controls input state latency. Lower values = lower latency but more CPU.
SMX_API void SMX_SetPollingRate(int iMainThreadMs, int iUSBPollingUs);

/// Returns the SDK version string.
/// @return C-string containing the version (e.g., "0.1.0").
SMX_API const char *SMX_Version();

/// Returns the elapsed time in seconds since the SDK was initialized.
/// This is useful for logging timestamps and measuring elapsed time.
/// Uses a high-resolution monotonic clock that is not affected by system clock adjustments.
///
/// @return Elapsed time in seconds as a double (e.g., 1.234 for 1.234 seconds).
SMX_API double SMX_GetMonotonicTime();

/// Information about a connected SMX device.
/// This structure holds the current connection state and device metadata.
/// Query it with SMX_GetInfo() to detect devices and retrieve their properties.
struct SMXInfo
{
    /// True if we're fully connected to this controller.
    /// A device is fully connected when it has a valid HID connection,
    /// device info has been retrieved, and configuration is available.
    bool m_bConnected = false;

    /// True if the physical player jumper is set to player 2 mode.
    /// Player 1 device is typically placed in slot 0, Player 2 in slot 1,
    /// but the SDK automatically reorders devices to maintain this convention.
    bool m_bIsPlayer2 = false;

    /// True if this controller has been assigned a serial number.
    /// If false, call SMX_SetSerialNumbers() to assign one.
    /// Once assigned, the serial number is permanently stored in the device.
    bool m_bHasSerialNumber = false;

    /// Device serial number (null-terminated hex string, 32 chars + null).
    /// Format: 32 lowercase hexadecimal characters representing 16 bytes of device serial.
    /// Only meaningful if m_bHasSerialNumber is true.
    char m_Serial[33] = {};

    /// Device firmware version number.
    /// Used internally to determine protocol compatibility.
    uint16_t m_iFirmwareVersion = 0;
};

#endif
