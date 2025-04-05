Instructions
==

Most users will find everything they need in this README. There are differences in the build process for ITGmania as compared to Stepmania, so it is recommended to stick with these ITGmania instructions. For anything not covered in this guide, you can usually refer to the original Stepmania documents [here](https://github.com/stepmania/stepmania/wiki/Compiling-StepMania), as long as you replace references to Stepmania with ITGmania.

Warning
==

Using CMake is considered stable, but not every single combination is known to work.
Using the defaults as suggested should cause minimal problems.

Init Submodules
==
Make sure you initialize the submodules after cloning the repository. This is required.

```sh
git clone --depth=1 https://github.com/itgmania/itgmania.git
cd itgmania
git submodule update --init --recursive
```

After that, you can follow the cmake instructions below to create the build.

Install CMake
==

At first, you have to install CMake.

All OSes
===

The common way of installing CMake is to go to [CMake's download page](http://www.cmake.org/download/). At this time of writing, the latest version is 3.23.2. The minimum version supported at this time is 3.20.

If this approach is used, consider using the binary distributions. Most should also provide a friendly GUI interface.

Windows
===

For those that prefer package manager systems, [Chocolatey](https://chocolatey.org/) has a CMake package. Run `choco install cmake` to get the latest stable version.

macOS Specific
===

For those that prefer package manager systems, both [Homebrew](http://brew.sh/) and [MacPorts](https://www.macports.org/) offer CMake as part of their offerings. Run `brew install cmake` or `port install cmake` respectively to get the latest stable version.

Linux
===

There are many package managers available for Linux. Look at your manual for more details. Either that, or utilize the All OS specific approach.


CMake Usage
==

There are two ways of working with cmake: the command line and the GUI.

CMake Command Line
===

If you are unfamiliar with cmake, first run `cmake --help`. This will present a list of options and generators.
The generators are used for setting up your project.

The following steps will assume you operate from the ITGmania project's Build directory.

For the first setup, you will want to run this command:

`cmake -G {YourGeneratorHere} .. && cmake ..`

Replace {YourGeneratorHere} with one of the generator choices from `cmake --help`.

If you are building on Windows and expecting your final executable to be able to run on Windows XP, append an additional parameter `-T "v140_xp"` (or `-T "v120_xp"`, depending on which version of Visual Studio you have installed) to your command line.

If any cmake project file changes, you can just run `cmake .. && cmake ..` to get up to date.
If this by itself doesn't work, you may have to clean the cmake cache.
Use `rm -rf CMakeCache.txt CMakeScripts/ CMakeFiles/ cmake_install.txt` to do that, and then run the generator command again as specified above.

The reason for running cmake at least twice is to make sure that all of the variables get set up appropriately.

Environment variables can be modified at this stage. If you want to pass `-ggdb` or any other flag that is not set up by default,
utilize `CXXFLAGS` or any appropriate variable.

CMake GUI
===

For those that use the GUI to work with cmake, you need to specify where the source code is and where the binaries will be built.
The first one, counter-intuitively, is actually the parent directory of this one: the main ITGmania directory.
The second one for building can be this directory.

Upon setting the source and build directories, you should `Configure` the build.
If no errors show up, you can hit `Generate` until none of the rows on the GUI are red.

If the cmake project file changes, you can just generate the build to get up to date.
If this by itself doesn't work, you may have to clean the cmake cache.
Go to File -> Delete Cache, and then run the `Configure` and `Generate` steps again.

Release vs Debug
==

If you are generating makefiles with cmake, you will also need to specify your build type.
Most users will want to use `RELEASE` while some developers may want to use `DEBUG`.

When generating your cmake files for the first time (or after any cache delete),
pass in `-DCMAKE_BUILD_TYPE=Debug` for a debug build. We have `RelWithDbgInfo` and `MinSizeRel` builds available as well.

It is advised to clean your cmake cache if you switch build types.

Note that if you use an IDE like Visual Studio or Xcode, you do not need to worry about setting the build type.
You can edit the build type directly in the IDE.

Compiling ITGmania
==
To use ITGmania on your computer, it is first assumed that **CMake** is run (see README.md for more information).
Then, follow the guide based on your operating system.

Windows
===
Everything needed for building on Windows is available via the Visual Studio Installer:
- The "Desktop development with C++" workload (which includes MSVC, C++ ATL, C++ MFC, C++ modules, C++/CLI support),
- The Windows SDK.

Upon launching the Visual Studio Installer, you will be in the _Workloads_ section. Here, you should select "Desktop development with C++".
![image](https://github.com/user-attachments/assets/7ed2370d-c891-4d7b-9ca5-c9ad39ad969c)

The **Windows SDK** is also required, so click the _Individual components_ section. Either use the search, or scroll thru the list until you find the section titled _SDK's, libraries, and frameworks_ to find the Windows SDK.

![image](https://github.com/user-attachments/assets/8cf1b443-bfbe-4c2f-af39-a904324956f5)

Confirm the installation. Once everything is installed, you can open **StepMania.sln** with Visual Studio. Set the build type to **Release** and press the hollow green button (or Ctrl+F5) to begin building.

![image](https://github.com/user-attachments/assets/f9235e14-bfc8-4f8f-8b30-9706dfb3bcc6)

macOS
===
Using Xcode, simply build in Xcode and it will place the .app file in the correct directory.

Linux
===
From the `itgmania` directory (not `src`), run `cmake -B build` followed by `sudo make install`. The `itgmania` executable will be in the same directory you are in, so you can type `./itgmania` to run the game.

Installing ITGmania
==
Installing in this context refers to placing the folders and generated binary in a standard location based on your operating system.
This guide assumes default install locations.
If you want to change the initial location, pass in `-DCMAKE_INSTALL_PREFIX=/new/path/here` when configuring your local setup.

Windows
===
The default installation directory is `C:\Games\ITGmania`.

macOS
===
The `ITGmania.app` package can be copied to `/Applications` and it will work as expected.

Linux
===
After installing, run `sudo make install`. The files will be placed in the location specified:
by default, that is now `/usr/local/itgmania`.

Last Words
==

With that, you should be good to go.
If there are still questions, view the resources on the parent directory's README.md file.

