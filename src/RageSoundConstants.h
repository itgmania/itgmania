#ifndef RAGE_SOUND_CONSTANTS_H
#define RAGE_SOUND_CONSTANTS_H

// Fallback sample rate used when no other sample rate is available.
// I use a macro here to ensure the constant does not affect
// how the legacy RageSound code is compiled. Users notice enough to
// complain when a constexpr / const int is used here. I haven't yet
// found the root of that issue within RageSound.
#define FALLBACK_SAMPLE_RATE 44100

#endif // RAGE_SOUND_CONSTANTS_H
