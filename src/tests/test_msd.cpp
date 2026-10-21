#include <string>

#include "MsdFile.h"
#include "unit_test.h"

namespace {

MsdFile Read(const std::string& input, bool unescape = false) {
  MsdFile file;
  file.ReadFromString(input, unescape);
  return file;
}

void TestEmptyInputs() {
  MsdFile empty = Read("");
  CHECK_EQ(empty.GetNumValues(), 0u);

  MsdFile whitespace = Read(" \t\r\n  \t");
  CHECK_EQ(whitespace.GetNumValues(), 0u);

  MsdFile comments = Read("// first comment\n// second comment\n");
  CHECK_EQ(comments.GetNumValues(), 0u);
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

void TestEmptyParameters() {
  MsdFile middle_empty = Read("#A::B;");
  CHECK_EQ(middle_empty.GetNumValues(), 1u);
  CHECK_EQ(middle_empty.GetNumParams(0u), 3u);
  CHECK_STR_EQ(middle_empty.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(middle_empty.GetParam(0u, 1u), "");
  CHECK_STR_EQ(middle_empty.GetParam(0u, 2u), "B");

  MsdFile leading_empty = Read("#:A;");
  CHECK_EQ(leading_empty.GetNumValues(), 1u);
  CHECK_EQ(leading_empty.GetNumParams(0u), 2u);
  CHECK_STR_EQ(leading_empty.GetParam(0u, 0u), "");
  CHECK_STR_EQ(leading_empty.GetParam(0u, 1u), "A");

  MsdFile trailing_empty = Read("#A:;");
  CHECK_EQ(trailing_empty.GetNumValues(), 1u);
  CHECK_EQ(trailing_empty.GetNumParams(0u), 2u);
  CHECK_STR_EQ(trailing_empty.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(trailing_empty.GetParam(0u, 1u), "");

  MsdFile only_empty = Read("#;");
  CHECK_EQ(only_empty.GetNumValues(), 1u);
  CHECK_EQ(only_empty.GetNumParams(0u), 1u);
  CHECK_STR_EQ(only_empty.GetParam(0u, 0u), "");
}

void TestUnterminatedValues() {
  MsdFile no_semicolon = Read("#A:B");
  CHECK_EQ(no_semicolon.GetNumValues(), 1u);
  CHECK_EQ(no_semicolon.GetNumParams(0u), 2u);
  CHECK_STR_EQ(no_semicolon.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(no_semicolon.GetParam(0u, 1u), "B");

  MsdFile trailing_colon = Read("#A:");
  CHECK_EQ(trailing_colon.GetNumValues(), 1u);
  CHECK_EQ(trailing_colon.GetNumParams(0u), 2u);
  CHECK_STR_EQ(trailing_colon.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(trailing_colon.GetParam(0u, 1u), "");
}

void TestMissingSemicolonRecovery() {
  MsdFile lf = Read("#A:B\n#C:D");
  CHECK_EQ(lf.GetNumValues(), 2u);
  CHECK_EQ(lf.GetNumParams(0u), 2u);
  CHECK_EQ(lf.GetNumParams(1u), 2u);
  CHECK_STR_EQ(lf.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(lf.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(lf.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(lf.GetParam(1u, 1u), "D");

  MsdFile crlf = Read("#A:B\r\n#C:D;");
  CHECK_EQ(crlf.GetNumValues(), 2u);
  CHECK_EQ(crlf.GetNumParams(0u), 2u);
  CHECK_EQ(crlf.GetNumParams(1u), 2u);
  CHECK_STR_EQ(crlf.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(crlf.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(crlf.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(crlf.GetParam(1u, 1u), "D");

  MsdFile trailing_whitespace = Read("#A:B   \r\n\t#C:D;");
  CHECK_EQ(trailing_whitespace.GetNumValues(), 2u);
  CHECK_EQ(trailing_whitespace.GetNumParams(0u), 2u);
  CHECK_EQ(trailing_whitespace.GetNumParams(1u), 2u);
  CHECK_STR_EQ(trailing_whitespace.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(trailing_whitespace.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(trailing_whitespace.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(trailing_whitespace.GetParam(1u, 1u), "D");
}

void TestMidLineHash() {
  MsdFile spaced = Read("#A:B #C:D;");
  CHECK_EQ(spaced.GetNumValues(), 1u);
  CHECK_EQ(spaced.GetNumParams(0u), 3u);
  CHECK_STR_EQ(spaced.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(spaced.GetParam(0u, 1u), "B #C");
  CHECK_STR_EQ(spaced.GetParam(0u, 2u), "D");

  MsdFile unspaced = Read("#A:B#C:D;");
  CHECK_EQ(unspaced.GetNumValues(), 1u);
  CHECK_EQ(unspaced.GetNumParams(0u), 3u);
  CHECK_STR_EQ(unspaced.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(unspaced.GetParam(0u, 1u), "B#C");
  CHECK_STR_EQ(unspaced.GetParam(0u, 2u), "D");
}

void TestComments() {
  MsdFile outside = Read("#A:B; // outside\n#C:D;");
  CHECK_EQ(outside.GetNumValues(), 2u);
  CHECK_STR_EQ(outside.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(outside.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(outside.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(outside.GetParam(1u, 1u), "D");

  MsdFile inside = Read("#A:B// inside\nC:D;");
  CHECK_EQ(inside.GetNumValues(), 1u);
  CHECK_EQ(inside.GetNumParams(0u), 3u);
  CHECK_STR_EQ(inside.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(inside.GetParam(0u, 1u), "B\nC");
  CHECK_STR_EQ(inside.GetParam(0u, 2u), "D");

  MsdFile at_eof = Read("#A:B// comment at eof");
  CHECK_EQ(at_eof.GetNumValues(), 1u);
  CHECK_EQ(at_eof.GetNumParams(0u), 2u);
  CHECK_STR_EQ(at_eof.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(at_eof.GetParam(0u, 1u), "B");
}

void TestEscapingEnabled() {
  MsdFile colon = Read("#A\\:B:C;", true);
  CHECK_EQ(colon.GetNumValues(), 1u);
  CHECK_EQ(colon.GetNumParams(0u), 2u);
  CHECK_STR_EQ(colon.GetParam(0u, 0u), "A:B");
  CHECK_STR_EQ(colon.GetParam(0u, 1u), "C");

  MsdFile semicolon = Read("#A\\;B;", true);
  CHECK_EQ(semicolon.GetNumValues(), 1u);
  CHECK_EQ(semicolon.GetNumParams(0u), 1u);
  CHECK_STR_EQ(semicolon.GetParam(0u, 0u), "A;B");

  MsdFile hash = Read("#A\\#B;", true);
  CHECK_EQ(hash.GetNumValues(), 1u);
  CHECK_EQ(hash.GetNumParams(0u), 1u);
  CHECK_STR_EQ(hash.GetParam(0u, 0u), "A#B");

  MsdFile backslash = Read("#A\\\\B;", true);
  CHECK_EQ(backslash.GetNumValues(), 1u);
  CHECK_EQ(backslash.GetNumParams(0u), 1u);
  CHECK_STR_EQ(backslash.GetParam(0u, 0u), "A\\B");

  MsdFile trailing_backslash = Read("#A:B\\", true);
  CHECK_EQ(trailing_backslash.GetNumValues(), 1u);
  CHECK_EQ(trailing_backslash.GetNumParams(0u), 2u);
  CHECK_STR_EQ(trailing_backslash.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(trailing_backslash.GetParam(0u, 1u), "B");
}

void TestEscapingDisabled() {
  MsdFile colon = Read("#A\\:B:C;", false);
  CHECK_EQ(colon.GetNumValues(), 1u);
  CHECK_EQ(colon.GetNumParams(0u), 2u);
  CHECK_STR_EQ(colon.GetParam(0u, 0u), "A\\:B");
  CHECK_STR_EQ(colon.GetParam(0u, 1u), "C");

  MsdFile semicolon = Read("#A\\;B:C;", false);
  CHECK_EQ(semicolon.GetNumValues(), 1u);
  CHECK_EQ(semicolon.GetNumParams(0u), 2u);
  CHECK_STR_EQ(semicolon.GetParam(0u, 0u), "A\\;B");
  CHECK_STR_EQ(semicolon.GetParam(0u, 1u), "C");

  MsdFile backslash = Read("#A\\\\B;", false);
  CHECK_EQ(backslash.GetNumValues(), 1u);
  CHECK_EQ(backslash.GetNumParams(0u), 1u);
  CHECK_STR_EQ(backslash.GetParam(0u, 0u), "A\\\\B");

  MsdFile trailing_backslash = Read("#A:B\\", false);
  CHECK_EQ(trailing_backslash.GetNumValues(), 1u);
  CHECK_EQ(trailing_backslash.GetNumParams(0u), 2u);
  CHECK_STR_EQ(trailing_backslash.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(trailing_backslash.GetParam(0u, 1u), "B\\");
}

void TestCrlf() {
  MsdFile crlf = Read("#A:B;\r\n#C:D;\r\n");
  CHECK_EQ(crlf.GetNumValues(), 2u);
  CHECK_EQ(crlf.GetNumParams(0u), 2u);
  CHECK_EQ(crlf.GetNumParams(1u), 2u);
  CHECK_STR_EQ(crlf.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(crlf.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(crlf.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(crlf.GetParam(1u, 1u), "D");
}

void TestAccessorBounds() {
  MsdFile msd = Read("#A:B;");
  CHECK_EQ(msd.GetNumParams(0u), 2u);
  CHECK_EQ(msd.GetNumParams(1u), 0u);
  CHECK_STR_EQ(msd.GetParam(1u, 0u), "");
  CHECK_STR_EQ(msd.GetParam(0u, 2u), "");
  CHECK_STR_EQ(msd.GetValue(0u)[2u], "");

  MsdFile empty;
  CHECK_EQ(empty.GetNumParams(0u), 0u);
  CHECK_STR_EQ(empty.GetParam(0u, 0u), "");
}

void TestErrorState() {
  MsdFile fresh;
  CHECK_STR_EQ(fresh.GetError(), "");
}

void TestBinaryAndUtf8() {
  std::string with_nul = "#A:";
  with_nul.push_back('\0');
  with_nul += "B;";
  MsdFile nul = Read(with_nul);
  CHECK_EQ(nul.GetNumValues(), 1u);
  CHECK_EQ(nul.GetNumParams(0u), 2u);
  CHECK_STR_EQ(nul.GetParam(0u, 0u), "A");
  std::string expected_nul;
  expected_nul.push_back('\0');
  expected_nul += "B";
  CHECK_STR_EQ(nul.GetParam(0u, 1u), expected_nul);

  MsdFile utf8 = Read("#TITLE:caf\xc3\xa9 \xe2\x9c\x93;");
  CHECK_EQ(utf8.GetNumValues(), 1u);
  CHECK_EQ(utf8.GetNumParams(0u), 2u);
  CHECK_STR_EQ(utf8.GetParam(0u, 0u), "TITLE");
  CHECK_STR_EQ(utf8.GetParam(0u, 1u), "caf\xc3\xa9 \xe2\x9c\x93");
}

void TestRepeatedReadsAppend() {
  MsdFile msd;
  msd.ReadFromString("#A:B;", false);
  msd.ReadFromString("#C:D;", false);
  msd.ReadFromString("#E:F;", false);

  CHECK_EQ(msd.GetNumValues(), 3u);
  CHECK_STR_EQ(msd.GetParam(0u, 0u), "A");
  CHECK_STR_EQ(msd.GetParam(0u, 1u), "B");
  CHECK_STR_EQ(msd.GetParam(1u, 0u), "C");
  CHECK_STR_EQ(msd.GetParam(1u, 1u), "D");
  CHECK_STR_EQ(msd.GetParam(2u, 0u), "E");
  CHECK_STR_EQ(msd.GetParam(2u, 1u), "F");
}

}  // namespace

void RunMsdTests() {
  TestEmptyInputs();
  TestSimpleValues();
  TestEmptyParameters();
  TestUnterminatedValues();
  TestMissingSemicolonRecovery();
  TestMidLineHash();
  TestComments();
  TestEscapingEnabled();
  TestEscapingDisabled();
  TestCrlf();
  TestAccessorBounds();
  TestErrorState();
  TestBinaryAndUtf8();
  TestRepeatedReadsAppend();
}
