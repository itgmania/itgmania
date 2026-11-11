#include "XmlFileUtil.h"

#include <cctype>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "LuaManager.h"
#include "RageFile.h"
#include "RageFileDriverMemory.h"
#include "RageUtil.h"
#include "XmlFile.h"
#include "global.h"

bool XmlFileUtil::LoadFromFileShowErrors(XNode& xml, RageFileBasic& f) {
  std::string error;
  std::string s;
  if (f.Read(s) == -1) {
    error = f.GetError();
  } else {
    Load(&xml, s, error);
  }
  if (error.empty()) {
    return true;
  }

  std::string warning = ssprintf("XML: LoadFromFile failed: %s", error.c_str());
  LuaHelpers::ReportScriptError(warning, "XML_PARSE_ERROR");
  return false;
}

bool XmlFileUtil::LoadFromFileShowErrors(XNode& xml, const std::string& file) {
  RageFile f;
  if (!f.Open(file, RageFile::READ)) {
    LuaHelpers::ReportScriptErrorFmt(
        "Couldn't open %s for reading: %s", file.c_str(), f.GetError().c_str());
    return false;
  }

  bool success = LoadFromFileShowErrors(xml, f);
  if (!success) {
    std::string warning =
        ssprintf("XML: LoadFromFile failed for file: %s", file.c_str());
    LuaHelpers::ReportScriptError(warning, "XML_PARSE_ERROR");
  }
  return success;
}

static const char kXmlTagOpen = '<';
static const char kXmlTagClose = '>';
static const char kXmlQuestion =
    '?';  // used in checking for meta tags: "<?TAG ... ?/>"
static const char kXmlTagPre = '/';
static const char kXmlExclamation = '!';
static const char kXmlDash = '-';

static std::map<std::string, std::string> map_entities_to_chars;
static std::map<char, std::string> map_chars_to_entities;

static void InitEntities() {
  if (!map_entities_to_chars.empty()) {
    return;
  }

  static struct Entity {
    char c_;
    const char* entity_;
  } const kEntityTable[] = {
      {
          '&',
          "amp",
      },
      {
          '\"',
          "quot",
      },
      {
          '\'',
          "apos",
      },
      {
          '<',
          "lt",
      },
      {
          '>',
          "gt",
      }};

  for (unsigned i = 0; i < ARRAYLEN(kEntityTable); ++i) {
    const Entity& ent = kEntityTable[i];
    map_entities_to_chars[ent.entity_] = std::string(1, ent.c_);
    map_chars_to_entities[ent.c_] = ent.entity_;
  }
}

// skip spaces
static void tcsskip(const std::string& s, std::string::size_type& i) {
  i = s.find_first_not_of(" \t\r\n", i);
}

// put string of (psz~end) on ps string
static void SetString(
    const std::string& s, int start, int end, std::string* ps,
    bool trim = false) {
  if (trim) {
    while (start < end && s[start] > 0 && isspace(s[start])) {
      start++;
    }
    while (end - 1 >= start && s[end - 1] > 0 && isspace(s[end - 1])) {
      end--;
    }
  }

  int len = end - start;
  if (len <= 0) {
    return;
  }

  ps->assign(s, start, len);
}

