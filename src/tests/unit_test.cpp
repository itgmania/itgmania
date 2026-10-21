#include "unit_test.h"

#include <iostream>
#include <string>

namespace unit_test {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* file, int line, const char* expression) {
  ++g_checks;
  if (condition) {
    return;
  }

  ++g_failures;
  std::cerr << file << ':' << line << ": CHECK failed: " << expression << '\n';
}

void CheckStrEq(
    const std::string& actual, const std::string& expected,
    const char* actual_expression, const char* expected_expression,
    const char* file, int line) {
  ++g_checks;
  if (actual == expected) {
    return;
  }

  ++g_failures;
  std::cerr << file << ':' << line
            << ": CHECK_STR_EQ failed: " << actual_expression
            << " == " << expected_expression << " (actual: \"" << actual
            << "\", expected: \"" << expected << "\")\n";
}

int Finish() {
  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}

}  // namespace unit_test
