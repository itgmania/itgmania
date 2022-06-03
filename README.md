ITGmania
========

ITGmania is a fork of [StepMania 5.1](https://github.com/stepmania/stepmania/tree/5_1-new), an advanced cross-platform rhythm game for home and arcade use.

[![Continuous integration](https://github.com/itgmania/itgmania/workflows/Continuous%20integration/badge.svg?branch=beta)](https://github.com/itgmania/itgmania/actions?query=workflow%3A%22Continuous+integration%22+branch%3Abeta)

## Changes to StepMania 5.1

- The mine fix
- Track held misses for pad debugging
- New preference to control note render ordering
- Fixed hold note rendering
- Increased the Stats.xml file size limit to 100MB
- Changed the default binding for P2/back from hyphen to backslash
- Built-in network functionality
- Per-player visual delay and disabled timing windows

## Installation
### From Packages

For those that do not wish to compile the game on their own and use a binary right away, be aware of the following issues:

* Windows 7 is the minimum supported version.
* macOS users need to have macOS 11 (Big Sur) or higher to run ITGmania.
* Linux users should receive all they need from the package manager of their choice.

### From Source

ITGmania can be compiled using [CMake](http://www.cmake.org/). More information about using CMake can be found in both the `Build` directory and CMake's documentation.

## Resources

* [ITGmania Website](https://www.itgmania.com/)
* [StepMania 5.1 to ITGmania Migration Guide](Docs/Userdocs/sm5_migration.md)
* [Lua for ITGmania](https://itgmania.github.io/lua-for-itgmania/)
* Lua API Documentation can be found in the Docs folder.

## Licensing Terms

In short- you can do anything you like with the game (including sell products made with it), provided you *do not*:

1. Sell the game *with the included songs*
2. Claim to have created the engine yourself or remove the credits
3. Not provide source code for any build which differs from any official release which includes MP3 support.

(It's not required, but we would also appreciate it if you link back to http://www.stepmania.com/)

For specific information/legalese:

* All of our source code is under the [MIT license](http://opensource.org/licenses/MIT).
* Any songs that are included within this repository are under the [<abbr title="Creative Commons Non-Commercial">CC-NC</abbr> license](https://creativecommons.org/).
* The [MAD library](http://www.underbit.com/products/mad/) and [FFmpeg codecs](https://www.ffmpeg.org/) when built with our code use the [GPL license](http://www.gnu.org).

## Credits

### ITGmania Team
- Martin Natano (natano)
- teejusb

### Contributors
- [Club Fantastic](https://wiki.clubfantastic.dance/en/Credits)
- EvocaitArt (Enchantment noteskin)
- LightningXCE (Cyber noteskin)
- MegaSphere (Note/Rainbow/Vivid noteskins)
- [Stepmania 5](Docs/credits_SM5.txt)
- [Old StepMania Team](Docs/credits_old_Stepmania_Team.txt)
