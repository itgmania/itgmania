// Adapted from http://www.codeproject.com/cpp/xmlite.asp.
// On 2004-02-09 Cho, Kyung-Min gave us permission to use and modify this
// library.
//
// XmlFile : XML Lite Parser Library
// by Cho, Kyung Min: bro@shinbiro.com 2002-10-30

#include "XmlFile.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

#include "LuaManager.h"
#include "RageUtil.h"
#include "global.h"

const std::string XNode::kTextAttribute = "__TEXT__";

XNode::XNode() {}

XNode::XNode(const std::string& name) { name_ = name; }

XNode::XNode(const XNode& cpy) : name_(cpy.name_) {
  FOREACH_CONST_Attr(&cpy, attr) this->AppendAttrFrom(
      attr->first, attr->second->Copy());
  FOREACH_CONST_Child(&cpy, c) this->AppendChild(new XNode(*c));
}

void XNode::Clear() { Free(); }

void XNode::Free() {
  FOREACH_Child(this, p) delete p;
  FOREACH_Attr(this, attr) delete attr->second;
  childs_.clear();
  children_by_name_.clear();
  attrs_.clear();
}

void XNodeStringValue::GetValue(std::string& out) const { out = value_; }
void XNodeStringValue::GetValue(int& out) const { out = StringToInt(value_); }
void XNodeStringValue::GetValue(float& out) const {
  out = StringToFloat(value_);
}
void XNodeStringValue::GetValue(bool& out) const {
  out = StringToInt(value_) != 0;
}
void XNodeStringValue::GetValue(unsigned& out) const {
  out = strtoul(value_.c_str(), nullptr, 0);
}
void XNodeStringValue::PushValue(lua_State* L) const {
  LuaHelpers::Push(L, value_);
}

void XNodeStringValue::SetValue(const std::string& v) { value_ = v; }
void XNodeStringValue::SetValue(int v) { value_ = ssprintf("%d", v); }
void XNodeStringValue::SetValue(float v) { value_ = ssprintf("%f", v); }
void XNodeStringValue::SetValue(unsigned v) { value_ = ssprintf("%u", v); }
void XNodeStringValue::SetValueFromStack(lua_State* L) {
  LuaHelpers::Pop(L, value_);
}

const XNodeValue* XNode::GetAttr(const std::string& attrname) const {
  XAttrs::const_iterator it = attrs_.find(attrname);
  if (it != attrs_.end()) {
    return it->second;
  }
  return nullptr;
}

bool XNode::PushAttrValue(lua_State* L, const std::string& name) const {
  const XNodeValue* attr = GetAttr(name);
  if (attr == nullptr) {
    lua_pushnil(L);
    return false;
  }
  attr->PushValue(L);
  return true;
}

XNodeValue* XNode::GetAttr(const std::string& attrname) {
  XAttrs::iterator it = attrs_.find(attrname);
  if (it != attrs_.end()) {
    return it->second;
  }
  return nullptr;
}

XNode* XNode::GetChild(const std::string& name) {
  std::multimap<std::string, XNode*>::iterator by_name =
      children_by_name_.lower_bound(name);
  if (by_name != children_by_name_.end() &&
      name == by_name->second->GetName()) {
    return by_name->second;
  }
  return nullptr;
}

const XNode* XNode::GetChild(const std::string& name) const {
  std::multimap<std::string, XNode*>::const_iterator by_name =
      children_by_name_.lower_bound(name);
  if (by_name != children_by_name_.end() &&
      name == by_name->second->GetName()) {
    return by_name->second;
  }
  return nullptr;
}

XNode* XNode::AppendChild(XNode* node) {
  DEBUG_ASSERT(node->name_.size());
  children_by_name_.insert(std::make_pair(node->name_, node));
  childs_.push_back(node);
  return node;
}

// detach node and delete object
bool XNode::RemoveChild(XNode* node, bool b_delete) {
  XNodes::iterator it = find(childs_.begin(), childs_.end(), node);
  if (it == childs_.end()) {
    return false;
  }
  RemoveChildFromByName(node);
  if (b_delete) {
    delete node;
  }
  childs_.erase(it);
  return true;
}

void XNode::RemoveChildFromByName(XNode* node) {
  std::multimap<std::string, XNode*>::iterator by_name =
      children_by_name_.lower_bound(node->name_);
  if (by_name != children_by_name_.end() &&
      node->GetName() == by_name->second->GetName()) {
    for (; by_name != children_by_name_.end(); ++by_name) {
      if (by_name->second == node) {
        children_by_name_.erase(by_name);
        break;
      }
    }
  }
}

void XNode::RenameChildInByName(XNode* node) {
  RemoveChildFromByName(node);
  children_by_name_.insert(std::make_pair(node->name_, node));
}

/* If bOverwrite is true and a node already exists with that name, the old value
 * will be deleted. If bOverwrite is false and a node already exists with that
 * name, the new value will be deleted. */
XNodeValue* XNode::AppendAttrFrom(
    const std::string& name, XNodeValue* value, bool overwrite) {
  DEBUG_ASSERT(name.size());
  std::pair<XAttrs::iterator, bool> ret =
      attrs_.insert(std::make_pair(name, (XNodeValue*)nullptr));
  if (!ret.second)  // already existed
  {
    if (overwrite) {
      delete ret.first->second;
    } else {
      delete value;
      value = ret.first->second;
    }
  }

  ret.first->second = value;

  return ret.first->second;
};

XNodeValue* XNode::AppendAttr(const std::string& name) {
  DEBUG_ASSERT(name.size());
  std::pair<XAttrs::iterator, bool> ret =
      attrs_.insert(std::make_pair(name, (XNodeValue*)nullptr));
  if (ret.second) {
    ret.first->second = new XNodeStringValue;
  }
  return ret.first->second;  // already existed
}
