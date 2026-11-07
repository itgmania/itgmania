#include "LuaDebugger.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "LuaDebugBreakpoint.h"
#include "LuaDebugDap.h"
#include "LuaDebugHelpers.h"
#include "LuaDebuggeeState.h"
#include "LuaManager.h"
#include "RageThreads.h"
#include "global.h"

namespace LuaDebug {

class Spinlock {
 public:
  bool try_lock() { return !locked_.test_and_set(std::memory_order_acquire); }

  void lock() {
    while (!try_lock()) {
      std::this_thread::yield();
    }
  }

  void unlock() { locked_.clear(std::memory_order_release); }

 private:
  std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
};

class Debugger::Impl {
 public:
  Impl()
      : lua_debug_mutex_("LuaDebug"), debuggee_state_(DebuggeeState::Root{}) {
    ASSERT(debugger_ == nullptr);
    debugger_ = this;
  }

  ~Impl() {
    // Clear breakpoints and continue execution.
    LockMutex lock(lua_debug_mutex_);

    DeleteAllBreakpoints();

    if (is_paused_) {
      SendRequest(CreateContinueRequest(0));
    }

    debugger_ = nullptr;
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void ActivateState(lua_State* lua) {
    LockMutex lock(lua_debug_mutex_);

    LuaThread* thread = nullptr;
    for (LuaThread& t : threads_) {
      if (t.lua_ == lua) {
        thread = &t;
        break;
      }
    }

    if (thread == nullptr) {
      LuaThread new_thread{next_thread_id_++, lua, true};
      threads_.push_back(new_thread);
      thread = &threads_.back();
      SendResponse(CreateThreadStartedEvent(thread->thread_id_));
    }

    lua_sethook(lua, lua_hook_, lua_hook_mask_, 0);

    thread->active_ = true;
    debuggee_state_.AddThread(thread->thread_id_, lua);
  }

  void DeactivateState(lua_State* lua) {
    LockMutex lock(lua_debug_mutex_);
    std::lock_guard lock2(breakpoints_lock_);

    lua_sethook(lua, nullptr, 0, 0);

    for (int i = breakpoints_.size() - 1; i >= 0; i--) {
      if (breakpoints_[i].GetThread() == lua) {
        breakpoints_.erase(breakpoints_.begin() + i);
      }
    }

    for (size_t i = 0; i < threads_.size(); i++) {
      if (threads_[i].lua_ == lua) {
        threads_[i].active_ = false;
        debuggee_state_.RemoveThread(threads_[i].thread_id_);
      }
    }
  }

  void SendRequest(Request&& request) {
    bool is_locked = lua_debug_mutex_.IsLockedByThisThread();
    if (!is_locked) {
      lua_debug_mutex_.Lock();
    }
    if (is_paused_) {
      to_lua_thread_.push(request);
      lua_debug_mutex_.Broadcast();
    } else {
      HandleRequest(request);
    }
    if (!is_locked) {
      lua_debug_mutex_.Unlock();
    }
  }

  bool TryReceiveResponse(Response& response) {
    LockMutex lock(lua_debug_mutex_);
    if (to_server_thread_.empty()) {
      return false;
    }
    response = std::move(to_server_thread_.front());
    to_server_thread_.pop();
    return true;
  }

  void SendResponse(Response&& response) {
    bool is_locked = lua_debug_mutex_.IsLockedByThisThread();
    if (!is_locked) {
      lua_debug_mutex_.Lock();
    }
    to_server_thread_.push(std::move(response));
    lua_debug_mutex_.Broadcast();
    if (!is_locked) {
      lua_debug_mutex_.Unlock();
    }
  }

  bool TryReceiveRequest(Request& request) {
    LockMutex lock(lua_debug_mutex_);
    if (to_lua_thread_.empty()) {
      return false;
    }
    request = to_lua_thread_.front();
    to_lua_thread_.pop();
    return true;
  }

  void ReceiveRequest(Request& request) {
    bool is_locked = lua_debug_mutex_.IsLockedByThisThread();
    if (!is_locked) {
      lua_debug_mutex_.Lock();
    }
    while (!TryReceiveRequest(request)) {
      lua_debug_mutex_.Wait();
    }
    if (!is_locked) {
      lua_debug_mutex_.Unlock();
    }
  }

 private:
  struct LuaThread {
    ThreadId thread_id_;
    lua_State* lua_;
    bool active_;
  };

  // Locks access to the shared data. Signaled when m_toLuaThread or
  // m_toServerThread is available for reading.
  RageEvent lua_debug_mutex_;

  // Access to m_breakpoints is synchronized by locking
  //  1. m_luaDebugMutex AND m_breakpointsLock on the server thread
  //  2. m_luaDebugMutex OR m_breakpointsLock on the Lua thread.
  // When locking both, m_luaDebugMutex is locked first to prevent deadlocks.
  Spinlock breakpoints_lock_;

  // Kept across client sessions
  std::vector<LuaThread> threads_;
  ThreadId next_thread_id_ = 1;
  BreakpointId next_breakpoint_id_ = 1;
  lua_Hook lua_hook_ = nullptr;
  int lua_hook_mask_ = 0;
  std::vector<Breakpoint> breakpoints_;

  // Client specific
  struct {
    int lines_offset_ = 0;
    bool supports_variable_type_ = false;
    SeqNo next_message_seq_ = 1;
  } client_;

  // State when execution is paused
  bool is_paused_ = false;
  ThreadId current_thread_ = 0;
  DebuggeeState debuggee_state_;

  // Communication between the Lua thread and the server thread.
  std::queue<Request> to_lua_thread_;
  std::queue<Response> to_server_thread_;

  void ContinueExecution() { is_paused_ = false; }

  void ExecuteUnknownCommand(const Request& request) {
    SendResponse(CreateErrorResponse(request, "unknown command"));
  }

  void ExecuteInitializeCommand(const Request& request) {
    const Json::Value& args = request["arguments"];
    client_.lines_offset_ =
        args["linesStartAt1"].isBool() && !args["linesStartAt1"].asBool() ? -1
                                                                          : 0;
    client_.supports_variable_type_ = args["supportsVariableType"].asBool();

    Response result = CreateDapResponse(request);
    result["body"]["supportsConfigurationDoneRequest"] = true;
    result["body"]["supportsFunctionBreakpoints"] = true;
    result["body"]["supportsConditionalBreakpoints"] = true;
    result["body"]["supportsHitConditionalBreakpoints"] = true;
    result["body"]["supportsSetVariable"] = true;
    result["body"]["supportSuspendDebuggee"] = true;
    result["body"]["supportsLogPoints"] = true;
    SendResponse(std::move(result));

    SendResponse(CreateDapEvent("initialized"));
    if (is_paused_) {
      SendResponse(CreateStoppedEvent(current_thread_, "entry", {}));
    }
  }

  void ExecuteConfigurationDoneCommand(const Request& request) {
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteAttachCommand(const Request& request) {
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteSetBreakpointsCommand(const Request& request) {
    std::string abs_path = request["arguments"]["source"]["path"].asString();

    const Json::Value& breakpoints = request["arguments"]["breakpoints"];
    int breakpoint_count = (int)breakpoints.size();

    std::vector<BreakpointId> new_breakpoint_ids;
    std::vector<int> line_numbers;
    for (int i = 0; (size_t)i < breakpoints.size(); i++) {
      int line_number = breakpoints[i]["line"].asInt();
      if (line_number >= 0) {
        line_number -= client_.lines_offset_;
      }
      if (line_number < 0) {
        line_number = 0;
      }
      line_numbers.push_back(line_number);
      new_breakpoint_ids.push_back(next_breakpoint_id_++);
    }

    std::vector<std::string> lua_paths = UnresolvePath(abs_path);
    for (std::string& source : lua_paths) {
      DeleteBreakpointsInSource(source);

      for (int i = 0; i < breakpoint_count; i++) {
        if (line_numbers[i] <= 0) {
          continue;
        }

        Breakpoint bp =
            Breakpoint::Line(new_breakpoint_ids[i], source, line_numbers[i]);
        bp.SetCondition(breakpoints[i]["condition"].asString());
        std::string hit_condition = breakpoints[i]["hitCondition"].asString();
        if (!hit_condition.empty()) {
          bp.SetHitCondition(std::stoi(hit_condition));
        }
        bp.SetLogMessage(breakpoints[i]["logMessage"].asString());

        AddBreakpoint(bp);
      }
    }

    Json::Value breakpoints_json(Json::arrayValue);
    for (int i = 0; i < breakpoint_count; i++) {
      Json::Value json;
      json["id"] = new_breakpoint_ids[i];
      json["source"]["path"] = abs_path;
      json["line"] = line_numbers[i] + client_.lines_offset_;
      if (lua_paths.empty()) {
        json["verified"] = false;
        json["reason"] = "failed";
        json["message"] = "Source file not found";
      } else {
        json["verified"] = true;
      }
      breakpoints_json.append(json);
    }

    Response result = CreateDapResponse(request);
    result["body"]["breakpoints"] = breakpoints_json;
    SendResponse(std::move(result));
  }

  void ExecuteSetFunctionBreakpointsCommand(const Request& request) {
    const Json::Value& breakpoints = request["arguments"]["breakpoints"];
    int breakpoint_count = (int)breakpoints.size();

    DeleteAllFunctionBreakpoints();

    Json::Value breakpoints_json(Json::arrayValue);
    for (int i = 0; (size_t)i < breakpoints.size(); i++) {
      Breakpoint bp = Breakpoint::Function(
          next_breakpoint_id_++, breakpoints[i]["name"].asString());
      bp.SetCondition(breakpoints[i]["condition"].asString());
      std::string hit_condition = breakpoints[i]["hitCondition"].asString();
      if (!hit_condition.empty()) {
        bp.SetHitCondition(std::stoi(hit_condition));
      }
      bp.SetLogMessage(breakpoints[i]["logMessage"].asString());

      AddBreakpoint(bp);

      Json::Value json;
      json["id"] = bp.GetId();
      json["verified"] = false;
      json["reason"] = "pending";
      breakpoints_json.append(json);
    }

    Response result = CreateDapResponse(request);
    result["body"]["breakpoints"] = breakpoints_json;
    SendResponse(std::move(result));
  }

  void ExecutePauseCommand(const Request& request) {
    SendResponse(CreateDapResponse(request));

    if (!is_paused_) {
      AddBreakpoint(Breakpoint::Pause());
    } else {
      SendResponse(CreateStoppedEvent(current_thread_, "pause", {}));
    }
  }

  void ExecuteNextCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    DebuggeeState& thread = debuggee_state_.GetThread(current_thread_);
    AddBreakpoint(Breakpoint::Next(thread.GetLua()));
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteStepInCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    DebuggeeState& thread = debuggee_state_.GetThread(current_thread_);
    AddBreakpoint(Breakpoint::Step(thread.GetLua()));
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteStepOutCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    DebuggeeState& thread = debuggee_state_.GetThread(current_thread_);
    AddBreakpoint(Breakpoint::Return(thread.GetLua()));
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteContinueCommand(const Request& request) {
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
    if (!is_paused_) {
      SendResponse(CreateContinuedEvent(0));
    }
  }

  void ExecuteDisconnectCommand(const Request& request) {
    DeleteAllBreakpoints();

    bool suspend_debuggee = request["arguments"]["suspendDebuggee"].asBool();
    if (is_paused_ && !suspend_debuggee) {
      ContinueExecution();
    }

    client_.lines_offset_ = 0;
    client_.supports_variable_type_ = false;
    client_.next_message_seq_ = 1;

    SendResponse(CreateDapResponse(request));
  }

  void ExecuteEvaluateCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    std::string expression = request["arguments"]["expression"].asString();
    ObjectRef frame_id = request["arguments"]["frameId"].asInt();

    DebuggeeState& frame_or_thread =
        frame_id ? debuggee_state_.GetObject(frame_id)
                 : debuggee_state_.GetThread(current_thread_);
    if (!frame_or_thread) {
      return SendResponse(CreateErrorResponse(request, "no such frame"));
    }

    lua_State* L = frame_or_thread.GetLua();
    int first_result_index = lua_gettop(L) + 1;
    int result_count = LuaEvaluate(expression, frame_or_thread);
    if (result_count < 0) {
      std::string error = LuaValueToString(L, -1, false);
      lua_pop(L, 1);
      return SendResponse(CreateErrorResponse(request, error));
    }

    std::string result;
    std::string result_type;
    for (int i = 0; i < result_count; i++) {
      if (!result.empty()) {
        result.append(", ");
      }
      result.append(LuaValueToString(L, first_result_index + i));

      if (!result_type.empty()) {
        result_type.append(", ");
      }
      result_type.append(lua_typename(L, lua_type(L, first_result_index + i)));
    }

    ObjectRef variables_reference = 0;
    if (result_count > 1) {
      // Create a reference for the return values.
      lua_createtable(L, result_count, 0);
      for (int i = 0; i < result_count; i++) {
        lua_pushvalue(L, first_result_index + i);
        lua_rawseti(L, -2, i + 1);
      }
      variables_reference = frame_or_thread.CreateReferenceFromStack().GetId();
    } else if (result_count == 1 && lua_type(L, -1) == LUA_TTABLE) {
      // Create a reference for the single return value.
      lua_pushvalue(L, -1);
      variables_reference = frame_or_thread.CreateReferenceFromStack().GetId();
    } else {
      variables_reference = 0;
    }
    lua_pop(L, result_count);

    Response response = CreateDapResponse(request);
    response["body"]["result"] = result;
    if (client_.supports_variable_type_) {
      response["body"]["type"] = result_type;
    }
    response["body"]["variablesReference"] = variables_reference;
    SendResponse(std::move(response));
  }

  void ExecuteSetVariableCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    ObjectRef parent_id = request["arguments"]["variablesReference"].asInt();
    std::string var_name = request["arguments"]["name"].asString();
    std::string expression = request["arguments"]["value"].asString();

    DebuggeeState& parent = debuggee_state_.GetObject(parent_id);
    if (!parent) {
      return SendResponse(CreateErrorResponse(request, "no such variable"));
    }
    lua_State* L = parent.GetLua();

    DebuggeeState& var = parent.GetVariable(var_name);
    if (!var) {
      return SendResponse(CreateErrorResponse(request, "no such variable"));
    }

    int result_count = LuaEvaluate(expression, parent.GetThread());
    if (result_count < 0) {
      std::string error = LuaValueToString(L, -1, false);
      lua_pop(L, 1);
      return SendResponse(CreateErrorResponse(request, error));
    }

    if (result_count > 1) {
      lua_pop(L, result_count - 1);
    }
    if (result_count < 1) {
      lua_pushnil(L);
    }

    int luaType = lua_type(L, -1);
    std::string value = LuaValueToString(L, -1);
    std::string type = lua_typename(L, luaType);
    if (!var.PopValue()) {
      return SendResponse(
          CreateErrorResponse(request, "error setting variable"));
    }

    Response response = CreateDapResponse(request);
    response["body"]["value"] = value;
    if (client_.supports_variable_type_) {
      response["body"]["type"] = type;
    }
    if (luaType == LUA_TTABLE) {
      response["body"]["variablesReference"] = var.GetId();
    }
    SendResponse(std::move(response));
  }

  void ExecuteThreadsCommand(const Request& request) {
    Json::Value threads_json(Json::arrayValue);
    for (DebuggeeState* thread : debuggee_state_.GetChildren()) {
      if (thread->GetType() != DebuggeeState::Type::Thread) {
        continue;
      }
      Json::Value thread_json;
      thread_json["id"] = thread->GetThreadId();
      thread_json["name"] = "Thread " + std::to_string(thread->GetThreadId());
      threads_json.append(thread_json);
    }

    if (threads_json.empty()) {
      Json::Value fake_thread;
      fake_thread["id"] = 0;
      fake_thread["name"] = "No threads";
      threads_json.append(fake_thread);
    }

    Response result = CreateDapResponse(request);
    result["body"]["threads"] = std::move(threads_json);
    SendResponse(std::move(result));
  }

  void ExecuteStackTraceCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ThreadId thread_id = request["arguments"]["threadId"].asUInt64();
    int start_frame = request["arguments"]["startFrame"].asInt();
    int levels = request["arguments"]["levels"].asInt();
    if (levels <= 0) {
      levels = INT_MAX;
    }

    DebuggeeState& thread = debuggee_state_.GetThread(thread_id);
    if (!thread) {
      Response empty_result = CreateDapResponse(request);
      empty_result["body"]["stackFrames"] = Json::Value(Json::arrayValue);
      empty_result["body"]["totalFrames"] = 0;
      SendResponse(std::move(empty_result));
    }

    Json::Value frames_json = Json::Value(Json::arrayValue);
    for (size_t i = start_frame;
         i < thread.GetChildren().size() && frames_json.size() < (size_t)levels;
         i++) {
      DebuggeeState& frame = *thread.GetChildren()[i];
      if (frame.GetType() != DebuggeeState::Type::StackFrame) {
        continue;
      }
      lua_Debug debug;
      if (!lua_getstack(frame.GetLua(), frame.GetStackLevel(), &debug)) {
        continue;
      }
      lua_getinfo(frame.GetLua(), "lnS", &debug);

      Json::Value frame_json;
      frame_json["id"] = frame.GetId();
      if (debug.name != nullptr && debug.name[0] != 0) {
        frame_json["name"] = debug.name;
      } else if (debug.what != nullptr && strcmp(debug.what, "Lua") == 0) {
        frame_json["name"] = "<Lua function>";
      } else if (debug.what != nullptr && strcmp(debug.what, "C") == 0) {
        frame_json["name"] = "<C function>";
      } else if (debug.what != nullptr && strcmp(debug.what, "C") == 0) {
        frame_json["name"] = "<main>";
      } else if (debug.what != nullptr && strcmp(debug.what, "tail") == 0) {
        frame_json["name"] = "<tail call>";
        frame_json["presentationHint"] = "label";
      } else {
        frame_json["name"] = "<unknown>";
      }

      std::string source = debug.source ? ResolvePath(debug.source) : "";
      if (!source.empty()) {
        frame_json["source"]["name"] = debug.short_src;
        frame_json["source"]["path"] = source;
      }

      if (debug.currentline > 0) {
        frame_json["line"] = debug.currentline + client_.lines_offset_;
      } else {
        frame_json["line"] = 0;
      }

      frame_json["column"] = 0;
      frames_json.append(frame_json);
    }

    Response result = CreateDapResponse(request);
    result["body"]["stackFrames"] = frames_json;
    result["body"]["totalFrames"] = (int)thread.GetChildren().size();
    SendResponse(std::move(result));
  }