// attr1="value1" attr2='value2' attr3=value3 />
//                                            ^- return pointer
// Desc   : loading attribute plain xml text
// Param  : pszAttrs - xml of attributes
//          pi = parser information
// Return : advanced string pointer. (error return npos)
namespace {
std::string::size_type LoadAttributes(
    XNode* node, const std::string& xml, std::string& error_out,
    std::string::size_type offset) {
  while (offset < xml.size()) {
    tcsskip(xml, offset);
    if (offset >= xml.size()) {
      continue;
    }

    // close tag
    if (offset < xml.size() &&
        (xml[offset] == kXmlTagClose || xml[offset] == kXmlTagPre ||
         xml[offset] == kXmlQuestion || xml[offset] == kXmlDash)) {
      return offset;  // well-formed tag
    }

    // XML Attr Name
    std::string::size_type end = xml.find_first_of(" =", offset);
    if (end == xml.npos) {
      // error
      if (error_out.empty()) {
        error_out =
            ssprintf("<%s> attribute has error ", node->GetName().c_str());
      }
      return std::string::npos;
    }

    // XML Attr Name
    std::string name;
    SetString(xml, offset, end, &name);

    // add new attribute
    DEBUG_ASSERT(name.size());
    XNodeValue* attr = node->AppendAttr(name);
    offset = end;

    // XML Attr Value
    tcsskip(xml, offset);

    if (offset < xml.size() && xml[offset] == '=') {
      ++offset;

      tcsskip(xml, offset);
      if (offset >= xml.size()) {
        continue;
      }

      // if " or '
      // or none quote
      char quote = xml[offset];
      if (quote == '"' || quote == '\'') {
        ++offset;
        end = xml.find(quote, offset);
      } else {
        // XXX: This is invalid XML, should we be accepting it? -- Steve
        // attr= value>
        // none quote mode
        end = xml.find_first_of(" >", offset);
      }

      if (end == xml.npos) {
        // error
        if (error_out.empty()) {
          error_out = ssprintf(
              "<%s> attribute text: couldn't find matching quote",
              name.c_str());
        }
        return std::string::npos;
      }

      std::string value;
      SetString(xml, offset, end, &value, true);
      ReplaceEntityText(value, map_entities_to_chars);
      attr->SetValue(value);
      offset = end;

      if (quote == '"' || quote == '\'') {
        ++offset;
      }
    }
  }

  // not well-formed tag
  return std::string::npos;
}

// <TAG attr1="value1" attr2='value2' attr3=value3 >
// </TAG>
// or
// <TAG />
//        ^- return pointer
// Desc   : load xml plain text
// Param  : pszXml - plain xml text
//          pi = parser information
// Return : advanced string pointer  (error return npos)
std::string::size_type LoadInternal(
    XNode* p_node, const std::string& xml, std::string& error_out,
    std::string::size_type offset) {
  p_node->Clear();

  // <
  offset = xml.find(kXmlTagOpen, offset);
  if (offset == std::string::npos) {
    return std::string::npos;
  }

  // </
  if (xml[offset + 1] == kXmlTagPre) {
    return offset;
  }

  // <!--
  if (!xml.compare(offset + 1, 3, "!--")) {
    offset += 4;

    // Find the close tag.
    std::string::size_type end = xml.find("-->", offset);
    if (end == std::string::npos) {
      if (error_out.empty()) {
        error_out = "Unterminated comment";
      }

      return std::string::npos;
    }

    // Skip -->.
    offset = end + 3;

    return LoadInternal(p_node, xml, error_out, offset);
  }

  // XML Node Tag Name Open
  offset++;
  std::string::size_type tag_end = xml.find_first_of(" \t\r\n/>", offset);
  std::string name;
  SetString(xml, offset, tag_end, &name);
  p_node->SetName(name);
  offset = tag_end;

  // Generate XML Attribute List
  offset = LoadAttributes(p_node, xml, error_out, offset);
  if (offset == std::string::npos) {
    return std::string::npos;
  }

  // alone tag <TAG ... /> or <?TAG ... ?> or <!-- ... -->
  // current pointer:   ^               ^              ^

  if (offset < xml.size() &&
      (xml[offset] == kXmlTagPre || xml[offset] == kXmlQuestion ||
       xml[offset] == kXmlDash)) {
    offset++;

    // skip over 2nd dash
    if (offset < xml.size() && xml[offset] == kXmlDash) {
      offset++;
    }

    if (offset == xml.size() || xml[offset] != kXmlTagClose) {
      // error: <TAG ... / >
      if (error_out.empty()) {
        error_out = "Element must be closed.";
      }

      // ill-formed tag
      return std::string::npos;
    }

    // well-formed tag
    ++offset;

    // UGLY: We want to ignore all XML meta tags.  So, since the Node we
    // just loaded is a meta tag, then Load ourself again using the rest
    // of the file until we reach a non-meta tag.
    if (!p_node->GetName().empty() &&
        (p_node->GetName()[0] == kXmlQuestion ||
         p_node->GetName()[0] == kXmlExclamation)) {
      offset = LoadInternal(p_node, xml, error_out, offset);
    }

    return offset;
  }

  // open/close tag <TAG ..> ... </TAG>
  //                        ^- current pointer
  if (p_node->GetAttr(XNode::kTextAttribute) == nullptr) {
    // Text Value
    ++offset;
    std::string::size_type end = xml.find(kXmlTagOpen, offset);
    if (end == std::string::npos) {
      if (error_out.empty()) {
        error_out = ssprintf(
            "%s must be closed with </%s>", p_node->GetName().c_str(),
            p_node->GetName().c_str());
      }
      // error cos not exist CloseTag </TAG>
      return std::string::npos;
    }

    std::string value;
    SetString(xml, offset, end, &value, true);

    offset = end;
    ReplaceEntityText(value, map_entities_to_chars);

    p_node->AppendAttr(XNode::kTextAttribute, value);
  }

  // generate child nodes
  while (offset < xml.size()) {
    XNode* node = new XNode;

    offset = LoadInternal(node, xml, error_out, offset);
    if (offset == std::string::npos) {
      delete node;
      return offset;
    }

    if (!node->GetName().empty()) {
      p_node->AppendChild(node);
    } else {
      delete node;
    }

    // open/close tag <TAG ..> ... </TAG>
    //                             ^- current pointer
    // CloseTag case
    if (offset + 1 < xml.size() && xml[offset] == kXmlTagOpen &&
        xml[offset + 1] == kXmlTagPre) {
      // </Close>
      offset += 2;  // C

      tcsskip(xml, offset);
      if (offset >= xml.size()) {
        continue;
      }

      std::string::size_type end = xml.find_first_of(" >", offset);
      if (end == std::string::npos) {
        if (error_out.empty()) {
          error_out = ssprintf(
              "it must be closed with </%s>", p_node->GetName().c_str());
        }
        // error
        return std::string::npos;
      }

      std::string closename;
      SetString(xml, offset, end, &closename);
      offset = end + 1;
      if (closename == p_node->GetName()) {
        // wel-formed open/close
        // return '>' or ' ' after pointer
        return offset;
      } else {
        // not welformed open/close
        if (error_out.empty()) {
          error_out = ssprintf(
              "'<%s> ... </%s>' is not well-formed.", p_node->GetName().c_str(),
              closename.c_str());
        }
        return std::string::npos;
      }
    } else  // Alone child Tag Loaded
    {
      if (p_node->GetAttr(XNode::kTextAttribute) == nullptr &&
          offset < xml.size() && xml[offset] != kXmlTagOpen) {
        // Text Value
        std::string::size_type end = xml.find(kXmlTagOpen, offset);
        if (end == std::string::npos) {
          // error cos not exist CloseTag </TAG>
          if (error_out.empty()) {
            error_out = ssprintf(
                "it must be closed with </%s>", p_node->GetName().c_str());
          }
          return std::string::npos;
        }

        std::string value;
        SetString(xml, offset, end, &value, true);

        offset = end;
        ReplaceEntityText(value, map_entities_to_chars);
        p_node->AppendAttr(XNode::kTextAttribute, value);
      }
    }
  }

  return offset;
}

bool GetXMLInternal(
    const XNode* node, RageFileBasic& f, bool write_tabs, int& tab_base) {
#define WRITE(x) \
  if (f.Write(x) == -1) return false
  // tab
  WRITE("\r\n");
  if (write_tabs) {
    for (int i = 0; i < tab_base; i++) {
      WRITE("\t");
    }
  }

  // <TAG
  WRITE("<");
  WRITE(node->GetName());

  // <TAG Attr1="Val1"
  FOREACH_CONST_Attr(node, p) {
    if (p->first == XNode::kTextAttribute) {
      continue;
    }
    std::string attr(p->second->GetValue<std::string>());
    ReplaceEntityText(attr, map_chars_to_entities);
    WRITE(" ");
    WRITE(p->first);
    WRITE("='");
    WRITE(attr);
    WRITE("'");
  }

  if (node->ChildrenEmpty() &&
      node->GetAttr(XNode::kTextAttribute) == nullptr) {
    // <TAG Attr1="Val1"/> alone tag
    WRITE("/>");
  } else {
    // <TAG Attr1="Val1"> and get child
    WRITE(">");

    if (!node->ChildrenEmpty()) {
      tab_base++;
    }

    FOREACH_CONST_Child(
        node, p) if (!GetXMLInternal(p, f, write_tabs, tab_base)) return false;

    // Text Value
    const XNodeValue* text = node->GetAttr(XNode::kTextAttribute);
    if (text != nullptr) {
      if (!node->ChildrenEmpty()) {
        WRITE("\r\n");
        if (write_tabs) {
          for (int i = 0; i < tab_base; i++) {
            WRITE("\t");
          }
        }
      }
      std::string s;
      text->GetValue(s);
      ReplaceEntityText(s, map_chars_to_entities);
      WRITE(s);
    }

    // </TAG> CloseTag
    if (!node->ChildrenEmpty()) {
      WRITE("\r\n");
      if (write_tabs) {
        for (int i = 0; i < tab_base - 1; i++) {
          WRITE("\t");
        }
      }
    }
    WRITE("</");
    WRITE(node->GetName());
    WRITE(">");

    if (!node->ChildrenEmpty()) {
      tab_base--;
    }
  }
  return true;
#undef WRITE
}
}  // namespace

