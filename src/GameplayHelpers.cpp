#include <vector>

#include "global.h"

#include "EnumHelper.h"
#include "GameplayHelpers.h"
#include "GameState.h"
#include "LuaManager.h"
#include "ThemeManager.h"
#include "Style.h"

std::vector<NotefieldMargins> GetNotefieldMargins() {
	LuaReference margin_func;
	std::vector<NotefieldMargins> margins(2);

	THEME->GetMetric("ScreenGameplay", "MarginFunction", margin_func);
	if (margin_func.GetLuaType() != LUA_TFUNCTION)
	{
		LuaHelpers::ReportScriptErrorFmt("MarginFunction metric for %s must be a function.", "ScreenGameplay");
		return margins;
	}

	// Setup the lua.
	Lua* lua_ptr = LUA->Get();
	margin_func.PushSelf(lua_ptr);
	lua_createtable(lua_ptr, 0, 0);
	int next_player_slot = 1;
	FOREACH_EnabledPlayer(pn)
	{
		Enum::Push(lua_ptr, pn);
		lua_rawseti(lua_ptr, -2, next_player_slot);
		++next_player_slot;
	}

	Enum::Push(lua_ptr, GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StyleType);
	RString err = "Error running MarginFunction: ";

	// Run the lua code.
	if (LuaHelpers::RunScriptOnStack(lua_ptr, /*Error=*/err, /*Args=*/2, /*ReturnValues*/3, /*ReportOnError*/true))
	{
		std::string margin_error_msg = "Margin value must be a number.";

		// Pull the values off the lua stack. Note that the lua function is
		// returning the margins of P1 + P2 combined. Therefore, we need to use
		// the center to tell where P1's margin ends and P2's begins, if
		// applicable.
		margins[PLAYER_1].left = SafeFArg(lua_ptr, -3, margin_error_msg, 40);
		float center = SafeFArg(lua_ptr, -2, margin_error_msg, 80);
		margins[PLAYER_1].right = center / 2.0f;

		margins[PLAYER_2].left = center / 2.0f;
		margins[PLAYER_2].right = SafeFArg(lua_ptr, -1, margin_error_msg, 40);
	}
	lua_settop(lua_ptr, 0);
	LUA->Release(lua_ptr);
	return margins;
}