  void ExecuteScopesCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ObjectRef frame_id = request["arguments"]["frameId"].asInt();

    DebuggeeState& frame = debuggee_state_.GetObject(frame_id);
    if (frame.GetType() != DebuggeeState::Type::StackFrame) {
      Response empty_result = CreateDapResponse(request);
      empty_result["body"]["scopes"] = Json::Value(Json::arrayValue);
      return SendResponse(std::move(empty_result));
    }

    Json::Value scopes_json(Json::arrayValue);

    DebuggeeState& locals = frame.GetLocals();
    if (locals) {
      Json::Value locals_json;
      locals_json["variablesReference"] = locals.GetId();
      locals_json["name"] = "Locals";
      locals_json["expensive"] = false;
      locals_json["presentationHint"] = "locals";
      scopes_json.append(locals_json);
    }

    DebuggeeState& upvalues = frame.GetUpvalues();
    if (upvalues) {
      Json::Value upvalues_json;
      upvalues_json["variablesReference"] = upvalues.GetId();
      upvalues_json["name"] = "Upvalues";
      upvalues_json["expensive"] = false;
      scopes_json.append(upvalues_json);
    }

    DebuggeeState& globals = frame.GetGlobals();
    if (globals) {
      Json::Value globals_json;
      globals_json["variablesReference"] = globals.GetId();
      globals_json["name"] = "Globals";
      globals_json["expensive"] = true;
      scopes_json.append(globals_json);
    }

