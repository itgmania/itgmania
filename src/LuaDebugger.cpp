#include "LuaDebugger.h"

#include <queue>
#include <string>
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
class Debugger::Impl {
 public:
  Impl() : m_luaDebugMutex("LuaDebug"), m_debuggeeState(DebuggeeState::Root{}) {
    ASSERT(g_debugger == nullptr);
    g_debugger = this;
  }

  ~Impl() {
    // Clear breakpoints and continue execution.
    LockMutex lock(m_luaDebugMutex);

    DeleteAllBreakpoints();

    if (m_isPaused) {
      SendRequest(CreateContinueRequest(0));
    }

    g_debugger = nullptr;
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void ActivateState(lua_State* lua) {
    LockMutex lock(m_luaDebugMutex);

    LuaThread* thread = nullptr;
    for (LuaThread& t : m_threads) {
      if (t.m_lua == lua) {
        thread = &t;
        break;
      }
    }

    if (thread == nullptr) {
      LuaThread newThread{m_nextThreadId++, lua, true};
      m_threads.push_back(newThread);
      thread = &m_threads.back();
      SendResponse(CreateThreadStartedEvent(thread->m_threadId));
    }

    lua_sethook(lua, m_luaHook, m_luaHookMask, 0);

    thread->m_active = true;
    m_debuggeeState.AddThread(thread->m_threadId, lua);
  }

  void DeactivateState(lua_State* lua) {
    LockMutex lock(m_luaDebugMutex);

    lua_sethook(lua, nullptr, 0, 0);

    for (int i = m_breakpoints.size() - 1; i >= 0; i--) {
      if (m_breakpoints[i].GetThread() == lua) {
        m_breakpoints.erase(m_breakpoints.begin() + i);
      }
    }

    for (size_t i = 0; i < m_threads.size(); i++) {
      if (m_threads[i].m_lua == lua) {
        m_threads[i].m_active = false;
        m_debuggeeState.RemoveThread(m_threads[i].m_threadId);
      }
    }
  }

  void SendRequest(Request&& request) {
    bool isLocked = m_luaDebugMutex.IsLockedByThisThread();
    if (!isLocked) {
      m_luaDebugMutex.Lock();
    }
    if (m_isPaused) {
      m_toLuaThread.push(request);
      m_luaDebugMutex.Broadcast();
    } else {
      HandleRequest(request);
    }
    if (!isLocked) {
      m_luaDebugMutex.Unlock();
    }
  }

  bool TryReceiveResponse(Response& response) {
    LockMutex lock(m_luaDebugMutex);
    if (m_toServerThread.empty()) {
      return false;
    }
    response = std::move(m_toServerThread.front());
    m_toServerThread.pop();
    return true;
  }

  void SendResponse(Response&& response) {
    bool isLocked = m_luaDebugMutex.IsLockedByThisThread();
    if (!isLocked) {
      m_luaDebugMutex.Lock();
    }
    m_toServerThread.push(std::move(response));
    m_luaDebugMutex.Broadcast();
    if (!isLocked) {
      m_luaDebugMutex.Unlock();
    }
  }

  bool TryReceiveRequest(Request& request) {
    LockMutex lock(m_luaDebugMutex);
    if (m_toLuaThread.empty()) {
      return false;
    }
    request = m_toLuaThread.front();
    m_toLuaThread.pop();
    return true;
  }

  void ReceiveRequest(Request& request) {
    bool isLocked = m_luaDebugMutex.IsLockedByThisThread();
    if (!isLocked) {
      m_luaDebugMutex.Lock();
    }
    while (!TryReceiveRequest(request)) {
      m_luaDebugMutex.Wait();
    }
    if (!isLocked) {
      m_luaDebugMutex.Unlock();
    }
  }

 private:
  struct LuaThread {
    ThreadId m_threadId;
    lua_State* m_lua;
    bool m_active;
  };

  // Locks access to the shared data. Signaled when m_toLuaThread or
  // m_toServerThread is available for reading.
  RageEvent m_luaDebugMutex;

  // Kept across client sessions
  std::vector<LuaThread> m_threads;
  ThreadId m_nextThreadId = 1;
  BreakpointId m_nextBreakpointId = 1;
  lua_Hook m_luaHook = nullptr;
  int m_luaHookMask = 0;
  std::vector<Breakpoint> m_breakpoints;

  // Client specific
  struct {
    int linesOffset = 0;
    int columnsOffset = 0;
    bool supportsVariableType = false;
    bool supportsInvalidatedEvent = false;
    SeqNo nextMessageSeq = 1;
  } m_client;

  // State when execution is paused
  bool m_isPaused = false;
  ThreadId m_currentThread = 0;
  DebuggeeState m_debuggeeState;

  // Communication between the Lua thread and the server thread.
  std::queue<Request> m_toLuaThread;
  std::queue<Response> m_toServerThread;

  void ContinueExecution() { m_isPaused = false; }

  void ExecuteUnknownCommand(const Request& request) {
    SendResponse(CreateErrorResponse(request, "unknown command"));
  }

  void ExecuteInitializeCommand(const Request& request) {
    const Json::Value& args = request["arguments"];
    m_client.linesOffset =
        args["linesStartAt1"].isBool() && !args["linesStartAt1"].asBool() ? -1
                                                                          : 0;
    m_client.columnsOffset =
        args["columnsStartAt1"].isBool() && !args["columnsStartAt1"].asBool()
            ? -1
            : 0;
    m_client.supportsVariableType = args["supportsVariableType"].asBool();
    m_client.supportsInvalidatedEvent =
        args["supportsInvalidatedEvent"].asBool();

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
    if (m_isPaused) {
      SendResponse(CreateStoppedEvent(m_currentThread, "entry", {}));
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
        lineNumber -= m_client.linesOffset;
      }
      if (lineNumber < 0) {
        lineNumber = 0;
      }
      lineNumbers.push_back(lineNumber);
      newBreakpointIds.push_back(m_nextBreakpointId++);
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
      bpJson["line"] = lineNumbers[i] + m_client.linesOffset;
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
          m_nextBreakpointId++, breakpoints[i]["name"].asString());
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

    if (!m_isPaused) {
      AddBreakpoint(Breakpoint::Pause());
    } else {
      SendResponse(CreateStoppedEvent(m_currentThread, "pause", {}));
    }
  }

