#include <string>

#include "MsdFile.h"
#include "unit_test.h"

namespace {

MsdFile Read(const std::string& input, bool unescape = false) {
  MsdFile file;
  file.ReadFromString(input, unescape);
  return file;
}

void TestSimpleValues() {
  MsdFile single = Read("#TITLE:Test;");
  CHECK_EQ(single.GetNumValues(), 1u);
  CHECK_EQ(single.GetNumParams(0u), 2u);
  CHECK_STR_EQ(single.GetParam(0u, 0u), "TITLE");
  CHECK_STR_EQ(single.GetParam(0u, 1u), "Test");
  CHECK_STR_EQ(single.GetValue(0u)[0u], "TITLE");
  CHECK_STR_EQ(single.GetValue(0u)[1u], "Test");

  MsdFile multiple = Read("#A;#B;");
  CHECK_EQ(multiple.GetNumValues(), 2u);
  CHECK_EQ(multiple.GetNumParams(0u), 1u);
  CHECK_EQ(multiple.GetNumParams(1u), 1u);
  CHECK_STR_EQ(multiple.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(multiple.GetParam(1u, 0u), "B");

  MsdFile params = Read("#A:B;#C:D;");
  CHECK_EQ(params.GetNumValues(), 2u);
  CHECK_EQ(params.GetNumParams(0u), 2u);
  CHECK_EQ(params.GetNumParams(1u), 2u);
  CHECK_STR_EQ(params.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(params.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(params.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(params.GetParam(1u, 1u), "D");
}

}  // namespace

void RunMsdTests() { TestSimpleValues(); }
