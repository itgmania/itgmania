#include "LightsDriver_Linux_Leds.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <cstring>

#include "EnumHelper.h"
#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "LightsManager.h"
#include "RageLog.h"
#include "StdString.h"

bool LightsDriver_Linux_Leds::WriteLight(const char* filename, bool state) {
  // if we are setting a light that doesn't exist, let the caller know the
  // function was a "success" nullptr is used to define that the stepmania light
  // is not mapped by the caller.
  if (filename == nullptr) {
    return true;
  }

  // LOG->Trace("LED: %s -> %d", filename, state);

  FILE* f = fopen(filename, "w");

  // if the filename should exist, but doesn't, let the caller know of the
  // failure (if desired) and log it.
  if (f == nullptr) {
    LOG->Warn(
        "Failed to set Linux_Led at %s. Check device permissions or udev "
        "rules. errno: %d: %s",
        filename, errno, strerror(errno));
    return false;
  }

  // write and close the file.
  fprintf(f, "%d", state ? kLinuxLedStateOn : kLinuxLedStateOff);
  fclose(f);

  return true;
}

bool LightsDriver_Linux_Leds::IsDance() {
  input_ = &GAMESTATE->GetCurrentGame()->input_scheme_;
  input_name_ = input_->name_;

  return EqualsNoCase(input_name_, "dance");
}

bool LightsDriver_Linux_Leds::IsPump() {
  input_ = &GAMESTATE->GetCurrentGame()->input_scheme_;
  input_name_ = input_->name_;

  return EqualsNoCase(input_name_, "pump");
}

void LightsDriver_Linux_Leds::SetLight(
    const char* filename, bool previous, bool desired) {
  // don't overload the linux system if the light has not been changed.
  if (previous != desired) {
    WriteLight(filename, desired);
  }
}

void LightsDriver_Linux_Leds::SetCabinetLights(
    const int int_array[], const LightsState* ls) {
  const char* base_file_location = GetGameControllerLightFile();

  if (base_file_location != nullptr) {
    char file_name[kLinuxLedMaxDirectoryLength];

    FOREACH_CabinetLight(light) {
      // valid lights are zero and positive.
      if (int_array[light] >= 0) {
        // don't waste sprintf time if we don't need to change the light
        if (previous_ls_.cabinet_lights_[light] != ls->cabinet_lights_[light]) {
          sprintf(file_name, base_file_location, int_array[light]);
          SetLight(
              file_name, previous_ls_.cabinet_lights_[light],
              ls->cabinet_lights_[light]);
        }
      }
    }
  }
}

void LightsDriver_Linux_Leds::SetCoinCounter(int index, const LightsState* ls) {
  const char* base_file_location = GetGameControllerLightFile();

  if (base_file_location != nullptr && index >= 0) {
    char file_name[kLinuxLedMaxDirectoryLength];

    if (previous_ls_.coin_counter_ != ls->coin_counter_) {
      sprintf(file_name, base_file_location, index);
      SetLight(file_name, previous_ls_.coin_counter_, ls->coin_counter_);
    }
  }
}

void LightsDriver_Linux_Leds::SetGameControllerLights(
    GameController gc, const int int_array[], const LightsState* ls) {
  const char* base_file_location = GetGameControllerLightFile();

  if (base_file_location != nullptr) {
    char file_name[kLinuxLedMaxDirectoryLength];

    // iterate over all gamebuttons, including the menu/start/etc buttons.
    FOREACH_ENUM(GameButton, gb) {
      // valid lights are zero and positive.
      if (int_array[gb] >= 0) {
        // don't waste sprintf time if we don't need to change the light
        if (previous_ls_.game_button_lights_[gc][gb] !=
            ls->game_button_lights_[gc][gb]) {
          sprintf(file_name, base_file_location, int_array[gb]);
          SetLight(
              file_name, previous_ls_.game_button_lights_[gc][gb],
              ls->game_button_lights_[gc][gb]);
        }
      }
    }
  }
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
