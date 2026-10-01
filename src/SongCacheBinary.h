#ifndef SONG_CACHE_BINARY_H
#define SONG_CACHE_BINARY_H

#include <string>

class Song;

/**
 * @brief Binary song cache.
 *
 * The file layout is a fixed-order serialization.
 *
 * On failure (e.g. corrupt file) it returns false and leaves the song in an
 * unspecified state, so the caller must Reset() it.
 */
namespace SongCacheBinary {

bool Write(const Song& song, const std::string& path);
bool Read(Song& song, const std::string& path);

}  // namespace SongCacheBinary

#endif
