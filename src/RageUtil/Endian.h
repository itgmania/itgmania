#ifndef RAGEUTIL_ENDIAN_H
#define RAGEUTIL_ENDIAN_H

#include <cstdint>

namespace Endian {
// When std::endian is supported by all desired compilers, we can eliminate the
// #ifdefs (At least) the current compiler used for the Ubuntu 20.04 build does
// not support this.
#if defined(__BYTE_ORDER__)
inline constexpr bool kLittle = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
inline constexpr bool kBig = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__;
#elif defined(_WIN32)
inline constexpr bool kLittle = true;
inline constexpr bool kBig = false;
#else
#error "unknown byte order"
#endif
}  // namespace Endian

#ifdef _WIN32
#define Swap32(n) _byteswap_ulong(n)
#define Swap24(n) _byteswap_ulong(n) >> 8
#define Swap16(n) _byteswap_ushort(n)
#else
#define Swap32(n) __builtin_bswap32(n)
#define Swap24(n) __builtin_bswap32(n) >> 8
#define Swap16(n) __builtin_bswap16(n)
#endif

inline uint32_t Swap32LE(uint32_t n) { return Endian::kLittle ? n : Swap32(n); }
inline uint32_t Swap24LE(uint32_t n) { return Endian::kLittle ? n : Swap24(n); }
inline uint16_t Swap16LE(uint16_t n) { return Endian::kLittle ? n : Swap16(n); }
inline uint32_t Swap32BE(uint32_t n) { return Endian::kBig ? n : Swap32(n); }
inline uint32_t Swap24BE(uint32_t n) { return Endian::kBig ? n : Swap24(n); }
inline uint16_t Swap16BE(uint16_t n) { return Endian::kBig ? n : Swap16(n); }

#endif