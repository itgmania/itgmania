ITGmania
========

ITGmania is a fork of [StepMania 5.1](https://github.com/stepmania/stepmania/tree/5_1-new), an advanced cross-platform rhythm game for home and arcade use.

[![Continuous integration](https://github.com/itgmania/itgmania/workflows/Continuous%20integration/badge.svg?branch=beta)](https://github.com/itgmania/itgmania/actions?query=workflow%3A%22Continuous+integration%22+branch%3Abeta)

## Changes to StepMania 5.1

- Built-in network functionality
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

(It's not required, but we would also appreciate it if you link back to [ITGmania](https://github.com/itgmania/itgmania) as well as [StepMania](https://github.com/stepmania/stepmania).)

For specific information/legalese:

* All of our source code is under the [MIT license](http://opensource.org/licenses/MIT).
* Songs included within the 'StepMania 5' folder are under the [<abbr title="Creative Commons Non-Commercial">CC-NC</abbr> license](https://creativecommons.org/).
* Simply Love is licensed under the GPLv3, or (at your option) any later version.
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
