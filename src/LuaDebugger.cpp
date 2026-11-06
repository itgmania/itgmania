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
      LuaThread newThread{next_thread_id_++, lua, true};
      threads_.push_back(newThread);
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
    bool isLocked = lua_debug_mutex_.IsLockedByThisThread();
    if (!isLocked) {
      lua_debug_mutex_.Lock();
    }
    if (is_paused_) {
      to_lua_thread_.push(request);
      lua_debug_mutex_.Broadcast();
    } else {
      HandleRequest(request);
    }
    if (!isLocked) {
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
    bool isLocked = lua_debug_mutex_.IsLockedByThisThread();
    if (!isLocked) {
      lua_debug_mutex_.Lock();
    }
    to_server_thread_.push(std::move(response));
    lua_debug_mutex_.Broadcast();
    if (!isLocked) {
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
    bool isLocked = lua_debug_mutex_.IsLockedByThisThread();
    if (!isLocked) {
      lua_debug_mutex_.Lock();
    }
    while (!TryReceiveRequest(request)) {
      lua_debug_mutex_.Wait();
    }
    if (!isLocked) {
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
    std::string absPath = request["arguments"]["source"]["path"].asString();

    const Json::Value& breakpoints = request["arguments"]["breakpoints"];
    int breakpointCount = (int)breakpoints.size();

    std::vector<BreakpointId> newBreakpointIds;
    std::vector<int> lineNumbers;
    for (int i = 0; (size_t)i < breakpoints.size(); i++) {
      int lineNumber = breakpoints[i]["line"].asInt();
      if (lineNumber >= 0) {
        lineNumber -= client_.lines_offset_;
      }
      if (lineNumber < 0) {
        lineNumber = 0;
      }
      lineNumbers.push_back(lineNumber);
      newBreakpointIds.push_back(next_breakpoint_id_++);
    }

    std::vector<std::string> luaPaths = UnresolvePath(absPath);
    for (std::string& source : luaPaths) {
      DeleteBreakpointsInSource(source);

      for (int i = 0; i < breakpointCount; i++) {
        if (lineNumbers[i] <= 0) {
          continue;
        }

        Breakpoint bp =
            Breakpoint::Line(newBreakpointIds[i], source, lineNumbers[i]);
        bp.SetCondition(breakpoints[i]["condition"].asString());
        std::string hitCondition = breakpoints[i]["hitCondition"].asString();
        if (!hitCondition.empty()) {
          bp.SetHitCondition(std::stoi(hitCondition));
        }
        bp.SetLogMessage(breakpoints[i]["logMessage"].asString());

        AddBreakpoint(bp);
      }
    }

    Json::Value breakpointsJson(Json::arrayValue);
    for (int i = 0; i < breakpointCount; i++) {
      Json::Value bpJson;
      bpJson["id"] = newBreakpointIds[i];
      bpJson["source"]["path"] = absPath;
      bpJson["line"] = lineNumbers[i] + client_.lines_offset_;
      if (luaPaths.empty()) {
        bpJson["verified"] = false;
        bpJson["reason"] = "failed";
        bpJson["message"] = "Source file not found";
      } else {
        bpJson["verified"] = true;
      }
      breakpointsJson.append(bpJson);
    }

    Response result = CreateDapResponse(request);
    result["body"]["breakpoints"] = breakpointsJson;
    SendResponse(std::move(result));
  }

  void ExecuteSetFunctionBreakpointsCommand(const Request& request) {
    const Json::Value& breakpoints = request["arguments"]["breakpoints"];
    int breakpointCount = (int)breakpoints.size();

    DeleteAllFunctionBreakpoints();

    Json::Value breakpointsJson(Json::arrayValue);
    for (int i = 0; (size_t)i < breakpoints.size(); i++) {
      Breakpoint bp = Breakpoint::Function(
          next_breakpoint_id_++, breakpoints[i]["name"].asString());
      bp.SetCondition(breakpoints[i]["condition"].asString());
      std::string hitCondition = breakpoints[i]["hitCondition"].asString();
      if (!hitCondition.empty()) {
        bp.SetHitCondition(std::stoi(hitCondition));
      }
      bp.SetLogMessage(breakpoints[i]["logMessage"].asString());

      AddBreakpoint(bp);

      Json::Value bpJson;
      bpJson["id"] = bp.GetId();
      bpJson["verified"] = false;
      bpJson["reason"] = "pending";
      breakpointsJson.append(bpJson);
    }

    Response result = CreateDapResponse(request);
    result["body"]["breakpoints"] = breakpointsJson;
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

    bool suspendDebuggee = request["arguments"]["suspendDebuggee"].asBool();
    if (is_paused_ && !suspendDebuggee) {
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
    ObjectRef frameId = request["arguments"]["frameId"].asInt();

    DebuggeeState& frameOrThread =
        frameId ? debuggee_state_.GetObject(frameId)
                : debuggee_state_.GetThread(current_thread_);
    if (!frameOrThread) {
      return SendResponse(CreateErrorResponse(request, "no such frame"));
    }

    lua_State* L = frameOrThread.GetLua();
    int firstResultIndex = lua_gettop(L) + 1;
    int resultCount = LuaEvaluate(expression, frameOrThread);
    if (resultCount < 0) {
      std::string error = LuaValueToString(L, -1, false);
      lua_pop(L, 1);
      return SendResponse(CreateErrorResponse(request, error));
    }

    std::string result;
    std::string resultType;
    for (int i = 0; i < resultCount; i++) {
      if (!result.empty()) {
        result.append(", ");
      }
      result.append(LuaValueToString(L, firstResultIndex + i));

      if (!resultType.empty()) {
        resultType.append(", ");
      }
      resultType.append(lua_typename(L, lua_type(L, firstResultIndex + i)));
    }

    ObjectRef variablesReference = 0;
    if (resultCount > 1) {
      // Create a reference for the return values.
      lua_createtable(L, resultCount, 0);
      for (int i = 0; i < resultCount; i++) {
        lua_pushvalue(L, firstResultIndex + i);
        lua_rawseti(L, -2, i + 1);
      }
      variablesReference = frameOrThread.CreateReferenceFromStack().GetId();
    } else if (resultCount == 1 && lua_type(L, -1) == LUA_TTABLE) {
      // Create a reference for the single return value.
      lua_pushvalue(L, -1);
      variablesReference = frameOrThread.CreateReferenceFromStack().GetId();
    } else {
      variablesReference = 0;
    }
    lua_pop(L, resultCount);

    Response response = CreateDapResponse(request);
    response["body"]["result"] = result;
    if (client_.supports_variable_type_) {
      response["body"]["type"] = resultType;
    }
    response["body"]["variablesReference"] = variablesReference;
    SendResponse(std::move(response));
  }

  void ExecuteSetVariableCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    ObjectRef parentId = request["arguments"]["variablesReference"].asInt();
    std::string varName = request["arguments"]["name"].asString();
    std::string expression = request["arguments"]["value"].asString();

    DebuggeeState& parent = debuggee_state_.GetObject(parentId);
    if (!parent) {
      return SendResponse(CreateErrorResponse(request, "no such variable"));
    }
    lua_State* L = parent.GetLua();

    DebuggeeState& var = parent.GetVariable(varName);
    if (!var) {
      return SendResponse(CreateErrorResponse(request, "no such variable"));
    }

    int resultCount = LuaEvaluate(expression, parent.GetThread());
    if (resultCount < 0) {
      std::string error = LuaValueToString(L, -1, false);
      lua_pop(L, 1);
      return SendResponse(CreateErrorResponse(request, error));
    }

    if (resultCount > 1) {
      lua_pop(L, resultCount - 1);
    }
    if (resultCount < 1) {
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
    Json::Value threadsJson(Json::arrayValue);
    for (DebuggeeState* thread : debuggee_state_.GetChildren()) {
      if (thread->GetType() != DebuggeeState::Type::Thread) {
        continue;
      }
      Json::Value threadJson;
      threadJson["id"] = thread->GetThreadId();
      threadJson["name"] = "Thread " + std::to_string(thread->GetThreadId());
      threadsJson.append(threadJson);
    }

    if (threadsJson.empty()) {
      Json::Value fakeThread;
      fakeThread["id"] = 0;
      fakeThread["name"] = "No threads";
      threadsJson.append(fakeThread);
    }

    Response result = CreateDapResponse(request);
    result["body"]["threads"] = std::move(threadsJson);
    SendResponse(std::move(result));
  }

  void ExecuteStackTraceCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ThreadId threadId = request["arguments"]["threadId"].asUInt64();
    int startFrame = request["arguments"]["startFrame"].asInt();
    int levels = request["arguments"]["levels"].asInt();
    if (levels <= 0) {
      levels = INT_MAX;
    }

    DebuggeeState& thread = debuggee_state_.GetThread(threadId);
    if (!thread) {
      Response emptyResult = CreateDapResponse(request);
      emptyResult["body"]["stackFrames"] = Json::Value(Json::arrayValue);
      emptyResult["body"]["totalFrames"] = 0;
      SendResponse(std::move(emptyResult));
    }

    Json::Value framesJson = Json::Value(Json::arrayValue);
    for (size_t i = startFrame;
         i < thread.GetChildren().size() && framesJson.size() < (size_t)levels;
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

      Json::Value frameJson;
      frameJson["id"] = frame.GetId();
      if (debug.name != nullptr && debug.name[0] != 0) {
        frameJson["name"] = debug.name;
      } else if (debug.what != nullptr && strcmp(debug.what, "Lua") == 0) {
        frameJson["name"] = "<Lua function>";
      } else if (debug.what != nullptr && strcmp(debug.what, "C") == 0) {
        frameJson["name"] = "<C function>";
      } else if (debug.what != nullptr && strcmp(debug.what, "C") == 0) {
        frameJson["name"] = "<main>";
      } else if (debug.what != nullptr && strcmp(debug.what, "tail") == 0) {
        frameJson["name"] = "<tail call>";
        frameJson["presentationHint"] = "label";
      } else {
        frameJson["name"] = "<unknown>";
      }

      std::string source = debug.source ? ResolvePath(debug.source) : "";
      if (!source.empty()) {
        frameJson["source"]["name"] = debug.short_src;
        frameJson["source"]["path"] = source;
      }

      if (debug.currentline > 0) {
        frameJson["line"] = debug.currentline + client_.lines_offset_;
      } else {
        frameJson["line"] = 0;
      }

      frameJson["column"] = 0;
      framesJson.append(frameJson);
    }

    Response result = CreateDapResponse(request);
    result["body"]["stackFrames"] = framesJson;
    result["body"]["totalFrames"] = (int)thread.GetChildren().size();
    SendResponse(std::move(result));
  }

  void ExecuteScopesCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ObjectRef frameId = request["arguments"]["frameId"].asInt();

    DebuggeeState& frame = debuggee_state_.GetObject(frameId);
    if (frame.GetType() != DebuggeeState::Type::StackFrame) {
      Response emptyResult = CreateDapResponse(request);
      emptyResult["body"]["scopes"] = Json::Value(Json::arrayValue);
      return SendResponse(std::move(emptyResult));
    }

    Json::Value scopesJson(Json::arrayValue);

    DebuggeeState& locals = frame.GetLocals();
    if (locals) {
      Json::Value localsJson;
      localsJson["variablesReference"] = locals.GetId();
      localsJson["name"] = "Locals";
      localsJson["expensive"] = false;
      localsJson["presentationHint"] = "locals";
      scopesJson.append(localsJson);
    }

    DebuggeeState& upvalues = frame.GetUpvalues();
    if (upvalues) {
      Json::Value upvaluesJson;
      upvaluesJson["variablesReference"] = upvalues.GetId();
      upvaluesJson["name"] = "Upvalues";
      upvaluesJson["expensive"] = false;
      scopesJson.append(upvaluesJson);
    }

    DebuggeeState& globals = frame.GetGlobals();
    if (globals) {
      Json::Value globalsJson;
      globalsJson["variablesReference"] = globals.GetId();
      globalsJson["name"] = "Globals";
      globalsJson["expensive"] = true;
      scopesJson.append(globalsJson);
    }

    Response result = CreateDapResponse(request);
    result["body"]["scopes"] = std::move(scopesJson);
    SendResponse(std::move(result));
  }

  void ExecuteVariablesCommand(const Request& request) {
    if (!is_paused_) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ObjectRef parentId = request["arguments"]["variablesReference"].asInt();

    DebuggeeState& parent = debuggee_state_.GetObject(parentId);
    if (!parent.IsVariablesContainer()) {
      return SendResponse(CreateErrorResponse(request, "no such variable"));
    }

    bool skipNamed = request["arguments"]["filter"].asString() == "indexed";
    bool skipIndexed = parent.IsScope() &&
                       request["arguments"]["filter"].asString() == "named";

    Json::Value varsJson(Json::arrayValue);
    for (DebuggeeState* var : parent.GetChildren()) {
      if (!var->IsVariable()) {
        continue;
      }
      if (skipNamed && var->GetType() != DebuggeeState::Type::IndexedVariable) {
        continue;
      }
      if (skipIndexed &&
          var->GetType() == DebuggeeState::Type::IndexedVariable) {
        continue;
      }
      if (!var->PushValue()) {
        continue;
      }

      lua_State* L = var->GetLua();
      int luaType = lua_type(L, -1);

      Json::Value varJson;
      varJson["name"] = var->GetName();
      varJson["value"] = LuaValueToString(L, -1);
      if (client_.supports_variable_type_) {
        varJson["type"] = lua_typename(L, luaType);
      }
      varJson["variablesReference"] = luaType == LUA_TTABLE ? var->GetId() : 0;

      if (var->GetType() == DebuggeeState::Type::MetaTableVariable) {
        varJson["presentationHint"]["kind"] = "virtual";
      } else if (parent.IsScope() && var->IsTemporaryVariable()) {
        varJson["presentationHint"]["kind"] = "virtual";
      } else if (luaType == LUA_TFUNCTION) {
        varJson["presentationHint"]["kind"] = "method";
      } else if (parent.IsScope()) {
        varJson["presentationHint"]["kind"] = "property";
      }

      varsJson.append(varJson);

      lua_pop(L, 1);
    }

    Response result = CreateDapResponse(request);
    result["body"]["variables"] = std::move(varsJson);
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

    int fullMask = LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE;
    int mask = 0;
    for (Breakpoint& bp : breakpoints_) {
      mask |= bp.GetEventMask();
      if (mask == fullMask) {
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

    bool anyDeleted = false;
    for (int i = breakpoints_.size() - 1; i >= 0; i--) {
      if (breakpoints_[i].GetFileName() == source) {
        breakpoints_.erase(breakpoints_.begin() + i);
        anyDeleted = true;
      }
    }
    if (anyDeleted) {
      UpdateHook();
    }
  }

  void DeleteAllFunctionBreakpoints() {
    LockMutex lock(lua_debug_mutex_);
    std::lock_guard lock2(breakpoints_lock_);

    bool anyDeleted = false;
    for (int i = breakpoints_.size() - 1; i >= 0; i--) {
      if (!breakpoints_[i].GetFunctionName().empty()) {
        breakpoints_.erase(breakpoints_.begin() + i);
        anyDeleted = true;
      }
    }
    if (anyDeleted) {
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
    bool isAnyHit = false;
    if (breakpoints_lock_.try_lock()) {
      for (Breakpoint& bp : breakpoints_) {
        if (bp.IsHit(L, *ar)) {
          isAnyHit = true;
          break;
        }
      }
      breakpoints_lock_.unlock();
      if (!isAnyHit) {
        return;
      }
    }

    LockMutex lock(lua_debug_mutex_);

    // Check again after acquiring m_luaDebugMutex. The breakpoints could have
    // changed between releasing m_breakpointsLock and acquiring
    // m_luaDebugMutex, or if we failed to acquire m_breakpointsLock at all.
    isAnyHit = false;
    for (Breakpoint& bp : breakpoints_) {
      if (bp.IsHit(L, *ar)) {
        isAnyHit = true;
        break;
      }
    }
    if (!isAnyHit) {
      return;
    }

    std::string source = ar->source ? ResolvePath(ar->source) : "";
    std::string sourceName = !source.empty() ? ar->short_src : "";
    int lineNumber =
        ar->currentline > 0 ? ar->currentline + client_.lines_offset_ : -1;

    DebuggeeState& currentStackFrame =
        debuggee_state_.GetThread(L).GetStackFrame(0);
    current_thread_ = currentStackFrame.GetThreadId();

    std::string reason;
    std::string evaluationErrors;
    std::vector<BreakpointId> hitBreakpointIds;
    bool anyDeleted = false;
    for (size_t i = 0; i < breakpoints_.size(); i++) {
      Breakpoint& bp = breakpoints_[i];
      if (!bp.IsHit(L, *ar)) {
        continue;
      }

      std::string evalError;
      if (!bp.EvaluateConditions(currentStackFrame, evalError)) {
        continue;
      }
      if (!evalError.empty()) {
        is_paused_ = true;
        if (!evaluationErrors.empty()) {
          evaluationErrors.append("\n");
        }
        evaluationErrors.append(evalError);
        if (bp.GetId() != 0) {
          hitBreakpointIds.push_back(bp.GetId());
        }
        continue;
      }

      if (bp.IsBreak()) {
        is_paused_ = true;
        reason = bp.GetReason();
        if (bp.GetId() != 0) {
          hitBreakpointIds.push_back(bp.GetId());
        }
      }

      std::string logError;
      std::string logMessage =
          bp.EvaluateLogMessage(currentStackFrame, logError);
      if (!logError.empty()) {
        is_paused_ = true;
        if (!evaluationErrors.empty()) {
          evaluationErrors.append("\n");
        }
        evaluationErrors.append(logError);
        if (bp.GetId() != 0) {
          hitBreakpointIds.push_back(bp.GetId());
        }
        continue;
      }

      if (!logMessage.empty()) {
        SendResponse(
            CreateOutputEvent(logMessage, sourceName, source, lineNumber));
      }

      if (bp.IsTemporary()) {
        breakpoints_.erase(breakpoints_.begin() + i);
        i--;
        anyDeleted = true;
      }
    }

    if (anyDeleted) {
      UpdateHook();
    }

    if (is_paused_) {
      if (!evaluationErrors.empty()) {
        SendResponse(CreateBreakpointEvaluationErrorEvent(
            current_thread_, evaluationErrors, hitBreakpointIds));
      } else {
        SendResponse(
            CreateStoppedEvent(current_thread_, reason, hitBreakpointIds));
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