    Response result = CreateDapResponse(request);
    result["body"]["scopes"] = std::move(scopes_json);
    SendResponse(std::move(result));
  }

  void ExecuteVariablesCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ObjectRef parent_id = request["arguments"]["variablesReference"].asInt();

    DebuggeeState& parent = debuggee_state_.GetObject(parent_id);
    if (!parent.IsVariablesContainer()) {
      return SendResponse(CreateErrorResponse(request, "no such variable"));
    }

    bool skip_named = request["arguments"]["filter"].asString() == "indexed";
    bool skip_indexed = parent.IsScope() &&
                        request["arguments"]["filter"].asString() == "named";

    Json::Value vars_json(Json::arrayValue);
    for (DebuggeeState* var : parent.GetChildren()) {
      if (!var->IsVariable()) {
        continue;
      }
      if (skip_named &&
          var->GetType() != DebuggeeState::Type::IndexedVariable) {
        continue;
      }
      if (skip_indexed &&
          var->GetType() == DebuggeeState::Type::IndexedVariable) {
        continue;
      }
      if (!var->PushValue()) {
        continue;
      }

      lua_State* L = var->GetLua();
      int luaType = lua_type(L, -1);

      Json::Value var_json;
      var_json["name"] = var->GetName();
      var_json["value"] = LuaValueToString(L, -1);
      if (client_.supports_variable_type_) {
        var_json["type"] = lua_typename(L, luaType);
      }
      var_json["variablesReference"] = luaType == LUA_TTABLE ? var->GetId() : 0;

      if (var->GetType() == DebuggeeState::Type::MetaTableVariable) {
        var_json["presentationHint"]["kind"] = "virtual";
      } else if (parent.IsScope() && var->IsTemporaryVariable()) {
        var_json["presentationHint"]["kind"] = "virtual";
      } else if (luaType == LUA_TFUNCTION) {
        var_json["presentationHint"]["kind"] = "method";
      } else if (parent.IsScope()) {
        var_json["presentationHint"]["kind"] = "property";
      }

      vars_json.append(var_json);

      lua_pop(L, 1);
    }