void XmlFileUtil::Load(
    XNode* node, const std::string& xml, std::string& error_out) {
  InitEntities();
  LoadInternal(node, xml, error_out, 0);
}

std::string XmlFileUtil::GetXML(const XNode* node) {
  RageFileObjMem f;
  int tab_base = 0;
  InitEntities();
  GetXMLInternal(node, f, true, tab_base);
  return f.GetString();
}

bool XmlFileUtil::SaveToFile(
    const XNode* node, RageFileBasic& f, const std::string& stylesheet,
    bool write_tabs) {
  f.PutLine("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>");
  if (!stylesheet.empty()) {
    f.PutLine(
        "<?xml-stylesheet type=\"text/xsl\" href=\"" + stylesheet + "\"?>");
  }
  int tab_base = 0;
  InitEntities();
  if (!GetXMLInternal(node, f, write_tabs, tab_base)) {
    return false;
  }
  f.PutLine("");
  if (f.Flush() == -1) {
    return false;
  }
  return true;
}

bool XmlFileUtil::SaveToFile(
    const XNode* node, const std::string& file, const std::string& stylesheet,
    bool write_tabs) {
  RageFile f;
  if (!f.Open(file, RageFile::WRITE)) {
    LuaHelpers::ReportScriptErrorFmt(
        "Couldn't open %s for writing: %s", file.c_str(), f.GetError().c_str());
    return false;
  }

  return SaveToFile(node, f, stylesheet, write_tabs);
}

