#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <iostream>
#include <string>

namespace unit_test {

extern int g_checks;
extern int g_failures;

void Check(bool condition, const char* file, int line, const char* expression);
void CheckStrEq(
    const std::string& actual, const std::string& expected,
    const char* actual_expression, const char* expected_expression,
    const char* file, int line);

template <typename Actual, typename Expected>
void CheckEq(
    const Actual& actual, const Expected& expected,
    const char* actual_expression, const char* expected_expression,
    const char* file, int line) {
  ++g_checks;
  if (actual == expected) {
    return;
  }

  ++g_failures;
  std::cerr << file << ':' << line << ": CHECK_EQ failed: " << actual_expression
            << " == " << expected_expression << " (actual: " << actual
            << ", expected: " << expected << ")\n";
}

int Finish();

}  // namespace unit_test

#define CHECK(condition) \
  ::unit_test::Check((condition), __FILE__, __LINE__, #condition)

#define CHECK_EQ(actual, expected) \
  ::unit_test::CheckEq(            \
      (actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_STR_EQ(actual, expected) \
  ::unit_test::CheckStrEq(             \
      (actual), (expected), #actual, #expected, __FILE__, __LINE__)

#endif
