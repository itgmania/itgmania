ITGmania
========

ITGmania is a fork of [StepMania 5.1](https://github.com/stepmania/stepmania/tree/5_1-new), an advanced cross-platform rhythm game for home and arcade use.

[![Continuous integration](https://github.com/itgmania/itgmania/workflows/Continuous%20integration/badge.svg?branch=beta)](https://github.com/itgmania/itgmania/actions?query=workflow%3A%22Continuous+integration%22+branch%3Abeta)

[![Build Status](https://travis-ci.org/stepmania/stepmania.svg?branch=master)](https://travis-ci.org/stepmania/stepmania)
[![Build status](https://ci.appveyor.com/api/projects/status/uvoplsnyoats81r2?svg=true)](https://ci.appveyor.com/project/Nickito12/stepmania)

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

* Windows users are expected to have installed the [Microsoft Visual C++ x86 Redistributable for Visual Studio 2015](http://www.microsoft.com/en-us/download/details.aspx?id=48145) prior to running the game. For those on a 64-bit operating system, grab the x64 redistributable as well. Windows 7 is the minimum supported version.
* Mac OS X users need to have Mac OS X 10.6.8 or higher to run ITGmania.
* Linux users should receive all they need from the package manager of their choice.

### From Source

ITGmania can be compiled using [CMake](http://www.cmake.org/). More information about using CMake can be found in both the `Build` directory and CMake's documentation.

## Resources

* Website: https://www.itgmania.com/
* Lua for ITGmania: https://itgmania.github.io/lua-for-itgmania/
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