    Response result = CreateDapResponse(request);
    result["body"]["variables"] = std::move(vars_json);
    SendResponse(std::move(result));
  }

  void HandleRequest(const Request& request) {
    try {
      if (!request.isObject()) {
        return;
      }
      if (request["type"].asString() != "request") {
        return;
      }
      if (!request["seq"].isUInt64()) {
        return;
      }

      std::string command = request["command"].asString();
      if (command == "attach") {
        return ExecuteAttachCommand(request);
      }
      if (command == "configurationDone") {
        return ExecuteConfigurationDoneCommand(request);
      }
      if (command == "continue") {
        return ExecuteContinueCommand(request);
      }
      if (command == "disconnect") {
        return ExecuteDisconnectCommand(request);
      }
      if (command == "evaluate") {
        return ExecuteEvaluateCommand(request);
      }
      if (command == "initialize") {
        return ExecuteInitializeCommand(request);
      }
      if (command == "next") {
        return ExecuteNextCommand(request);
      }
      if (command == "pause") {
        return ExecutePauseCommand(request);
      }
      if (command == "scopes") {
        return ExecuteScopesCommand(request);
      }
      if (command == "setBreakpoints") {
        return ExecuteSetBreakpointsCommand(request);
      }
      if (command == "setFunctionBreakpoints") {
        return ExecuteSetFunctionBreakpointsCommand(request);
      }
      if (command == "setVariable") {
        return ExecuteSetVariableCommand(request);
      }
      if (command == "stackTrace") {
        return ExecuteStackTraceCommand(request);
      }
      if (command == "stepIn") {
        return ExecuteStepInCommand(request);
      }
      if (command == "stepOut") {
        return ExecuteStepOutCommand(request);
      }
      if (command == "threads") {
        return ExecuteThreadsCommand(request);
      }
      if (command == "variables") {
        return ExecuteVariablesCommand(request);
      }

      ExecuteUnknownCommand(request);
    } catch (const Json::LogicError&) {
      SendResponse(CreateErrorResponse(request, "badRequest"));
    }
  }

  void UpdateHook() {
    ASSERT(lua_debug_mutex_.IsLockedByThisThread());

    int full_mask = LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE;
    int mask = 0;
    for (Breakpoint& bp : breakpoints_) {
      mask |= bp.GetEventMask();
      if (mask == full_mask) {
        break;
      }
    }

    if (lua_hook_mask_ != mask) {
      lua_hook_ = mask == 0 ? nullptr : LuaHook_Static;
      lua_hook_mask_ = mask;
      for (LuaThread& thread : threads_) {
        if (thread.active_) {
          lua_sethook(thread.lua_, lua_hook_, lua_hook_mask_, 0);
        }
      }
    }
  }

  void AddBreakpoint(Breakpoint breakpoint) {
    LockMutex lock(lua_debug_mutex_);
    std::lock_guard lock2(breakpoints_lock_);

    breakpoints_.push_back(breakpoint);
    UpdateHook();
  }

  void DeleteBreakpointsInSource(std::string source) {
    LockMutex lock(lua_debug_mutex_);
    std::lock_guard lock2(breakpoints_lock_);

    bool any_deleted = false;
    for (int i = breakpoints_.size() - 1; i >= 0; i--) {
      if (breakpoints_[i].GetFileName() == source) {
        breakpoints_.erase(breakpoints_.begin() + i);
        any_deleted = true;
      }
    }
    if (any_deleted) {
      UpdateHook();
    }
  }

  void DeleteAllFunctionBreakpoints() {
    LockMutex lock(lua_debug_mutex_);
    std::lock_guard lock2(breakpoints_lock_);

    bool any_deleted = false;
    for (int i = breakpoints_.size() - 1; i >= 0; i--) {
      if (!breakpoints_[i].GetFunctionName().empty()) {
        breakpoints_.erase(breakpoints_.begin() + i);
        any_deleted = true;
      }
    }
    if (any_deleted) {
      UpdateHook();
    }
  }

  void DeleteAllBreakpoints() {
    LockMutex lock(lua_debug_mutex_);
    std::lock_guard lock2(breakpoints_lock_);

    breakpoints_.clear();
    UpdateHook();
  }

  void LuaHook(lua_State* L, lua_Debug* ar) {
    // These are set to arbitrary values before calling lua_getinfo. Clear them
    // so that Breakpoint::IsHit can set them as needed.
    ar->source = nullptr;
    ar->currentline = INT_MIN;
    ar->name = nullptr;

    // Locking the m_luaDebugMutex mutex is slow, so try with only
    // m_breakpointsLock first. Only try to lock it once to avoid excessive
    // spinning if the server thread holds it for a long time.
    bool is_any_hit = false;
    if (breakpoints_lock_.try_lock()) {
      for (Breakpoint& bp : breakpoints_) {
        if (bp.IsHit(L, *ar)) {
          is_any_hit = true;
          break;
        }
      }
      breakpoints_lock_.unlock();
      if (!is_any_hit) {
        return;
      }
    }

    LockMutex lock(lua_debug_mutex_);

    // Check again after acquiring m_luaDebugMutex. The breakpoints could have
    // changed between releasing m_breakpointsLock and acquiring
    // m_luaDebugMutex, or if we failed to acquire m_breakpointsLock at all.
    is_any_hit = false;
    for (Breakpoint& bp : breakpoints_) {
      if (bp.IsHit(L, *ar)) {
        is_any_hit = true;
        break;
      }
    }
    if (!is_any_hit) {
      return;
    }

    std::string source = ar->source ? ResolvePath(ar->source) : "";
    std::string source_name = !source.empty() ? ar->short_src : "";
    int line_number =
        ar->currentline > 0 ? ar->currentline + client_.lines_offset_ : -1;

    DebuggeeState& current_stack_frame =
        debuggee_state_.GetThread(L).GetStackFrame(0);
    current_thread_ = current_stack_frame.GetThreadId();

    std::string reason;
    std::string evaluation_errors;
    std::vector<BreakpointId> hit_breakpoint_ids;
    bool any_deleted = false;
    for (size_t i = 0; i < breakpoints_.size(); i++) {
      Breakpoint& bp = breakpoints_[i];
      if (!bp.IsHit(L, *ar)) {
        continue;
      }

      std::string eval_error;
      if (!bp.EvaluateConditions(current_stack_frame, eval_error)) {
        continue;
      }
      if (!eval_error.empty()) {
        is_paused_ = true;
        if (!evaluation_errors.empty()) {
          evaluation_errors.append("\n");
        }
        evaluation_errors.append(eval_error);
        if (bp.GetId() != 0) {
          hit_breakpoint_ids.push_back(bp.GetId());
        }
        continue;
      }

      if (bp.IsBreak()) {
        is_paused_ = true;
        reason = bp.GetReason();
        if (bp.GetId() != 0) {
          hit_breakpoint_ids.push_back(bp.GetId());
        }
      }

      std::string log_error;
      std::string log_message =
          bp.EvaluateLogMessage(current_stack_frame, log_error);
      if (!log_error.empty()) {
        is_paused_ = true;
        if (!evaluation_errors.empty()) {
          evaluation_errors.append("\n");
        }
        evaluation_errors.append(log_error);
        if (bp.GetId() != 0) {
          hit_breakpoint_ids.push_back(bp.GetId());
        }
        continue;
      }

      if (!log_message.empty()) {
        SendResponse(
            CreateOutputEvent(log_message, source_name, source, line_number));
      }

      if (bp.IsTemporary()) {
        breakpoints_.erase(breakpoints_.begin() + i);
        i--;
        any_deleted = true;
      }
    }

    if (any_deleted) {
      UpdateHook();
    }

    if (is_paused_) {
      if (!evaluation_errors.empty()) {
        SendResponse(CreateBreakpointEvaluationErrorEvent(
            current_thread_, evaluation_errors, hit_breakpoint_ids));
      } else {
        SendResponse(
            CreateStoppedEvent(current_thread_, reason, hit_breakpoint_ids));
      }

      while (is_paused_) {
        Request request;
        ReceiveRequest(request);
        HandleRequest(request);
      }
      SendResponse(CreateContinuedEvent(current_thread_));
    }

    current_thread_ = 0;
    debuggee_state_.Invalidate();
  }

  // Need to use a static variable to pass the debugger to the Lua hook.
  static Debugger::Impl* debugger_;

  static void LuaHook_Static(lua_State* L, lua_Debug* debug) {
    ASSERT(debugger_ != nullptr);
    debugger_->LuaHook(L, debug);
  }
};

Debugger::Impl* Debugger::Impl::debugger_;

Debugger::Debugger() : impl_(new Debugger::Impl()) {}
Debugger::~Debugger() = default;

void Debugger::ActivateState(lua_State* lua) { impl_->ActivateState(lua); }
void Debugger::DeactivateState(lua_State* lua) { impl_->DeactivateState(lua); }
void Debugger::SendRequest(Request&& request) {
  impl_->SendRequest(std::move(request));
}
bool Debugger::TryReceiveResponse(Response& response) {
  return impl_->TryReceiveResponse(response);
}
}  // namespace LuaDebug

/*
 * Copyright (C) 2025  Arttu Ylä-Outinen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
