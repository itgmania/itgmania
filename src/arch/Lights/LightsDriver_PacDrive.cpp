#include "LightsDriver_PacDrive.h"

#include <cstring>
#include <string>

#include "GameInput.h"
#include "LightsManager.h"
#include "Preference.h"
#include "StdString.h"
#include "arch/Lights/LightsDriver.h"
#include "archutils/Common/HidDevice.h"

REGISTER_LIGHTS_DRIVER_CLASS(PacDrive);

static Preference<std::string> pac_drive_light_ordering(
    "PacDriveLightOrdering", "openitg");
int pac_drive_light_order = 0;

LightsDriver_PacDrive::LightsDriver_PacDrive()
    : dev_{
          PACDRIVE_VID, make_pids(PACDRIVE_PID, PACDRIVE_PID_MAX),
          PACDRIVE_INTERFACE} {
  prev_led_state_.raw_ = 0;
  memset(state_.raw_state_, 0x00, sizeof(state_.raw_state_));

  std::string light_order = pac_drive_light_ordering.Get();
  if (CompareNoCase(light_order, "lumenar") == 0 ||
      CompareNoCase(light_order, "openitg") == 0) {
    pac_drive_light_order = 1;
  }
}

LightsDriver_PacDrive::~LightsDriver_PacDrive() {}

void LightsDriver_PacDrive::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  switch (pac_drive_light_order) {
    case 1:
      /*
       * OpenITG (LumenAR) Order:
       * http://solid-orange.com/wp-content/uploads/2014/02/ddr_oitg_pacdrive_pins.gif
       * Note: The bit order in OpenITG is byte swapped as PacDrive's vendor
       * library byte swaps the values.
       *
       * This listing mirrors the PHYSICAL pins each of the lights comes out.
       *
       * 01: P1 Left
       * 02: P1 Right
       * 03: P1 Up
       * 04: P1 Down
       * 05: P2 Left
       * 06: P2 Right
       * 07: P2 Up
       * 08: P2 Down
       * 09: Marquee UL
       * 10: Marquee UR
       * 11: Marquee LL
       * 12: Marquee LR
       * 13: P1 Start
       * 14: P2 Start
       * 15: Bass Left
       * 16: Bass Right
       */

      state_.leds_.led01_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_LEFT];
      state_.leds_.led02_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_RIGHT];
      state_.leds_.led03_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_UP];
      state_.leds_.led04_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_DOWN];

      state_.leds_.led05_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_LEFT];
      state_.leds_.led06_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_RIGHT];
      state_.leds_.led07_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_UP];
      state_.leds_.led08_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_DOWN];

      state_.leds_.led09_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT];
      state_.leds_.led10_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT];
      state_.leds_.led11_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT];
      state_.leds_.led12_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT];

      state_.leds_.led13_ =
          ls->game_button_lights_[GameController_1][GAME_BUTTON_START];
      state_.leds_.led14_ =
          ls->game_button_lights_[GameController_2][GAME_BUTTON_START];

      state_.leds_.led15_ = ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                            ls->cabinet_lights_[LIGHT_BASS_RIGHT];
      state_.leds_.led16_ = ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                            ls->cabinet_lights_[LIGHT_BASS_RIGHT];

      break;

    case 0:
    default:
      /*
       * SM5 order.
       * see pg7 of http://www.peeweepower.com/stepmania/sm509pacdriveinfo.pdf
       *
       * 01: Marquee UL
       * 02: Marquee UR
       * 03: Marquee LL
       * 04: Marquee LR
       * 05: Bass (both)
       * 06: P1 Left
       * 07: P1 Right
       * 08: P1 Up
       * 09: P1 Down
       * 10: P1 Start
       * 11: P2 Left
       * 12: P2 Right
       * 13: P2 Up
       * 14: P2 Down
       * 15: P2 Start
       * 16: (unused)
       */

      state_.leds_.led01_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT];
      state_.leds_.led02_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT];
      state_.leds_.led03_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT];
      state_.leds_.led04_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT];

      state_.leds_.led05_ = ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                            ls->cabinet_lights_[LIGHT_BASS_RIGHT];

      state_.leds_.led06_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_LEFT];
      state_.leds_.led07_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_RIGHT];
      state_.leds_.led08_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_UP];
      state_.leds_.led09_ =
          ls->game_button_lights_[GameController_1][DANCE_BUTTON_DOWN];
      state_.leds_.led10_ =
          ls->game_button_lights_[GameController_1][GAME_BUTTON_START];

      state_.leds_.led11_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_LEFT];
      state_.leds_.led12_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_RIGHT];
      state_.leds_.led13_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_UP];
      state_.leds_.led14_ =
          ls->game_button_lights_[GameController_2][DANCE_BUTTON_DOWN];
      state_.leds_.led15_ =
          ls->game_button_lights_[GameController_2][GAME_BUTTON_START];

      // led16 is not used.
      state_.leds_.led16_ = false;
      break;
  }

  // only push on changes.
  if (state_.leds_.raw_ != prev_led_state_.raw_) {
    state_.report_id_ = PACDRIVE_HIDREPORT_ID;
    state_.pad0_ = 0;
    state_.pad1_ = 0;

    dev_.Write((unsigned char*)&state_.raw_state_, sizeof(state_.raw_state_));
    prev_led_state_ = state_.leds_;
  }
}
