#ifndef SMXConfigPacket_h
#define SMXConfigPacket_h

#include <cstdint>
#include <vector>

// SMXConfig is needed internally for the connection handshake (the device isn't
// considered "connected" until its config is read). We keep the struct here but
// don't expose it in the public API.

/// Packed sensor settings for a single panel.
/// Contains low/high thresholds for load cell and FSR (Force Sensitive Resistor) sensors,
/// as well as combined (multi-sensor) thresholds. All values are tightly packed.
#pragma pack(push, 1)
struct packed_sensor_settings_t {
    uint8_t loadCellLowThreshold;         // Load cell activation threshold
    uint8_t loadCellHighThreshold;        // Load cell deactivation threshold
    uint8_t fsrLowThreshold[4];           // FSR activation thresholds (4 sensors)
    uint8_t fsrHighThreshold[4];          // FSR deactivation thresholds (4 sensors)
    uint16_t combinedLowThreshold;        // Combined sensor activation threshold
    uint16_t combinedHighThreshold;       // Combined sensor deactivation threshold
    uint16_t reserved;                    // Padding/reserved for future use
};

/// Configuration state for an SMX device, sent and received during initialization.
/// Describes debounce settings, sensor thresholds, panel colors, auto-calibration parameters, etc.
/// This structure is read from the device during connection handshake and cached in SMXDevice.
/// The internal config is not exposed in the public API; it's used only for connection verification.
struct SMXConfig
{
    uint8_t masterVersion = 0xFF;                                // Firmware master version
    uint8_t configVersion = 0x05;                                // Config format version
    uint8_t flags = 0;                                           // Configuration flags
    uint16_t debounceNodelayMilliseconds = 0;                    // Debounce with no delay (ms)
    uint16_t debounceDelayMilliseconds = 0;                      // Debounce with delay (ms)
    uint16_t panelDebounceMicroseconds = 4000;                   // Panel debounce timing (μs)
    uint8_t autoCalibrationMaxDeviation = 100;                   // Max deviation for auto-calibration
    uint8_t badSensorMinimumDelaySeconds = 15;                   // Delay before marking sensor bad (s)
    uint16_t autoCalibrationAveragesPerUpdate = 60;              // Number of averages per calibration update
    uint16_t autoCalibrationSamplesPerAverage = 500;             // Samples per average in calibration
    uint16_t autoCalibrationMaxTare = 0xFFFF;                    // Maximum tare value for auto-cal
    uint8_t enabledSensors[5]{};                                 // Bitmask of enabled sensors (5 bytes = 40 bits)
    uint8_t autoLightsTimeout = 1000/128;                        // Auto light timeout (units of 128ms)
    uint8_t stepColor[3*9]{};                                    // RGB color for each of 9 panels (3 bytes each)
    uint8_t platformStripColor[3]{};                             // RGB color for platform light strip
    uint16_t autoLightPanelMask = 0xFFFF;                        // Bitmask of panels that auto-light
    uint8_t panelRotation{};                                     // Panel rotation setting
    packed_sensor_settings_t panelSettings[9]{};                 // Sensor settings for each panel
    uint8_t preDetailsDelayMilliseconds = 5;                     // Delay before sending details (ms)
    uint8_t padding[49]{};                                       // Padding to reach desired struct size
};
#pragma pack(pop)

/// Converts old config format (v3) to new config format (v5).
/// This function handles firmware version upgrades by mapping old config fields to new ones.
/// If the old config uses an uninitialized version (0xFF), basic defaults are applied.
/// Supports incremental upgrades through intermediate versions.
///
/// Version history:
/// - v3 and earlier: Original sensor threshold layout
/// - v2: Additional panel thresholds added (panels 0, 3, 5, 6, 8)
/// - v3: debounceDelayMilliseconds field added
/// - v5 (current): Sensor settings restructured into packed_sensor_settings_t per panel
///
/// @param oldConfig Raw old config data (as vector of bytes)
/// @param newConfig Output config struct to be populated with converted values
void ConvertToNewConfig(const std::vector<uint8_t> &oldConfig, SMXConfig &newConfig);

#endif