#include "LuaReference.h"
class XNodeLuaValue : public XNodeValue {
 public:
  LuaReference value_;
  XNodeValue* Copy() const { return new XNodeLuaValue(*this); }

  template <typename T>
  T GetValue() const {
    T val;
    GetValue(val);
    return val;
  }

  void GetValue(std::string& out) const;
  void GetValue(int& out) const;
  void GetValue(float& out) const;
  void GetValue(bool& out) const;
  void GetValue(unsigned& out) const;
  void PushValue(lua_State* L) const;

  void SetValue(const std::string& v);
  void SetValue(int v);
  void SetValue(float v);
  void SetValue(unsigned v);
  void SetValueFromStack(lua_State* L);
};

void XNodeLuaValue::PushValue(lua_State* L) const { value_.PushSelf(L); }

void XNodeLuaValue::GetValue(std::string& out) const {
  Lua* L = LUA->Get();
  PushValue(L);
  LuaHelpers::Pop(L, out);
  LUA->Release(L);
}
void XNodeLuaValue::GetValue(int& out) const {
  Lua* L = LUA->Get();
  PushValue(L);
  LuaHelpers::Pop(L, out);
  LUA->Release(L);
}
void XNodeLuaValue::GetValue(float& out) const {
  Lua* L = LUA->Get();
  PushValue(L);
  LuaHelpers::Pop(L, out);
  LUA->Release(L);
}
void XNodeLuaValue::GetValue(bool& out) const {
  Lua* L = LUA->Get();
  PushValue(L);
  LuaHelpers::Pop(L, out);
  LUA->Release(L);
}
void XNodeLuaValue::GetValue(unsigned& out) const {
  Lua* L = LUA->Get();
  PushValue(L);
  float val;
  LuaHelpers::Pop(L, val);
  out = unsigned(val);
  LUA->Release(L);
}

