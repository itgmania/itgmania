#include <gtest/gtest.h>

#include <string>

#include "MsdFile.h"

namespace {

MsdFile Read(const std::string& input, bool unescape = false) {
  MsdFile file;
  file.ReadFromString(input, unescape);
  return file;
}

TEST(MsdFile, SimpleValues) {
  MsdFile single = Read("#TITLE:Test;");
  ASSERT_EQ(single.GetNumValues(), 1u);
  ASSERT_EQ(single.GetNumParams(0u), 2u);
  EXPECT_EQ(single.GetParam(0u, 0u), "TITLE");
  EXPECT_EQ(single.GetParam(0u, 1u), "Test");
  EXPECT_EQ(single.GetValue(0u)[0u], "TITLE");
  EXPECT_EQ(single.GetValue(0u)[1u], "Test");
}

TEST(MsdFile, MultipleValues) {
  MsdFile multiple = Read("#A;#B;");
  ASSERT_EQ(multiple.GetNumValues(), 2u);
  ASSERT_EQ(multiple.GetNumParams(0u), 1u);
  ASSERT_EQ(multiple.GetNumParams(1u), 1u);
  EXPECT_EQ(multiple.GetParam(0u, 0u), "A");
  EXPECT_EQ(multiple.GetParam(1u, 0u), "B");
}

TEST(MsdFile, MultipleParams) {
  MsdFile params = Read("#A:B;#C:D;");
  ASSERT_EQ(params.GetNumValues(), 2u);
  ASSERT_EQ(params.GetNumParams(0u), 2u);
  ASSERT_EQ(params.GetNumParams(1u), 2u);
  EXPECT_EQ(params.GetParam(0u, 0u), "A");
  EXPECT_EQ(params.GetParam(0u, 1u), "B");
  EXPECT_EQ(params.GetParam(1u, 0u), "C");
  EXPECT_EQ(params.GetParam(1u, 1u), "D");
}

}  // namespace
