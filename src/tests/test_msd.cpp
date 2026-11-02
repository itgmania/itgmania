#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MsdFile.h"

namespace {

MsdFile Read(const std::string& input, bool unescape = false) {
  MsdFile file;
  file.ReadFromString(input, unescape);
  return file;
}

void ExpectValues(
    const MsdFile& file,
    const std::vector<std::vector<std::string>>& expected) {
  ASSERT_EQ(file.GetNumValues(), expected.size());
  for (unsigned i = 0; i < file.GetNumValues(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(file.GetNumParams(i), expected[i].size());
    EXPECT_EQ(file.GetValue(i).params, expected[i]);
  }
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

TEST(MsdFile, EmptyInput) {
  ExpectValues(Read(""), {});
  ExpectValues(Read("// only a comment"), {});
}

TEST(MsdFile, TextOutsideValuesIsIgnored) {
  ExpectValues(Read("junk\n#A:B;\nmore junk"), {{"A", "B"}});
  ExpectValues(Read(";#A:B;;#C:D;"), {{"A", "B"}, {"C", "D"}});
}

TEST(MsdFile, EmptyValuesAndParams) {
  ExpectValues(Read("#;"), {{""}});
  ExpectValues(Read("#"), {{""}});
  ExpectValues(Read("#:;"), {{"", ""}});
  ExpectValues(Read("#A::B;"), {{"A", "", "B"}});
}

TEST(MsdFile, MissingTrailingSemicolon) {
  ExpectValues(Read("#A:B"), {{"A", "B"}});
  ExpectValues(Read("#A"), {{"A"}});
  ExpectValues(Read("#A;"), {{"A"}});
}

TEST(MsdFile, SpecialCharactersAtEndOfInput) {
  ExpectValues(Read("#A:"), {{"A", ""}});
  ExpectValues(Read("#A:B:"), {{"A", "B", ""}});
  ExpectValues(Read("#A:B#"), {{"A", "B#"}});
}

TEST(MsdFile, MissingSemicolonBeforeNewValue) {
  ExpectValues(Read("#A:1\n#B:2;"), {{"A", "1"}, {"B", "2"}});
  ExpectValues(Read("#A:1\n\n#B:2;"), {{"A", "1"}, {"B", "2"}});
  ExpectValues(Read("#A:1\r\n  \t#B:2;"), {{"A", "1"}, {"B", "2"}});
}

TEST(MsdFile, HashInMiddleOfLineIsLiteral) {
  ExpectValues(Read("#A:1 #B;"), {{"A", "1 #B"}});
  ExpectValues(Read("#A:1 #B:2;"), {{"A", "1 #B", "2"}});
  ExpectValues(Read("#A:x#y;"), {{"A", "x#y"}});
}

TEST(MsdFile, EscapedHashAtLineStartIsLiteral) {
  ExpectValues(Read("#A:B\n\\#C;", true), {{"A", "B\n#C"}});
  ExpectValues(Read("#A:B\n\\#C;", false), {{"A", "B\n\\#C"}});
}

TEST(MsdFile, Comments) {
  ExpectValues(Read("// leading comment\n#A:B;"), {{"A", "B"}});
  ExpectValues(Read("// #A:B;\n#C:D;"), {{"C", "D"}});
  ExpectValues(
      Read("#A:B; // trailing comment\n#C:D;"), {{"A", "B"}, {"C", "D"}});
  ExpectValues(Read("#A:B// comment to EOF"), {{"A", "B"}});
}

TEST(MsdFile, CommentBeforeNewValueAfterMissingSemicolon) {
  ExpectValues(Read("#A:1\n// comment\n#B:2;"), {{"A", "1"}, {"B", "2"}});
}

TEST(MsdFile, CommentInsideValueIsStripped) {
  ExpectValues(
      Read("#NOTES:line1\n//comment\nline2;"), {{"NOTES", "line1\n\nline2"}});
}

TEST(MsdFile, SlashSlashInValueStartsComment) {
  ExpectValues(Read("#A:foo//bar;"), {{"A", "foo"}});
}

TEST(MsdFile, SlashesInsideValues) {
  ExpectValues(Read("#A:foo/bar;"), {{"A", "foo/bar"}});
  ExpectValues(Read("#A:foo/;"), {{"A", "foo/"}});
  ExpectValues(Read("#A:/;"), {{"A", "/"}});
  ExpectValues(Read("#A:foo/"), {{"A", "foo/"}});
}

TEST(MsdFile, EscapedSlashDoesNotStartComment) {
  ExpectValues(Read(R"(#A:a\//b;)", true), {{"A", "a//b"}});
  ExpectValues(Read(R"(#A:a\//b;)"), {{"A", R"(a\//b)"}});
}

TEST(MsdFile, EscapesArePreservedWhenNotUnescaping) {
  ExpectValues(Read(R"(#A:B\:C\;D\\E;)"), {{"A", R"(B\:C\;D\\E)"}});
}

TEST(MsdFile, EscapesAreUnescapedWhenRequested) {
  ExpectValues(Read(R"(#A:B\:C\;D\\E;)", true), {{"A", R"(B:C;D\E)"}});
}

TEST(MsdFile, EscapedSemicolonDoesNotTerminateValue) {
  ExpectValues(Read(R"(#A:x\;y;)", true), {{"A", "x;y"}});
  ExpectValues(Read(R"(#A:x\;y;)"), {{"A", R"(x\;y)"}});
}

TEST(MsdFile, NewlinesArePreservedInValues) {
  ExpectValues(Read("#A:line1\nline2;"), {{"A", "line1\nline2"}});
  ExpectValues(Read("#A:line1\r\nline2;"), {{"A", "line1\r\nline2"}});
}

TEST(MsdFile, TrailingBackslashAtEndOfInput) {
  ExpectValues(Read("#A:x\\", true), {{"A", "x"}});
  ExpectValues(Read("#A:x\\"), {{"A", "x\\"}});
}

TEST(MsdFile, OutOfRangeAccessors) {
  MsdFile file = Read("#A:B;");
  EXPECT_EQ(file.GetNumParams(1u), 0u);
  EXPECT_EQ(file.GetParam(0u, 2u), "");
  EXPECT_EQ(file.GetParam(1u, 0u), "");
  EXPECT_EQ(file.GetValue(0u)[2u], "");
}

TEST(MsdFile, ReadFileMissingFile) {
  MsdFile file;
  EXPECT_FALSE(file.ReadFile("/unit_test_msd/does_not_exist.msd", true));
  EXPECT_FALSE(file.GetError().empty());
}

}  // namespace
