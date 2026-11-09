#ifndef STEPMANIA_VER_H
#define STEPMANIA_VER_H

extern const char* const kProductVersion;

// XXX: These names are misnomers. This is actually the BUILD number, time and
// date. NOTE: In GNU toolchain these are defined in ver.cpp. In MSVC these are
// defined in archutils/Win32/verinc.c I think. Why? I don't know and I don't
// have MSVC. --root
extern const unsigned long kVersionNum;
extern const char* const kVersionTime;
extern const char* const kVersionDate;
extern const char* const kSmVersionGitHash;

#endif
