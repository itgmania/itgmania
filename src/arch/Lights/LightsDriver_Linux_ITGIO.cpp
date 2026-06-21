#include "LightsDriver_Linux_ITGIO.h"

#include <fcntl.h>
#include <stdio.h>

#include "GameInput.h"
#include "LightsManager.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS2(ITGIO, Linux_ITGIO);

namespace {
const int coin_counter_index = 14;

const int cabinet_lights[] = {
    8,   // LIGHT_MARQUEE_UP_LEFT
    10,  // LIGHT_MARQUEE_UP_RIGHT
    9,   // LIGHT_MARQUEE_LR_LEFT
    11,  // LIGHT_MARQUEE_LR_RIGHT
    15,  // LIGHT_BASS_LEFT
    15,  // LIGHT_BASS_RIGHT
};

const int player1_lights[] = {
    -1,  // GAME_BUTTON_MENULEFT
    -1,  // GAME_BUTTON_MENURIGHT
    -1,  // GAME_BUTTON_MENUUP
    -1,  // GAME_BUTTON_MENUDOWN
    13,  // GAME_BUTTON_START
    -1,  // GAME_BUTTON_SELECT
    -1,  // GAME_BUTTON_BACK
    -1,  // GAME_BUTTON_RESTART
    -1,  // GAME_BUTTON_COIN
    -1,  // GAME_BUTTON_OPERATOR
    -1,  // GAME_BUTTON_EFFECT_UP
    -1,  // GAME_BUTTON_EFFECT_DOWN
    1,   // GAME_BUTTON_CUSTOM_01
    0,   // GAME_BUTTON_CUSTOM_02
    3,   // GAME_BUTTON_CUSTOM_03
    2,   // GAME_BUTTON_CUSTOM_04
    -1,  // GAME_BUTTON_CUSTOM_05
    -1,  // GAME_BUTTON_CUSTOM_06
    -1,  // GAME_BUTTON_CUSTOM_07
    -1,  // GAME_BUTTON_CUSTOM_08
    -1,  // GAME_BUTTON_CUSTOM_09
    -1,  // GAME_BUTTON_CUSTOM_10
    -1,  // GAME_BUTTON_CUSTOM_11
    -1,  // GAME_BUTTON_CUSTOM_12
    -1,  // GAME_BUTTON_CUSTOM_13
    -1,  // GAME_BUTTON_CUSTOM_14
    -1,  // GAME_BUTTON_CUSTOM_15
    -1,  // GAME_BUTTON_CUSTOM_16
    -1,  // GAME_BUTTON_CUSTOM_17
    -1,  // GAME_BUTTON_CUSTOM_18
    -1,  // GAME_BUTTON_CUSTOM_19
};

const int player2_lights[] = {
    -1,  // GAME_BUTTON_MENULEFT
    -1,  // GAME_BUTTON_MENURIGHT
    -1,  // GAME_BUTTON_MENUUP
    -1,  // GAME_BUTTON_MENUDOWN
    12,  // GAME_BUTTON_START
    -1,  // GAME_BUTTON_SELECT
    -1,  // GAME_BUTTON_BACK
    -1,  // GAME_BUTTON_RESTART
    -1,  // GAME_BUTTON_COIN
    -1,  // GAME_BUTTON_OPERATOR
    -1,  // GAME_BUTTON_EFFECT_UP
    -1,  // GAME_BUTTON_EFFECT_DOWN
    5,   // GAME_BUTTON_CUSTOM_01
    4,   // GAME_BUTTON_CUSTOM_02
    7,   // GAME_BUTTON_CUSTOM_03
    6,   // GAME_BUTTON_CUSTOM_04
    -1,  // GAME_BUTTON_CUSTOM_05
    -1,  // GAME_BUTTON_CUSTOM_06
    -1,  // GAME_BUTTON_CUSTOM_07
    -1,  // GAME_BUTTON_CUSTOM_08
    -1,  // GAME_BUTTON_CUSTOM_09
    -1,  // GAME_BUTTON_CUSTOM_10
    -1,  // GAME_BUTTON_CUSTOM_11
    -1,  // GAME_BUTTON_CUSTOM_12
    -1,  // GAME_BUTTON_CUSTOM_13
    -1,  // GAME_BUTTON_CUSTOM_14
    -1,  // GAME_BUTTON_CUSTOM_15
    -1,  // GAME_BUTTON_CUSTOM_16
    -1,  // GAME_BUTTON_CUSTOM_17
    -1,  // GAME_BUTTON_CUSTOM_18
    -1,  // GAME_BUTTON_CUSTOM_19
};

static_assert(
    ARRAYLEN(cabinet_lights) == NUM_CabinetLight,
    "LightsDriver_Linux_ITGIO ARRAYLEN(cabinet_lights) != NUM_CabinetLight");
static_assert(
    ARRAYLEN(player1_lights) == NUM_GameButton,
    "LightsDriver_Linux_ITGIO ARRAYLEN(player1_lights) != NUM_GameButton");
static_assert(
    ARRAYLEN(player2_lights) == NUM_GameButton,
    "LightsDriver_Linux_ITGIO ARRAYLEN(player2_lights) != NUM_GameButton");

}  // namespace

void LightsDriver_Linux_ITGIO::Set(const LightsState* ls) {
  SetCabinetLights(cabinet_lights, ls);

  SetGameControllerLights(GameController_1, player1_lights, ls);
  SetGameControllerLights(GameController_2, player2_lights, ls);

  SetCoinCounter(coin_counter_index, ls);

  previousLS = *ls;
}

/*
 * (c) 2020 din
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
 *
 * i love lamp
 */
