#include "TechCountsCategory.h"

#include "EnumHelper.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "global.h"

static const char* TechCountsCategoryNames[] = {
    "Crossovers",     "HalfCrossovers",   "FullCrossovers", "Footswitches",
    "UpFootswitches", "DownFootswitches", "Sideswitches",   "Jacks",
    "Brackets",       "Doublesteps"};

XToString(TechCountsCategory);
XToLocalizedString(TechCountsCategory);
LuaFunction(
    TechCountsCategoryToLocalizedString,
    TechCountsCategoryToLocalizedString(Enum::Check<TechCountsCategory>(L, 1)));
LuaXType(TechCountsCategory);
