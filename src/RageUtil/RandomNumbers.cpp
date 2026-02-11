
#include "RandomNumbers.h"

#include <cmath>
#include <random>

#include "LuaBinding.h"
#include "LuaManager.h"

RandomGen g_RandomNumberGenerator;

/* Extend MersenneTwister into Lua space. This is intended to replace
 * math.randomseed and math.random, so we conform to their behavior. */

namespace
{
	MersenneTwister g_LuaPRNG;

	/* To map from [0..2^31-1] to [0..1), we divide by 2^31. */
	const double DIVISOR = std::pow( double(2), double(31) );

	static int Seed( lua_State *L )
	{
		g_LuaPRNG = MersenneTwister( IArg(1) );
		return 0;
	}

	static int Random( lua_State *L )
	{
		switch( lua_gettop(L) )
		{
			/* [0..1) */
			case 0:
			{
				std::uniform_real_distribution<> dist( 0, 1 );
				double r = dist( g_LuaPRNG );
				lua_pushnumber( L, r );
				return 1;
			}

			/* [1..u] */
			case 1:
			{
				int upper = IArg(1);
				luaL_argcheck( L, 1 <= upper, 1, "interval is empty" );
				std::uniform_int_distribution<> dist( 1, upper );
				lua_pushnumber( L, dist( g_LuaPRNG ) );
				return 1;
			}
			/* [l..u] */
			case 2:
			{
				int lower = IArg(1);
				int upper = IArg(2);
				luaL_argcheck( L, lower < upper, 2, "interval is empty" );
				std::uniform_int_distribution<> dist( lower, upper );
				lua_pushnumber( L, dist( g_LuaPRNG ) );
				return 1;
			}

			/* wrong amount of arguments */
			default:
			{
				return luaL_error( L, "wrong number of arguments" );
			}
		}
	}

	const luaL_Reg MersenneTwisterTable[] =
	{
		LIST_METHOD( Seed ),
		LIST_METHOD( Random ),
		{ nullptr, nullptr }
	};
}

LUA_REGISTER_NAMESPACE( MersenneTwister );