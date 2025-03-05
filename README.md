ITGmania
========

ITGmania is a fork of [StepMania 5.1](https://github.com/stepmania/stepmania/tree/5_1-new), an advanced cross-platform rhythm game for home and arcade use.

[![Continuous integration](https://github.com/itgmania/itgmania/actions/workflows/ci.yml/badge.svg?branch=beta)](https://github.com/itgmania/itgmania/actions/workflows/ci.yml)

## Changes to StepMania 5.1

- Built-in network functionality
- Fully 64-bit, optimized for modern OSes
- Reload new songs from within the song select screen
- The mine fix applied (courtesy of [DinsFire64](https://gist.github.com/DinsFire64/4a3f763cd3033afd55a176980b32a3b5))
- Held misses tracked in the engine for pad debugging
- Fixed overlapping hold bug
- Per-player visual delay
- Per-player disabling of timing windows
- New preference to control note render ordering
- Increased the Stats.xml file size limit to 100MB
- Changed the default binding for P2/back from hyphen to backslash

## Installation

You can choose between using the installer or using the portable build. Using the installer is recommended, because it makes upgrading to new versions easier.

### Windows

**Windows 7 is the minimum supported version.**

 * You will likely have to manually allow the installer to start.

### macOS

**macOS users need to have macOS 11 (Big Sur) or higher to run ITGmania.**
* Move ITGmania.app to the Applications folder, and then run the following command in Terminal:

   * `xattr -dr com.apple.quarantine /Applications/ITGmania`
   
* You should then add ITGmania to the "Input Monitoring" section of System Preferences (under Security & Privacy)

### Linux

**Linux users should receive all they need from the package manager of their choice.**

* **Debian-based**:

  * `sudo apt install libgdk-pixbuf-2.0-0 libgl1 libglvnd0 libgtk-3-0 libusb-0.1-4 libxinerama1 libxtst6`

* **Fedora-based**:

  * `sudo yum install gdk-pixbuf2 gtk3 libusb-compat-0.1 libXinerama libXtst`

*  **Arch Linux**:

   * `sudo pacman -S mesa gtk3 libusb-compat libxinerama libxtst llvm-libs`

* **OpenSUSE**:

   * OpenSUSE comes with everything you need pre-installed.


### Build From Source

ITGmania can be compiled using [CMake](http://www.cmake.org/). More information about using CMake to build ITGmania can be found in both the `Build` directory and CMake's documentation.

## Resources

* [ITGmania Website](https://www.itgmania.com/)
* [StepMania 5.1 to ITGmania Migration Guide](Docs/Userdocs/sm5_migration.md)
* [Lua for ITGmania](https://itgmania.github.io/lua-for-itgmania/)
* Lua API Documentation can be found in the Docs folder.

## Licensing Terms

ITGmania, as well as the Simply Love theme, are both under the GPLv3 license, or at your option, any later version.

If ITGmania code is used in your project, we would also appreciate it if you link back to [ITGmania](https://github.com/itgmania/itgmania) as well as [StepMania](https://github.com/stepmania/stepmania).

For specific information/legalese:

* All of our source code is under the [GPLv3 license](https://www.gnu.org/licenses/gpl-3.0.en.html).
* Songs included within the 'StepMania 5' folder are under the [<abbr title="Creative Commons Non-Commercial">CC-NC</abbr> license](https://creativecommons.org/).
* Simply Love is licensed under the GPLv3, or, at your option, any later version.
* The copyright for songs in the 'Club Fantastic' folders rests with the original authors. The content is explicitly NOT placed under a Creative Commons license (or similar license), but has been provided free of charge, for personal or public use, including online broadcasting, tournaments, and other purposes. Go to the [Club Fantastic](https://www.clubfantastic.com/) website for more information.
* The [MAD library](http://www.underbit.com/products/mad/) and [FFmpeg codecs](https://www.ffmpeg.org/) when built with our code use the [GPL license](http://www.gnu.org).

## Credits

### ITGmania Team
- Martin Natano (natano)
- teejusb

### Contributors
- [Club Fantastic](https://wiki.clubfantastic.dance/en/Credits)
- [DinsFire64](https://gist.github.com/DinsFire64/4a3f763cd3033afd55a176980b32a3b5) (Mine Fix)
- [EvocaitArt](https://twitter.com/EvocaitArt) (Enchantment NoteSkin)
- [jenx](https://www.amarion.net/) (Fast Profile Switching)
- [LightningXCE](https://twitter.com/lightningxce) (Cyber NoteSkin)
- [MegaSphere](https://github.com/Pete-Lawrence/Peters-Noteskins) (Note/Rainbow/Vivid NoteSkins)
- [StepMania 5](Docs/credits_SM5.txt)
- [Old StepMania Team](Docs/credits_old_Stepmania_Team.txt)
