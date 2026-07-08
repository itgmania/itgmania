Most users will find everything they need in these instructions.

There are differences in the build process for ITGmania as compared to Stepmania, so it is recommended to stick with these instructions.

For anything not covered in this guide, you can usually refer to the original Stepmania documents [here](https://github.com/stepmania/stepmania/wiki/Compiling-StepMania), as long as you replace references to Stepmania with ITGmania.

# Prerequisites

## Compiler

The default compiler for each OS is as follows:

 - Windows: Visual Studio 2022/2026
   - VS 2022 is required to build with Windows 7 compatibility 
   - Installation of all components is handled via the Visual Studio Installer
 - macOS: Xcode
   - A minimal command-line only version of Xcode can be installed by running `xcode-select --install`.
   - The full GUI version of Xcode can also be used, but you have to manually specify the Xcode generator.
 - *nix: GNU GCC
   - Clang also works well. 
   
## Git

git is needed to clone the repository as well as prepare the submodules.

For *nix users, usually you would just install the `git` package with your package manager.

macOS users can use Homebrew to install git, or use [GitHub Desktop](https://desktop.github.com/download/).

Windows users can use [Git for Windows](https://git-scm.com/install/windows), or use [GitHub Desktop](https://desktop.github.com/download/).
   
## CMake

The common way of installing CMake is to use your OS's package manager, or for a GUI version, to go to [CMake's download page](http://www.cmake.org/download/). 

The minimum required version of CMake is currently **CMake 3.20**.

### Windows

A GUI CMake installer is available from the CMake download page linked above.

For those that prefer package manager systems, [Chocolatey](https://chocolatey.org/) has a CMake package. Run `choco install cmake` to get the latest stable version.

### macOS

Both [Homebrew](http://brew.sh/) and [MacPorts](https://www.macports.org/) offer CMake as part of their offerings. Run `brew install cmake` or `port install cmake` respectively to get the latest stable version.

### Linux

The package is usually just called `cmake` via your distribution's package manager.

Note that the Linux build depends on certain libraries on your system which would also be provided by your distribution. You can find those listed [here](https://github.com/itgmania/itgmania/discussions/403).

## ccache

ccache is optional, but supported. If you are rebuilding the game frequently, this can save a lot of time. You'll need to pass the `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` flags to CMake.

# Local Development

## Warning

Using CMake is considered stable, but not every single combination is tested frequently.
Using the default options as suggested should cause minimal (if any) problems.

## Preparing Submodules

Make sure you initialize the submodules after cloning the repository. This is required. If you use GitHub Desktop, this should be done automatically upon cloning the ITGmania repository.

```sh
git clone https://github.com/itgmania/itgmania.git
cd itgmania
git submodule update --init --recursive
```

After that, you can follow the CMake instructions below to create the build.

## CMake Usage

There are two ways of working with CMake: the command line and the GUI.

### CMake Command Line

If you are unfamiliar with cmake, first run `cmake --help`. This will present a list of options and generators.
The generators are used for setting up your project.

The following steps will assume you operate from the ITGmania project's Build directory.

For the first setup, enter the `itgmania` directory, and run the following command:

`cmake -S . -B build`

You can apply other build flags at this time.

### CMake GUI

Press the "Browse Source..." button and choose the itgmania directory you just cloned using `git`.

Inside the itgmania directory, create a directory called `build`. Press the "Browse Build..." button and choose that `build` directory you just created. 

Upon setting the source and build directories, you should press the `Configure` button.

If no errors show up, you can then press the `Generate` button.

If the cmake project file changes, you can just re-generate the build to get up to date.

If this by itself doesn't work, you may have to clean the CMake cache.
Go to File -> Delete Cache, and then run the `Configure` and `Generate` steps again.

## Release vs Debug

Without any special options provided in the CMake configuration stage, you will generate a Release build.

When generating your cmake files for the first time (or after any cache delete),
pass in `-DCMAKE_BUILD_TYPE=Debug` for a debug build.

We have `RelWithDbgInfo` and `MinSizeRel` builds available as well.

It is advised to clean your CMake cache if you switch build types.

Note that if you use an IDE like Visual Studio or Xcode, you do not need to worry about setting the build type - you can edit the build type directly in the IDE.

# Compiling ITGmania

## Windows

Everything needed for building on Windows is available via the Visual Studio Installer:
- The "Desktop development with C++" workload (which includes MSVC, C++ ATL, C++ MFC, C++ modules, C++/CLI support),
- The Windows SDK.

Upon launching the Visual Studio Installer, you will be in the _Workloads_ section. Here, you should select "Desktop development with C++".
![image](https://github.com/user-attachments/assets/7ed2370d-c891-4d7b-9ca5-c9ad39ad969c)

The **Windows SDK** is also required, so click the _Individual components_ section. Either use the search, or scroll thru the list until you find the section titled _SDK's, libraries, and frameworks_ to find the Windows SDK.

![image](https://github.com/user-attachments/assets/8cf1b443-bfbe-4c2f-af39-a904324956f5)

Confirm the installation. Once everything is installed, you can open **StepMania.sln** with Visual Studio. Set the build type to **Release** and press the hollow green button (or Ctrl+F5) to begin building.

![image](https://github.com/user-attachments/assets/f9235e14-bfc8-4f8f-8b30-9706dfb3bcc6)

## macOS

Your architecture will be automatically detected, but to specify which platform you are building for, you can append the `CMAKE_OSX_ARCHITECTURES` flag with either `arm64` or `x86_64` as options.

Afterwards, run `cmake --build build --parallel "$(sysctl -n hw.logicalcpu)"`. After this step is done, the newly built ITGmania.app will be in your `itgmania` directory.

## Linux

From the `itgmania` directory (not `src`), run `cmake --build build --parallel $(nproc)`. The `itgmania` executable will be in the same directory you are in, so you can type `./itgmania` to run the game.

# Installing ITGmania

Installing in this context refers to placing the folders and generated binary in a standard location based on your operating system.
This guide assumes default install locations.
If you want to change the initial location, pass in `-DCMAKE_INSTALL_PREFIX=/new/path/here` when configuring your local setup.

## Windows

With the instructions as described, an installer won't be generated. The generated exe will be in the `Program` folder within your cloned `itgmania` directory. You need to invoke CPack and have [NSIS](https://nsis.sourceforge.io/Download) installed to create an installer.

## macOS

The `ITGmania.app` package can be copied to `/Applications` and it will work as expected.

## Linux

After installing, run `sudo make install`. The files will be placed in the location specified:
by default, that is now `/usr/local/itgmania`.
