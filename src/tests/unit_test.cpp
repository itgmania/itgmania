#include "unit_test.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace unit_test {

int g_checks = 0;
int g_failures = 0;

namespace {

std::string Escape(const std::string& value) {
  std::ostringstream result;
  for (unsigned char c : value) {
    switch (c) {
      case '\\':
        result << "\\\\";
        break;
      case '\n':
        result << "\\n";
        break;
      case '\r':
        result << "\\r";
        break;
      case '\t':
        result << "\\t";
        break;
      case '\0':
        result << "\\0";
        break;
      default:
        if (c < 0x20 || c == 0x7f) {
          result << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<int>(c) << std::dec;
        } else {
          result << c;
        }
        break;
    }
  }
  return result.str();
}

}  // namespace

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
            << " == " << expected_expression << " (actual: \"" << Escape(actual)
            << "\", expected: \"" << Escape(expected) << "\")\n";
}

int Finish() {
  std::cout << g_checks << " checks, " << g_failures << " failures\n";
  return g_failures == 0 ? 0 : 1;
}

}  // namespace unit_test
