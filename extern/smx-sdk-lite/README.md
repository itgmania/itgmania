# SMX SDK Lite

A minimal, cross-platform SDK for StepManiaX dance pads. Supports device discovery, connection management, player identification, and reading panel input state.

See [the StepManiaX website](https://stepmaniax.com) and the [documentation](https://steprevolution.github.io/stepmaniax-sdk/) for more info.

SDK support: [sdk@stepmaniax.com](mailto:sdk@stepmaniax.com)

## Features

- Auto-discovers up to 2 connected SMX pads via USB HID
- Handles connect, disconnect, and reconnect
- Identifies P1 vs P2 and auto-corrects pad ordering
- Reads panel press/release state as a bitmask
- Assigns serial numbers to controllers
- Reports firmware version
- Cross-platform: Linux, macOS (Intel & Apple Silicon), Windows

## Dependencies

- **CMake** 3.14+
- **C++14** compiler
- **[hidapi](https://github.com/libusb/hidapi)** — lightweight HID library

## Building

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install build-essential cmake libhidapi-dev
```

```bash
cd sdk-lite
mkdir build && cd build
cmake ..
make
```

#### USB permissions

By default, HID devices require root access. To allow your user to access SMX pads without `sudo`, create a udev rule:

```bash
sudo tee /etc/udev/rules.d/99-stepmaniax.rules <<EOF
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="2341", ATTRS{idProduct}=="8037", MODE="0666"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and replug the pad after applying the rule.

### Linux (Fedora/RHEL)

```bash
sudo dnf install gcc-c++ cmake hidapi-devel
```

Then follow the same build and udev steps as above.

### Linux (Arch)

```bash
sudo pacman -S base-devel cmake hidapi
```

Then follow the same build and udev steps as above.

### macOS (Intel & Apple Silicon)

Install dependencies via [Homebrew](https://brew.sh):

```bash
brew install cmake hidapi
```

```bash
cd sdk-lite
mkdir build && cd build
cmake ..
make
```

This works on both Intel and Apple Silicon Macs. Homebrew installs native arm64 libraries on Apple Silicon and x86_64 on Intel. No special flags are needed — CMake will detect the correct architecture automatically.

**Note:** macOS may prompt you to allow the application to access USB devices. No additional permissions setup is required beyond granting that access.

### Windows

#### Option A: vcpkg

Install [vcpkg](https://github.com/microsoft/vcpkg) and [Visual Studio](https://visualstudio.microsoft.com/) (or the Build Tools with C++ workload).

```powershell
vcpkg install hidapi
```

```powershell
cd sdk-lite
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

#### Option B: MSYS2/MinGW

Install [MSYS2](https://www.msys2.org/), then from the MSYS2 MinGW64 shell:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-hidapi
```

```bash
cd sdk-lite
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

**Note:** On Windows, no special driver or permissions setup is needed. The SMX pads use standard USB HID and work with the built-in HID driver.

### Building as a Shared Library

By default, the build produces a static library (`libsmx-lite.a` / `smx-lite.lib`). To build a shared library (`.so` / `.dylib` / `.dll`), add `-DBUILD_SHARED_LIBS=ON`:

#### Linux

```bash
cmake .. -DBUILD_SHARED_LIBS=ON
make
# produces: libsmx-lite.so
```

#### macOS (Universal Binary — Intel + Apple Silicon)

```bash
cmake .. -DBUILD_SHARED_LIBS=ON
make
# produces: libsmx-lite.dylib (fat binary: x86_64 + arm64)
```

To build for a single architecture only:

```bash
cmake .. -DBUILD_SHARED_LIBS=ON -DCMAKE_OSX_ARCHITECTURES=arm64   # Apple Silicon only
cmake .. -DBUILD_SHARED_LIBS=ON -DCMAKE_OSX_ARCHITECTURES=x86_64  # Intel only
```

#### Windows (MSVC / vcpkg)

```powershell
cmake .. -DBUILD_SHARED_LIBS=ON -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
# produces: smx-lite.dll + smx-lite.lib (import library)
```

#### Windows (MSYS2 / MinGW)

```bash
cmake .. -DBUILD_SHARED_LIBS=ON -G "MinGW Makefiles"
mingw32-make
# produces: libsmx-lite.dll + libsmx-lite.dll.a
```

Only the public `SMX_*` API functions are exported from the shared library. All internal symbols are hidden.

## Running the sample

After building, run the sample application:

```bash
# Linux / macOS
./smx-sample

# With custom polling rates (main thread ms, USB polling thread us)
./smx-sample 50 500

# Windows (from build directory)
.\Release\smx-sample.exe   # vcpkg/MSVC
.\smx-sample.exe            # MSYS2/MinGW
```

The sample will scan for connected SMX pads and print connection events and panel input state changes to the console. Press `Ctrl+C` to quit.

Example output:

```
SMX SDK Lite v0.1.0
Scanning for StepManiaX devices... Press Ctrl+C to quit.
Usage: ./smx-sample [main_thread_ms] [usb_polling_us]
Pad 0 connected (jumper: P1, serial: 1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d, fw: 5)
Pad 0: input state 0000
Pad 0: input state 0010
```

## API overview

```c
// Start scanning for devices. Callback fires on connect/disconnect/input change.
void SMX_Start(SMXUpdateCallback callback, void *pUser);

// Stop and disconnect.
void SMX_Stop();

// Optional: redirect log output.
void SMX_SetLogCallback(SMXLogCallback callback);

// Get connection status, serial number, firmware version.
void SMX_GetInfo(int pad, SMXInfo *info);

// Get currently pressed panels as a bitmask.
uint16_t SMX_GetInputState(int pad);

// Assign serial numbers to controllers without one.
void SMX_SetSerialNumbers();

// Configure thread sleep intervals (main thread ms, USB polling thread us).
void SMX_SetPollingRate(int iMainThreadMs, int iUSBPollingUs);

// Get SDK version string.
const char *SMX_Version();
```

`pad` is 0 for player 1, 1 for player 2.

## SMXInfo fields

```c
struct SMXInfo {
    bool m_bConnected;        // true if pad is fully connected
    bool m_bIsPlayer2;        // physical player jumper setting on the PCB
    bool m_bHasSerialNumber;  // true if a serial number has been assigned
    char m_Serial[33];        // hex serial string (only valid if m_bHasSerialNumber)
    uint16_t m_iFirmwareVersion;
};
```

## Handling duplicate player jumpers

Each pad has a physical jumper on the PCB that sets it as P1 or P2. The SDK uses this to assign pads to slot 0 (P1) and slot 1 (P2). If both pads have the same jumper setting, the SDK can't determine which is which and will assign them to slots arbitrarily.

To detect this, check `m_bIsPlayer2` on both pads:

```c
SMXInfo info[2];
SMX_GetInfo(0, &info[0]);
SMX_GetInfo(1, &info[1]);

if(info[0].m_bConnected && info[1].m_bConnected &&
   info[0].m_bIsPlayer2 == info[1].m_bIsPlayer2)
{
    // Both pads have the same jumper setting.
    // Use serial numbers to tell them apart.
}
```

If pads don't have serial numbers assigned (`m_bHasSerialNumber` is false), call `SMX_SetSerialNumbers()` to assign them. Once assigned, serial numbers persist on the controller across power cycles and can be used to reliably identify specific pads regardless of jumper configuration.

## Panel bitmask

`SMX_GetInputState` returns a `uint16_t` where each bit corresponds to a panel:

```
Bit:    0   1   2   3   4   5   6   7   8
Panel: ┌───┬───┬───┐
       │ 0 │ 1 │ 2 │
       ├───┼───┼───┤
       │ 3 │ 4 │ 5 │
       ├───┼───┼───┤
       │ 6 │ 7 │ 8 │
       └───┴───┴───┘
```

## Project structure

```
sdk-lite/
├── SMX.h                    # Public API header
├── SMX.cpp                  # Helpers, device logic, manager, API implementation
├── SMXDeviceConnection.h    # HID I/O class (header)
├── SMXDeviceConnection.cpp  # HID I/O class (implementation)
├── SMXConfigPacket.h        # Internal config struct
├── SMXConfigPacket.cpp      # Old firmware config format conversion
├── sample.cpp               # Sample application
└── CMakeLists.txt           # Build configuration
```

## License

See [LICENSE.txt](../LICENSE.txt) in the repository root.
