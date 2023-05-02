
#ifndef LUABINDING_H
#define LUABINDING_H

#include "global.h"

#include <vector>

#include "LuaManager.h"
#include "LuaReference.h"

// helpers to expose Lua bindings for C++ classes.
class LuaBinding {
 public:
  LuaBinding();
  virtual ~LuaBinding();
  void Register(lua_State* L);

  static void RegisterTypes(lua_State* L);

  bool IsDerivedClass() const { return GetClassName() != GetBaseClassName(); }
  virtual const RString& GetClassName() const = 0;
  virtual const RString& GetBaseClassName() const = 0;

  static void ApplyDerivedType(Lua* L, const RString& class_name, void* self);
  static bool CheckLuaObjectType(lua_State* L, int num_args, const char* type);

 protected:
  virtual void Register(Lua* L, int methods, int metatable) = 0;

  static void CreateMethodsTable(lua_State* L, const RString& name);
  static void* GetPointerFromStack(Lua* L, const RString& type, int arg);

  static bool Equal(lua_State* L);
  static int PushEqual(lua_State* L);
};

// Allow the binding of Lua to various classes.
template <typename Type>
class Luna : public LuaBinding {
 protected:
  typedef Type T;
  typedef int(binding_t)(T* p, lua_State* L);

  struct RegType {
    const char* name;
    binding_t* func;
  };

  void Register(Lua* L, int methods, int metatable) {
    lua_pushcfunction(L, tostring_T);
    lua_setfield(L, metatable, "__tostring");

    // fill method table with methods from class T
    for (unsigned i = 0; i < methods_.size(); ++i) {
      const RegType* l = &methods_[i];
      lua_pushlightuserdata(L, (void*)l->func);
      lua_pushcclosure(L, thunk, 1);
      lua_setfield(L, methods, l->name);
    }
  }

 public:
  virtual const RString& GetClassName() const { return class_name_; }
  virtual const RString& GetBaseClassName() const { return base_class_name_; }
  static RString class_name_;
  static RString base_class_name_;

  // Get userdata from the Lua stack and return a pointer to T object.
  static T* check(lua_State* L, int num_args, bool is_self = false) {
    if (!LuaBinding::CheckLuaObjectType(L, num_args, class_name_)) {
      if (is_self) {
        luaL_typerror(L, num_args, class_name_);
      } else {
        LuaHelpers::TypeError(L, num_args, class_name_);
      }
    }

    return get(L, num_args);
  }

  static T* get(lua_State* L, int num_args) {
    return (T*)GetPointerFromStack(L, class_name_, num_args);
  }

  // Push a table or userdata for the given object. This is called on the base
	// class, so we pick up the instance of the base class, if any.
  static void PushObject(Lua* L, const RString& derived_class_name, T* p);

 protected:
  void AddMethod(const char* name, int (*func)(T* p, lua_State* L)) {
    RegType r = {name, func};
    methods_.push_back(r);
  }

 private:
  // Member function dispatcher.
  static int thunk(Lua* L) {
    // Stack has userdata, followed by method args.
		// Get self
    T* obj = check(L, 1, true);
		// Remove self so member function args start at index 1.
    lua_remove(L, 1);
    // Get member function from upvalue.
    binding_t* func = (binding_t*)lua_touserdata(L, lua_upvalueindex(1));
		// Call member function.
    return func(obj, L);
  }

  std::vector<RegType> methods_;

  static int tostring_T(lua_State* L) {
    char buff[32];
    const void* data = check(L, 1);
    snprintf(buff, sizeof(buff), "%p", data);
    lua_pushfstring(L, "%s (%s)", class_name_.c_str(), buff);
    return 1;
  }
};

// Instanced classes have an associated table, which is used as "self" instead
// of a raw userdata. This should be as lightweight as possible.
#include "LuaReference.h"
class LuaClass : public LuaTable {
 public:
  LuaClass() {}
  LuaClass(const LuaClass& cpy);
  virtual ~LuaClass();
  LuaClass& operator=(const LuaClass& cpy);
};

// Only a base class has to indicate that it's instanced (has a per-object Lua
// table). Derived classes simply call the base class's Push function,
// specifying a different class name, so they don't need to know about it.
#define LUA_REGISTER_INSTANCED_BASE_CLASS(T)                                  \
  template <>                                                                 \
  void Luna<T>::PushObject(Lua* L, const RString& derived_class_name, T* p) { \
    p->m_pLuaInstance->PushSelf(L);                                           \
    LuaBinding::ApplyDerivedType(L, derived_class_name, p);                   \
  }                                                                           \
  LUA_REGISTER_CLASS_BASIC(T, T)

