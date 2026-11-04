#include "LightsDriver_HidBlueDot.h"

#include "GameInput.h"
#include "LightsManager.h"
#include "PlayerNumber.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(HidBlueDot);

LightsDriver_HidBlueDot::LightsDriver_HidBlueDot()
    : cab_data_{0}, pad_data_{0}, dev_{VID, PID} {
  cab_data_[1] = 0x01;
  pad_data_[1] = 0x02;
}

LightsDriver_HidBlueDot::~LightsDriver_HidBlueDot() {}

void LightsDriver_HidBlueDot::SetCabinetLight(
    CabinetLightIndex index, bool value) {
  SetLight((unsigned char*)cab_data_, (int)index, value);
};

void LightsDriver_HidBlueDot::SetPadLight(PadLightIndex index, bool value) {
  SetLight((unsigned char*)pad_data_, (int)index, value);
};

void LightsDriver_HidBlueDot::SetLight(
    unsigned char* buffer, int index, bool value) {
  buffer[2] &= (unsigned char)~(1 << index);

  if (value) {
    buffer[2] |= (unsigned char)(1 << index);
  }
};

void LightsDriver_HidBlueDot::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  //! Set cabinet lights.
  SetCabinetLight(m_Marquee_UpLeft, ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT]);
  SetCabinetLight(
      m_Marquee_UpRight, ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT]);
  SetCabinetLight(m_Marquee_LwLeft, ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT]);
  SetCabinetLight(
      m_Marquee_LwRight, ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT]);
  SetCabinetLight(
      m_Buttons_Left, ls->game_button_lights_[PLAYER_1][GAME_BUTTON_START]);
  SetCabinetLight(
      m_Buttons_Right, ls->game_button_lights_[PLAYER_2][GAME_BUTTON_START]);
  SetCabinetLight(
      m_Bass, ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                  ls->cabinet_lights_[LIGHT_BASS_RIGHT]);

  dev_.Write((unsigned char*)&cab_data_, 3);

  //! Set pad lights.
  SetPadLight(
      m_PadP1_Left, ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_LEFT]);
  SetPadLight(
      m_PadP1_Right, ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_RIGHT]);
  SetPadLight(m_PadP1_Up, ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_UP]);
  SetPadLight(
      m_PadP1_Down, ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_DOWN]);
  SetPadLight(
      m_PadP2_Left, ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_LEFT]);
  SetPadLight(
      m_PadP2_Right, ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_RIGHT]);
  SetPadLight(m_PadP2_Up, ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_UP]);
  SetPadLight(
      m_PadP2_Down, ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_DOWN]);

  dev_.Write((unsigned char*)&pad_data_, 3);
}
