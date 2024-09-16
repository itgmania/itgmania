#include "global.h"
#include "EmergencyShutdown.h"
#include "RageUtil.h"

typedef void (*Callback)();
static Callback g_pEmergencyFunc[5];
static unsigned g_iNumEmergencyFuncs = 0;

void RegisterEmergencyShutdownCallback( void (*pFunc)() )
{
	unsigned iArrayLen = ArrayLenUnsigned(g_pEmergencyFunc);
	ASSERT( g_iNumEmergencyFuncs+1 < iArrayLen );
	g_pEmergencyFunc[ g_iNumEmergencyFuncs++ ] = pFunc;
}

void DoEmergencyShutdown()
{
	for( unsigned i = 0; i < g_iNumEmergencyFuncs; ++i )
		g_pEmergencyFunc[i]();
}

