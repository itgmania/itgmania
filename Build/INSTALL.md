Warning
==
Make sure you read README.md first if you have not.

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