void XNodeLuaValue::SetValueFromStack(lua_State* L) { value_.SetFromStack(L); }

void XNodeLuaValue::SetValue(const std::string& v) {
  Lua* L = LUA->Get();
  LuaHelpers::Push(L, v);
  SetValueFromStack(L);
  LUA->Release(L);
}
void XNodeLuaValue::SetValue(int v) {
  Lua* L = LUA->Get();
  LuaHelpers::Push(L, v);
  SetValueFromStack(L);
  LUA->Release(L);
}
void XNodeLuaValue::SetValue(float v) {
  Lua* L = LUA->Get();
  LuaHelpers::Push(L, v);
  SetValueFromStack(L);
  LUA->Release(L);
}
void XNodeLuaValue::SetValue(unsigned v) {
  Lua* L = LUA->Get();
  LuaHelpers::Push(L, (float)v);
  SetValueFromStack(L);
  LUA->Release(L);
}

namespace {
XNodeValue* CompileXMLNodeValue(
    Lua* L, const std::string& name, const XNodeValue* value,
    const std::string& file) {
  std::string expression;
  value->GetValue(expression);

  if (EndsWith(name, "Command")) {
    // Use legacy parsing
    LuaHelpers::ParseCommandList(L, expression, file, true);
  } else if (expression.size() > 0 && expression[0] == '@') {
    // Lua expression
    expression.erase(0, 1);
    LuaHelpers::RunExpression(L, expression, file);
  } else {
    // This is a raw string.
    LuaHelpers::Push(L, expression);
  }

  XNodeLuaValue* ret = new XNodeLuaValue;
  ret->SetValueFromStack(L);
  return ret;
}
}  // namespace

void XmlFileUtil::AnnotateXNodeTree(XNode* node, const std::string& file) {
  std::string dir = Dirname(file);

  std::vector<XNode*> queue;
  queue.push_back(node);
  while (!queue.empty()) {
    node = queue.back();
    queue.pop_back();
    FOREACH_Child(node, child) queue.push_back(child);

    /* Source file, for error messages: */
    node->AppendAttr("_Source", file);

    /* Directory of caller, for relative paths: */
    node->AppendAttr("_Dir", dir);

    /* Note that this node came from a legacy XML file */
    node->AppendAttr("_LegacyXml", true);
  }
}

void XmlFileUtil::CompileXNodeTree(XNode* node, const std::string& file) {
  std::vector<XNode*> to_compile;
  to_compile.push_back(node);

  Lua* L = LUA->Get();
  while (to_compile.size()) {
    node = to_compile.back();
    to_compile.pop_back();
    FOREACH_Child(node, child) to_compile.push_back(child);

    FOREACH_Attr(node, attr) {
      XNodeValue* value =
          CompileXMLNodeValue(L, attr->first, attr->second, file);
      delete attr->second;
      attr->second = value;
    }
  }

  LUA->Release(L);
}