#define LUA_REGISTER_CLASS(T)                                                 \
  template <>                                                                 \
  void Luna<T>::PushObject(Lua* L, const RString& derived_class_name, T* p) { \
    void** pData = (void**)lua_newuserdata(L, sizeof(void*));                 \
    *pData = p;                                                               \
    LuaBinding::ApplyDerivedType(L, derived_class_name, p);                   \
  }                                                                           \
  LUA_REGISTER_CLASS_BASIC(T, T)

#define LUA_REGISTER_DERIVED_CLASS(T, B)                                      \
  template <>                                                                 \
  void Luna<T>::PushObject(Lua* L, const RString& derived_class_name, T* p) { \
    Luna<B>::PushObject(L, derived_class_name, p);                            \
  }                                                                           \
  LUA_REGISTER_CLASS_BASIC(T, B)

#define LUA_REGISTER_CLASS_BASIC(T, B)                              \
  template <>                                                       \
  RString Luna<T>::class_name_ = #T;                                \
  template <>                                                       \
  RString Luna<T>::base_class_name_ = #B;                           \
  void T::PushSelf(lua_State* L) {                                  \
    Luna<B>::PushObject(L, Luna<T>::class_name_, this);             \
  }                                                                 \
  static Luna##T registera##T;                                      \
  /* Call PushSelf, so we always call the derived Luna<T>::Push. */ \
  namespace LuaHelpers {                                            \
  template <>                                                       \
  void Push<T*>(lua_State * L, T* const& object) {                  \
    if (object == nullptr)                                          \
      lua_pushnil(L);                                               \
    else                                                            \
      object->PushSelf(L);                                          \
  }                                                                 \
  }

#define DEFINE_METHOD(method_name, expr)       \
  static int method_name(T* p, lua_State* L) { \
    LuaHelpers::Push(L, p->expr);              \
    return 1;                                  \
  }

#define COMMON_RETURN_SELF \
  p->PushSelf(L);          \
  return 1;

#define GET_SET_BOOL_METHOD(method_name, bool_name)  \
  static int get_##method_name(T* p, lua_State* L) { \
    lua_pushboolean(L, p->bool_name);                \
    return 1;                                        \
  }                                                  \
  static int set_##method_name(T* p, lua_State* L) { \
    p->bool_name = lua_toboolean(L, 1);              \
    COMMON_RETURN_SELF;                              \
  }

#define GETTER_SETTER_BOOL_METHOD(bool_name)       \
  static int get_##bool_name(T* p, lua_State* L) { \
    lua_pushboolean(L, p->get_##bool_name());      \
    return 1;                                      \
  }                                                \
  static int set_##bool_name(T* p, lua_State* L) { \
    p->set_##bool_name(lua_toboolean(L, 1));       \
    COMMON_RETURN_SELF;                            \
  }

#define GET_SET_FLOAT_METHOD(method_name, float_name) \
  static int get_##method_name(T* p, lua_State* L) {  \
    lua_pushnumber(L, p->float_name);                 \
    return 1;                                         \
  }                                                   \
  static int set_##method_name(T* p, lua_State* L) {  \
    p->float_name = FArg(1);                          \
    COMMON_RETURN_SELF;                               \
  }

#define GETTER_SETTER_FLOAT_METHOD(float_name)      \
  static int get_##float_name(T* p, lua_State* L) { \
    lua_pushnumber(L, p->get_##float_name());       \
    return 1;                                       \
  }                                                 \
  static int set_##float_name(T* p, lua_State* L) { \
    p->set_##float_name(FArg(1));                   \
    COMMON_RETURN_SELF;                             \
  }

#define ADD_METHOD(method_name) AddMethod(#method_name, method_name)
#define ADD_GET_SET_METHODS(method_name) \
  ADD_METHOD(get_##method_name);         \
  ADD_METHOD(set_##method_name);

#define LUA_REGISTER_NAMESPACE(T)         \
  static void Register##T(lua_State* L) { \
    luaL_register(L, #T, T##Table);       \
    lua_pop(L, 1);                        \
  }                                       \
  REGISTER_WITH_LUA_FUNCTION(Register##T)
#define LIST_METHOD(method_name) \
  {                              \
#method_name, method_name    \
  }

// Explicitly separates the stack into args and return values.
// This way, the stack can safely be used to store the previous values.
void DefaultNilArgs(lua_State* L, int n);
float FArgGTEZero(lua_State* L, int index);

#endif  // LUABINDING_H

/*
 * (c) 2001-2005 Peter Shook, Chris Danford, Glenn Maynard
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