  void ExecuteNextCommand(const Request& request) {
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    DebuggeeState& thread = m_debuggeeState.GetThread(m_currentThread);
    AddBreakpoint(Breakpoint::Next(thread.GetLua()));
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteStepInCommand(const Request& request) {
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    DebuggeeState& thread = m_debuggeeState.GetThread(m_currentThread);
    AddBreakpoint(Breakpoint::Step(thread.GetLua()));
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteStepOutCommand(const Request& request) {
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    DebuggeeState& thread = m_debuggeeState.GetThread(m_currentThread);
    AddBreakpoint(Breakpoint::Return(thread.GetLua()));
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
  }

  void ExecuteContinueCommand(const Request& request) {
    ContinueExecution();
    SendResponse(CreateDapResponse(request));
    if (!m_isPaused) {
      SendResponse(CreateContinuedEvent(0));
    }
  }

  void ExecuteDisconnectCommand(const Request& request) {
    DeleteAllBreakpoints();

    bool suspendDebuggee = request["arguments"]["suspendDebuggee"].asBool();
    if (m_isPaused && !suspendDebuggee) {
      ContinueExecution();
    }

    m_client.linesOffset = 0;
    m_client.columnsOffset = 0;
    m_client.supportsVariableType = false;
    m_client.supportsInvalidatedEvent = false;
    m_client.nextMessageSeq = 1;

    SendResponse(CreateDapResponse(request));
  }

  void ExecuteEvaluateCommand(const Request& request) {
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    std::string expression = request["arguments"]["expression"].asString();
    ObjectRef frameId = request["arguments"]["frameId"].asInt();

    DebuggeeState& frameOrThread =
        frameId ? m_debuggeeState.GetObject(frameId)
                : m_debuggeeState.GetThread(m_currentThread);
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
    if (m_client.supportsVariableType) {
      response["body"]["type"] = resultType;
    }
    response["body"]["variablesReference"] = variablesReference;
    SendResponse(std::move(response));
  }

  void ExecuteSetVariableCommand(const Request& request) {
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }
    ObjectRef parentId = request["arguments"]["variablesReference"].asInt();
    std::string varName = request["arguments"]["name"].asString();
    std::string expression = request["arguments"]["value"].asString();

    DebuggeeState& parent = m_debuggeeState.GetObject(parentId);
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
    if (m_client.supportsVariableType) {
      response["body"]["type"] = type;
    }
    if (luaType == LUA_TTABLE) {
      response["body"]["variablesReference"] = var.GetId();
    }
    SendResponse(std::move(response));
  }

  void ExecuteThreadsCommand(const Request& request) {
    Json::Value threadsJson(Json::arrayValue);
    for (DebuggeeState* thread : m_debuggeeState.GetChildren()) {
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
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ThreadId threadId = request["arguments"]["threadId"].asUInt64();
    int startFrame = request["arguments"]["startFrame"].asInt();
    int levels = request["arguments"]["levels"].asInt();
    if (levels <= 0) {
      levels = INT_MAX;
    }

    DebuggeeState& thread = m_debuggeeState.GetThread(threadId);
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
        frameJson["line"] = debug.currentline + m_client.linesOffset;
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
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ObjectRef frameId = request["arguments"]["frameId"].asInt();

    DebuggeeState& frame = m_debuggeeState.GetObject(frameId);
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
    if (!m_isPaused) {
      return SendResponse(CreateNotStoppedResponse(request));
    }

    ObjectRef parentId = request["arguments"]["variablesReference"].asInt();

    DebuggeeState& parent = m_debuggeeState.GetObject(parentId);
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
      if (m_client.supportsVariableType) {
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
    ASSERT(m_luaDebugMutex.IsLockedByThisThread());

    int fullMask = LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE;
    int mask = 0;
    for (Breakpoint& bp : m_breakpoints) {
      mask |= bp.GetEventMask();
      if (mask == fullMask) {
        break;
      }
    }

    if (m_luaHookMask != mask) {
      m_luaHook = mask == 0 ? nullptr : LuaHook_Static;
      m_luaHookMask = mask;
      for (LuaThread& thread : m_threads) {
        if (thread.m_active) {
          lua_sethook(thread.m_lua, m_luaHook, m_luaHookMask, 0);
        }
      }
    }
  }

  void AddBreakpoint(Breakpoint breakpoint) {
    LockMutex lock(m_luaDebugMutex);
    m_breakpoints.push_back(breakpoint);
    UpdateHook();
  }

  void DeleteBreakpointsInSource(std::string source) {
    LockMutex lock(m_luaDebugMutex);
    bool anyDeleted = false;
    for (int i = m_breakpoints.size() - 1; i >= 0; i--) {
      if (m_breakpoints[i].GetFileName() == source) {
        m_breakpoints.erase(m_breakpoints.begin() + i);
        anyDeleted = true;
      }
    }
    if (anyDeleted) {
      UpdateHook();
    }
  }

  void DeleteAllFunctionBreakpoints() {
    LockMutex lock(m_luaDebugMutex);
    bool anyDeleted = false;
    for (int i = m_breakpoints.size() - 1; i >= 0; i--) {
      if (!m_breakpoints[i].GetFunctionName().empty()) {
        m_breakpoints.erase(m_breakpoints.begin() + i);
        anyDeleted = true;
      }
    }
    if (anyDeleted) {
      UpdateHook();
    }
  }

  void DeleteAllBreakpoints() {
    LockMutex lock(m_luaDebugMutex);
    m_breakpoints.clear();
    UpdateHook();
  }

  void LuaHook(lua_State* L, lua_Debug* ar) {
    LockMutex lock(m_luaDebugMutex);

    lua_getinfo(L, "lnS", ar);
    bool anyHit = false;
    for (Breakpoint& bp : m_breakpoints) {
      if (bp.IsHit(L, *ar)) {
        anyHit = true;
        break;
      }
    }
    if (!anyHit) {
      return;
    }

    std::string source = ar->source ? ResolvePath(ar->source) : "";
    std::string sourceName = !source.empty() ? ar->short_src : "";
    int lineNumber =
        ar->currentline > 0 ? ar->currentline + m_client.linesOffset : -1;

    DebuggeeState& currentStackFrame =
        m_debuggeeState.GetThread(L).GetStackFrame(0);
    m_currentThread = currentStackFrame.GetThreadId();

    std::string reason;
    std::string evaluationErrors;
    std::vector<BreakpointId> hitBreakpointIds;
    bool anyDeleted = false;
    for (size_t i = 0; i < m_breakpoints.size(); i++) {
      Breakpoint& bp = m_breakpoints[i];
      if (!bp.IsHit(L, *ar)) {
        continue;
      }

      std::string evalError;
      if (!bp.EvaluateConditions(currentStackFrame, evalError)) {
        continue;
      }
      if (!evalError.empty()) {
        m_isPaused = true;
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
        m_isPaused = true;
        reason = bp.GetReason();
        if (bp.GetId() != 0) {
          hitBreakpointIds.push_back(bp.GetId());
        }
      }

      std::string logError;
      std::string logMessage =
          bp.EvaluateLogMessage(currentStackFrame, logError);
      if (!logError.empty()) {
        m_isPaused = true;
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
        m_breakpoints.erase(m_breakpoints.begin() + i);
        i--;
        anyDeleted = true;
      }
    }

    if (anyDeleted) {
      UpdateHook();
    }

    if (m_isPaused) {
      if (!evaluationErrors.empty()) {
        SendResponse(CreateBreakpointEvaluationErrorEvent(
            m_currentThread, evaluationErrors, hitBreakpointIds));
      } else {
        SendResponse(
            CreateStoppedEvent(m_currentThread, reason, hitBreakpointIds));
      }

      while (m_isPaused) {
        Request request;
        ReceiveRequest(request);
        HandleRequest(request);
      }
      SendResponse(CreateContinuedEvent(m_currentThread));
    }

    m_currentThread = 0;
    m_debuggeeState.Invalidate();
  }

  // Need to use a static variable to pass the debugger to the Lua hook.
  static Debugger::Impl* g_debugger;

  static void LuaHook_Static(lua_State* L, lua_Debug* debug) {
    ASSERT(g_debugger != nullptr);
    g_debugger->LuaHook(L, debug);
  }
};

Debugger::Impl* Debugger::Impl::g_debugger;

Debugger::Debugger() : m_impl(new Debugger::Impl()) {}
Debugger::~Debugger() = default;

void Debugger::ActivateState(lua_State* lua) { m_impl->ActivateState(lua); }
void Debugger::DeactivateState(lua_State* lua) { m_impl->DeactivateState(lua); }
void Debugger::SendRequest(Request&& request) {
  m_impl->SendRequest(std::move(request));
}
bool Debugger::TryReceiveResponse(Response& response) {
  return m_impl->TryReceiveResponse(response);
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