namespace {
XNode* XNodeFromTableRecursive(
    lua_State* L, const std::string& s_name, LuaReference& processed_tables) {
  XNode* node = new XNode(s_name);

  // Set the value of the node to the table.
  {
    XNodeLuaValue* value = new XNodeLuaValue;
    lua_pushvalue(L, -1);
    value->SetValueFromStack(L);
    node->AppendAttrFrom(XNode::kTextAttribute, value);
  }

  // Iterate over the table, pulling out attributes and tables to process.
  std::vector<std::string> node_names_to_add;
  std::vector<LuaReference> nodes_to_add;

  /* Add array elements first, in array order, so iterating over the XNode
   * keeps the array in order. */
  FOREACH_LUATABLEI(L, -1, i) {
    if (!lua_istable(L, -1)) {
      continue;
    }
    node_names_to_add.push_back(ssprintf("%i", i));
    nodes_to_add.push_back(LuaReference());
    nodes_to_add.back().SetFromStack(L);
  }

  int len = node_names_to_add.size();
  FOREACH_LUATABLE(L, -1) {
    // If this entry is a table, add it recursively.
    if (lua_istable(L, -2)) {
      if (lua_isnumber(L, -1)) {
        /* If this number is an integer, and between [1,iLen], then
         * we added this one already above. */
        lua_Number f = lua_tonumber(L, -1);
        int i;
        lua_number2int(i, f);
        if (i >= 1 && i <= len && float(i) == f) {
          continue;
        }
      }

      std::string n_name;
      LuaHelpers::Pop(L, n_name);
      node_names_to_add.push_back(n_name);
      nodes_to_add.push_back(LuaReference());
      nodes_to_add.back().SetFromStack(L);
      continue;
    }

    std::string n_name;
    LuaHelpers::Pop(L, n_name);

    // Otherwise, add an attribute.
    XNodeLuaValue* value = new XNodeLuaValue;
    value->SetValueFromStack(L);
    node->AppendAttrFrom(n_name, value);
  }
  lua_pop(L, 1);

  // Recursively process tables.
  for (size_t i = 0; i < nodes_to_add.size(); ++i) {
    const std::string& node_name = node_names_to_add[i];
    LuaReference& node_to_add = nodes_to_add[i];

    // Check if the table is on the stack.
    processed_tables.PushSelf(L);
    node_to_add.PushSelf(L);  // push table
    lua_gettable(L, -2);

    bool saw_this_table_already = !lua_isnil(L, -1);
    lua_pop(L, 2);  // pop lua_gettable result, ProcessedTables
    if (saw_this_table_already) {
      continue;
    }

    // Add the table to the stack.
    processed_tables.PushSelf(L);
    node_to_add.PushSelf(L);
    lua_pushboolean(L, true);
    lua_settable(L, -3);
    lua_pop(L, 1);  // pop ProcessedTables

    node_to_add.PushSelf(L);
    XNode* new_node = XNodeFromTableRecursive(L, node_name, processed_tables);
    if (new_node) {
      node->AppendChild(new_node);
    }

    // Remove the table from the stack.
    processed_tables.PushSelf(L);
    node_to_add.PushSelf(L);
    lua_pushnil(L);
    lua_settable(L, -3);
    lua_pop(L, 1);  // pop ProcessedTables
  }

  return node;
}

}  // namespace

/* Pop a table off of the stack, and return an XNode tree referring recursively
 * to entries in the table.
 *
 * The table may not contain table cycles; if a cycle is detected, only the
 * first table seen will have a corresponding XNode.
 *
 * Users of the resulting XNode may access the original table via PushValue. */
XNode* XmlFileUtil::XNodeFromTable(lua_State* L) {
  /* Maintain a set of references that we've created. Tables may loop; XNode
   * trees may not. If we encounter a cycle, skip creating an XNode for
   * that node. */
  LuaReference processed_tables;
  lua_newtable(L);
  processed_tables.SetFromStack(L);

  return XNodeFromTableRecursive(L, "Layer", processed_tables);
}

/* Move nodes from pFrom into pTo which don't already exist in pTo. For
 * efficiency, nodes will be moved, not copied, so pFrom will be modified.
 * On return, the contents of pFrom will be undefined and should be deleted. */
void XmlFileUtil::MergeIniUnder(XNode* from, XNode* to) {
  /* Batch up nodes to move, and do them all at once, to deal sanely
   * with the possibility of duplicate child names. */
  std::vector<XNodes::iterator> to_move;

  // Iterate over each section in pFrom.
  XNodes::iterator it = from->GetChildrenBegin();
  while (it != from->GetChildrenEnd()) {
    XNodes::iterator next = it;
    ++next;

    // If this node doesn't exist in pTo, just move the whole node.
    XNode* section_node = *it;
    XNode* child_node = to->GetChild(section_node->GetName());
    if (child_node == nullptr) {
      to_move.push_back(it);
    } else {
      FOREACH_Attr(section_node, it2) {
        // Don't overwrite existing nodes.
        child_node->AppendAttrFrom(it2->first, it2->second->Copy(), false);
      }
    }

    it = next;
  }

  /* Iterate in reverse, since erasing iterators will invalidate the
   * iterators after it. */
  for (int i = to_move.size() - 1; i >= 0; --i) {
    XNode* node = *to_move[i];
    from->RemoveChild(node, false);
    to->AppendChild(node);
  }
}

/*
 * (c) 2001-2006 Chris Danford, Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
