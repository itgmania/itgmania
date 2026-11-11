#ifndef RAGEUTIL_RANDOMNUMBERS_H
#define RAGEUTIL_RANDOMNUMBERS_H

#include <ctime>
#include <random>

class MersenneTwister : public std::mt19937 {
 public:
  MersenneTwister(int i_seed = 0)
      : std::mt19937(i_seed == 0 ? time(nullptr) : i_seed) {}
};

typedef MersenneTwister RandomGen;

extern RandomGen random_number_generator;

/**
 * @brief Return a float between the low and high values.
 * @param fLow the low value, inclusive.
 * @param fHigh the high value, inclusive.
 * @return the random float.
 */
inline float RandomFloat(float low, float high) {
  std::uniform_real_distribution<> dist(low, high);
  return dist(random_number_generator);
}

// Returns an integer between nLow and nHigh inclusive
inline int RandomInt(int low, int high) {
  std::uniform_int_distribution<> dist(low, high);
  return dist(random_number_generator);
}

// Returns an integer between 0 and n-1 inclusive (replacement for rand() % n).
inline int RandomInt(int n) { return RandomInt(0, n - 1); }

// Simple function for generating random numbers
inline float randomf(const float low = -1.0f, const float high = 1.0f) {
  return RandomFloat(low, high);
}

#endif