/* XmlFile - Simple XML reading and writing. */

#ifndef XML_FILE_H
#define XML_FILE_H

#include <map>
#include <string>
#include <vector>

struct DateTime;
class RageFileBasic;
struct lua_State;

class XNodeValue {
 public:
  virtual ~XNodeValue() {}
  virtual XNodeValue* Copy() const = 0;

  virtual void GetValue(std::string& out) const = 0;
  virtual void GetValue(int& out) const = 0;
  virtual void GetValue(float& out) const = 0;
  virtual void GetValue(bool& out) const = 0;
  virtual void GetValue(unsigned& out) const = 0;
  virtual void PushValue(lua_State* L) const = 0;

  template <typename T>
  T GetValue() const {
    T val;
    GetValue(val);
    return val;
  }

  virtual void SetValue(const std::string& v) = 0;
  virtual void SetValue(int v) = 0;
  virtual void SetValue(float v) = 0;
  virtual void SetValue(unsigned v) = 0;
  virtual void SetValueFromStack(lua_State* L) = 0;
};

class XNodeStringValue : public XNodeValue {
 public:
  std::string value_;

  XNodeValue* Copy() const { return new XNodeStringValue(*this); }

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

typedef std::map<std::string, XNodeValue*> XAttrs;
class XNode;
typedef std::vector<XNode*> XNodes;
/** @brief Loop through each node. */
#define FOREACH_Attr(pNode, Var)                       \
  for (XAttrs::iterator Var = (pNode)->attrs_.begin(); \
       Var != (pNode)->attrs_.end(); ++Var)
/** @brief Loop through each node, using a constant iterator. */
#define FOREACH_CONST_Attr(pNode, Var)                       \
  for (XAttrs::const_iterator Var = (pNode)->attrs_.begin(); \
       Var != (pNode)->attrs_.end(); ++Var)
/** @brief Loop through each child. */
#define FOREACH_Child(pNode, Var)                                             \
  XNode* Var = nullptr;                                                       \
  for (XNodes::iterator Var##Iter = (pNode)->GetChildrenBegin();              \
       Var = (Var##Iter != (pNode)->GetChildrenEnd()) ? *Var##Iter : nullptr, \
                        Var##Iter != (pNode)->GetChildrenEnd();               \
       ++Var##Iter)
/** @brief Loop through each child, using a constant iterator. */
#define FOREACH_CONST_Child(pNode, Var)                                       \
  const XNode* Var = nullptr;                                                 \
  for (XNodes::const_iterator Var##Iter = (pNode)->GetChildrenBegin();        \
       Var = (Var##Iter != (pNode)->GetChildrenEnd()) ? *Var##Iter : nullptr, \
                              Var##Iter != (pNode)->GetChildrenEnd();         \
       ++Var##Iter)

class XNode {
 private:
  XNodes childs_;  // child nodes
  std::multimap<std::string, XNode*> children_by_name_;

 public:
  std::string name_;
  XAttrs attrs_;  // attributes

  void SetName(const std::string& name) { name_ = name; }
  const std::string& GetName() const { return name_; }

  static const std::string kTextAttribute;
  template <typename T>
  void GetTextValue(T& out) const {
    GetAttrValue(kTextAttribute, out);
  }

  // in own attribute list
  const XNodeValue* GetAttr(const std::string& attr_name) const;
  XNodeValue* GetAttr(const std::string& attr_name);
  template <typename T>
  bool GetAttrValue(const std::string& name, T& out) const {
    const XNodeValue* attr = GetAttr(name);
    if (attr == nullptr) {
      return false;
    }
    attr->GetValue(out);
    return true;
  }
  bool PushAttrValue(lua_State* L, const std::string& name) const;

  XNodes::iterator GetChildrenBegin() { return childs_.begin(); }
  XNodes::const_iterator GetChildrenBegin() const { return childs_.begin(); }
  XNodes::iterator GetChildrenEnd() { return childs_.end(); }
  XNodes::const_iterator GetChildrenEnd() const { return childs_.end(); }
  bool ChildrenEmpty() const { return childs_.empty(); }

  // in one level child nodes
  const XNode* GetChild(const std::string& name) const;
  XNode* GetChild(const std::string& name);
  template <typename T>
  bool GetChildValue(const std::string& name, T& out) const {
    const XNode* child = GetChild(name);
    if (child == nullptr) {
      return false;
    }
    child->GetTextValue(out);
    return true;
  }
  // modify DOM
  template <typename T>
  XNode* AppendChild(const std::string& name, T value) {
    XNode* p = AppendChild(name);
    p->AppendAttr(XNode::kTextAttribute, value);
    return p;
  }
  XNode* AppendChild(const std::string& name) {
    XNode* p = new XNode(name);
    return AppendChild(p);
  }
  XNode* AppendChild(XNode* node);
  bool RemoveChild(XNode* node, bool b_delete = true);
  void RemoveChildFromByName(XNode* node);
  void RenameChildInByName(XNode* node);

  XNodeValue* AppendAttrFrom(
      const std::string& name, XNodeValue* value, bool overwrite = true);
  XNodeValue* AppendAttr(const std::string& name);
  template <typename T>
  XNodeValue* AppendAttr(const std::string& name, T value) {
    XNodeValue* val = AppendAttr(name);
    val->SetValue(value);
    return val;
  }
  XNode();
  explicit XNode(const std::string& name);
  XNode(const XNode& cpy);
  ~XNode() { Free(); }

  void Clear();

 private:
  void Free();
  XNode& operator=(const XNode& cpy);  // don't use
};

#endif
