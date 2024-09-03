#ifndef CONVENIENT_VALUES_CONSTEXPRS
#define CONVENIENT_VALUES_CONSTEXPRS

// constexpr values to improve possible compiler optimization
constexpr float NEGATIVE_ONE = -1.0f;
constexpr float NEGATIVE_POINT_ONE = -0.1f;
constexpr float ZERO = 0.0f;
constexpr float POINT_ZERO_ONE = 0.01f;
constexpr float POINT_ONE = 0.1f;
constexpr float ONE_QUARTER = 0.25f;
constexpr float ONE_HALF = 0.5f;
constexpr float THREE_QUARTERS = 0.75f;
constexpr float ONE = 1.0f;
constexpr float TWO = 2.0f;
constexpr float THREE = 3.0f;
constexpr float FOUR = 4.0f;
constexpr float FIVE = 5.0f;
constexpr float EIGHT = 8.0f;

// PI is defined via RageMath.h as 3.1415926536f, but values based on it are here
constexpr float TWO_PI_OVER_THREE = 3.1415926536f * 2.0f / 3.0f;
constexpr float FOUR_PI_OVER_THREE = 3.1415926536f * 4.0f / 3.0f;
constexpr float TWO_PI = 2.0f * 3.1415926536f;
constexpr double PI_180 = 3.1415926536 / 180.0;
constexpr double PI_180R = 180.0 / 3.1415926536;

// note types
constexpr float _NOTE_TYPE_4TH = 1.0f;      // quarter notes
constexpr float _NOTE_TYPE_8TH = 1.0f/2;    // eighth notes
constexpr float _NOTE_TYPE_12TH = 1.0f/3;   // quarter note triplets
constexpr float _NOTE_TYPE_16TH = 1.0f/4;   // sixteenth notes
constexpr float _NOTE_TYPE_24TH = 1.0f/6;   // eighth note triplets
constexpr float _NOTE_TYPE_32ND = 1.0f/8;   // thirty-second notes
constexpr float _NOTE_TYPE_48TH = 1.0f/12;  // sixteenth note triplets
constexpr float _NOTE_TYPE_64TH = 1.0f/16;  // sixty-fourth notes
constexpr float _NOTE_TYPE_192ND = 1.0f/48; // sixty-fourth note triplets
constexpr float _NOTE_TYPE_INVALID = 1.0f/48;   // invalid note type

#endif
