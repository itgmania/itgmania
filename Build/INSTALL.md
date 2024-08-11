Warning
==
Make sure you read README.md first if you have not.

Compiling ITGmania
==
To use ITGmania on your computer, it is first assumed that cmake is run (see README.md for more information).
Then, follow the guide based on your operating system.

Windows
===
Using Visual Studio, simply build and it will place the .exe file in the correct directory. You will need Visual Studio, MSVC, C++ ATL, C++ MFC, and the Windows SDK; all of which can be installed with Visual Studio Installer.

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

