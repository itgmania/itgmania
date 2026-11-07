#include "ScreenSelectCharacter.h"

#include <vector>

#include "AnnouncerManager.h"
#include "Banner.h"
#include "Character.h"
#include "CharacterManager.h"
#include "GameConstantsAndTypes.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "ThemeManager.h"
#include "global.h"

#define TITLE_ON_COMMAND(p) \
  THEME->GetMetricA(        \
      "ScreenSelectCharacter", ssprintf("TitleP%dOnCommand", p + 1))
#define TITLE_OFF_COMMAND(p) \
  THEME->GetMetricA(         \
      "ScreenSelectCharacter", ssprintf("TitleP%dOffCommand", p + 1))
#define CARD_ON_COMMAND(p) \
  THEME->GetMetricA(       \
      "ScreenSelectCharacter", ssprintf("CardP%dOnCommand", p + 1))
#define CARD_OFF_COMMAND(p) \
  THEME->GetMetricA(        \
      "ScreenSelectCharacter", ssprintf("CardP%dOffCommand", p + 1))
#define CARD_ARROWS_ON_COMMAND(p) \
  THEME->GetMetricA(              \
      "ScreenSelectCharacter", ssprintf("CardArrowsP%dOnCommand", p + 1))
#define CARD_ARROWS_OFF_COMMAND(p) \
  THEME->GetMetricA(               \
      "ScreenSelectCharacter", ssprintf("CardArrowsP%dOffCommand", p + 1))
#define EXPLANATION_ON_COMMAND \
  THEME->GetMetricA("ScreenSelectCharacter", "ExplanationOnCommand")
#define EXPLANATION_OFF_COMMAND \
  THEME->GetMetricA("ScreenSelectCharacter", "ExplanationOffCommand")
#define ATTACK_FRAME_ON_COMMAND(p) \
  THEME->GetMetricA(               \
      "ScreenSelectCharacter", ssprintf("AttackFrameP%dOnCommand", p + 1))
#define ATTACK_FRAME_OFF_COMMAND(p) \
  THEME->GetMetricA(                \
      "ScreenSelectCharacter", ssprintf("AttackFrameP%dOffCommand", p + 1))
#define ATTACK_ICON_WIDTH \
  THEME->GetMetricF("ScreenSelectCharacter", "AttackIconWidth")
#define ATTACK_ICON_HEIGHT \
  THEME->GetMetricF("ScreenSelectCharacter", "AttackIconHeight")
#define ATTACK_ICONS_START_X(p) \
  THEME->GetMetricF(            \
      "ScreenSelectCharacter", ssprintf("AttackIconsP%dStartX", p + 1))
#define ATTACK_ICONS_START_Y(p) \
  THEME->GetMetricF(            \
      "ScreenSelectCharacter", ssprintf("AttackIconsP%dStartY", p + 1))
#define ATTACK_ICONS_SPACING_X \
  THEME->GetMetricF("ScreenSelectCharacter", "AttackIconsSpacingX")
#define ATTACK_ICONS_SPACING_Y \
  THEME->GetMetricF("ScreenSelectCharacter", "AttackIconsSpacingY")
#define ATTACK_ICONS_ON_COMMAND(p) \
  THEME->GetMetricA(               \
      "ScreenSelectCharacter", ssprintf("AttackIconsP%dOnCommand", p + 1))
#define ATTACK_ICONS_OFF_COMMAND(p) \
  THEME->GetMetricA(                \
      "ScreenSelectCharacter", ssprintf("AttackIconsP%dOffCommand", p + 1))
#define TIMER_SECONDS THEME->GetMetricI("ScreenSelectCharacter", "TimerSeconds")
#define ICON_WIDTH THEME->GetMetricF("ScreenSelectCharacter", "IconWidth")
#define ICON_HEIGHT THEME->GetMetricF("ScreenSelectCharacter", "IconHeight")
#define ICONS_ON_COMMAND(p) \
  THEME->GetMetricA(        \
      "ScreenSelectCharacter", ssprintf("IconsP%dOnCommand", p + 1))
#define ICONS_OFF_COMMAND(p) \
  THEME->GetMetricA(         \
      "ScreenSelectCharacter", ssprintf("IconsP%dOffCommand", p + 1))

const PlayerNumber CPU_PLAYER[NUM_PLAYERS] = {PLAYER_2, PLAYER_1};

REGISTER_SCREEN_CLASS(ScreenSelectCharacter);

void ScreenSelectCharacter::Init() {
  ScreenWithMenuElements::Init();

  std::vector<Character*> characters;
  CHARMAN->GetCharacters(characters);
  if (characters.empty()) {
    this->PostScreenMessage(SM_GoToNextScreen, 0);
    return;
  }

  switch (GAMESTATE->play_mode_) {
    // For Rave/Battle mode, we force the players to select characters
    // (by not returning in this switch)
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      break;

    default:
      /* Non Rave/Battle mode, just skip this screen if disabled. */
      if (PREFSMAN->show_dancing_characters_ != SDC_Select) {
        this->PostScreenMessage(SM_GoToNextScreen, 0);
        return;
      }
  }

  FOREACH_PlayerNumber(p) {
    selected_character_[p] = 0;
    if (GAMESTATE->IsHumanPlayer(p)) {
      selection_row_[p] = CHOOSING_HUMAN_CHARACTER;
    }
  }

  FOREACH_EnabledPlayer(p) {
    spr_title_[p].Load(THEME->GetPathG("ScreenSelectCharacter", "title 2x2"));
    spr_title_[p].SetState(GAMESTATE->IsHumanPlayer(p) ? p : 2 + p);
    spr_title_[p].StopAnimating();
    spr_title_[p].RunCommands(TITLE_ON_COMMAND(p));

    this->AddChild(&spr_title_[p]);

    spr_card_[p].RunCommands(CARD_ON_COMMAND(p));
    this->AddChild(&spr_card_[p]);

    spr_card_arrows_[p].Load(
        THEME->GetPathG("ScreenSelectCharacter", "card arrows"));
    spr_card_arrows_[p].RunCommands(CARD_ARROWS_ON_COMMAND(p));
    this->AddChild(&spr_card_arrows_[p]);

    for (unsigned i = 0; i < MAX_CHAR_ICONS_TO_SHOW; i++) {
      spr_icons_[p][i].ScaleToClipped(ICON_WIDTH, ICON_HEIGHT);
      this->AddChild(&spr_icons_[p][i]);
    }

    if (GAMESTATE->play_mode_ == PLAY_MODE_BATTLE ||
        GAMESTATE->play_mode_ == PLAY_MODE_RAVE) {
      spr_attack_frame_[p].Load(
          THEME->GetPathG("ScreenSelectCharacter", "attack frame 1x2"));
      spr_attack_frame_[p].StopAnimating();
      spr_attack_frame_[p].SetState(p);
      spr_attack_frame_[p].RunCommands(ATTACK_FRAME_ON_COMMAND(p));
      this->AddChild(&spr_attack_frame_[p]);

      for (int i = 0; i < NUM_ATTACK_LEVELS; i++) {
        for (int j = 0; j < NUM_ATTACKS_PER_LEVEL; j++) {
          float x = ATTACK_ICONS_START_X(p) + ATTACK_ICONS_SPACING_X * j;
          float y = ATTACK_ICONS_START_Y(p) + ATTACK_ICONS_SPACING_Y * i;
          attack_icons_[p][i][j].SetXY(x, y);
          attack_icons_[p][i][j].RunCommands(ATTACK_ICONS_ON_COMMAND(p));
          this->AddChild(&attack_icons_[p][i][j]);
        }
      }
    }
  }

  spr_explanation_.Load(
      THEME->GetPathG("ScreenSelectCharacter", "explanation"));
  spr_explanation_.RunCommands(EXPLANATION_ON_COMMAND);
  this->AddChild(&spr_explanation_);

  sound_change_.Load(THEME->GetPathS("ScreenSelectCharacter", "change"), true);

  SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("select group intro"));

  FOREACH_PlayerNumber(p) {
    if (GAMESTATE->IsHumanPlayer(p)) {
      AfterRowChange(p);
      AfterValueChange(p);
    }

    for (unsigned i = 0; i < MAX_CHAR_ICONS_TO_SHOW; i++) {
      spr_icons_[p][i].RunCommands(ICONS_ON_COMMAND(p));
    }
  }
}

ScreenSelectCharacter::~ScreenSelectCharacter() {
  LOG->Trace("ScreenSelectCharacter::~ScreenSelectCharacter()");
}

bool ScreenSelectCharacter::Input(const InputEventPlus& input) {
  LOG->Trace("ScreenSelectCharacter::Input()");

  if (IsTransitioning()) {
    return false;
  }

  return ScreenWithMenuElements::Input(input);  // default input handler
}

void ScreenSelectCharacter::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_BeginFadingOut) {
    StartTransitioningScreen(SM_GoToNextScreen);
  } else if (sm == SM_MenuTimer) {
    MakeSelection(PLAYER_1);
    if (!AllAreFinishedChoosing()) {
      ResetTimer();
    }
  } else {
    ScreenWithMenuElements::HandleScreenMessage(sm);
  }
}

PlayerNumber ScreenSelectCharacter::GetAffectedPlayerNumber(PlayerNumber pn) {
  switch (selection_row_[pn]) {
    case CHOOSING_HUMAN_CHARACTER:
      return pn;
    case CHOOSING_CPU_CHARACTER:
      return CPU_PLAYER[pn];
    case FINISHED_CHOOSING:
      return pn;
    default:
      FAIL_M(ssprintf(
          "Invalid character selection state: %i", selection_row_[pn]));
  }
}

void ScreenSelectCharacter::BeforeRowChange(PlayerNumber pn) {
  PlayerNumber affected = GetAffectedPlayerNumber(pn);
  switch (selection_row_[pn]) {
    case CHOOSING_CPU_CHARACTER:
    case CHOOSING_HUMAN_CHARACTER:
      spr_card_arrows_[affected].StopEffect();
      break;
    default:
      break;
  }
}

void ScreenSelectCharacter::AfterRowChange(PlayerNumber pn) {
  PlayerNumber affected = GetAffectedPlayerNumber(pn);
  switch (selection_row_[pn]) {
    case CHOOSING_CPU_CHARACTER:
    case CHOOSING_HUMAN_CHARACTER:
      spr_card_arrows_[affected].SetEffectGlowShift(
          1.0f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
      break;
    default:
      break;
  }
}

void ScreenSelectCharacter::AfterValueChange(PlayerNumber pn) {
  PlayerNumber affected = GetAffectedPlayerNumber(pn);
  switch (selection_row_[pn]) {
    case CHOOSING_CPU_CHARACTER:
    case CHOOSING_HUMAN_CHARACTER: {
      std::vector<Character*> characters;
      CHARMAN->GetCharacters(characters);
      Character* p_char = characters[selected_character_[affected]];
      spr_card_[affected].UnloadTexture();
      spr_card_[affected].Load(p_char->GetCardPath());

      if (GAMESTATE->play_mode_ == PLAY_MODE_BATTLE ||
          GAMESTATE->play_mode_ == PLAY_MODE_RAVE) {
        for (int i = 0; i < NUM_ATTACK_LEVELS; i++) {
          for (int j = 0; j < NUM_ATTACKS_PER_LEVEL; j++) {
            attack_icons_[affected][i][j].Load("ScreenSelectCharacter");
            attack_icons_[affected][i][j].Set(p_char->attacks_[i][j]);
          }
        }
      }

      int c = selected_character_[affected] - MAX_CHAR_ICONS_TO_SHOW / 2;
      wrap(c, characters.size());

      for (unsigned i = 0; i < MAX_CHAR_ICONS_TO_SHOW; i++) {
        c++;
        wrap(c, characters.size());
        Character* character = characters[c];
        Banner& banner = spr_icons_[affected][i];
        banner.LoadIconFromCharacter(character);
        float x = (affected == PLAYER_1) ? 320 - ICON_WIDTH : 320 + ICON_WIDTH;
        float y = SCALE(
            i, 0.f, MAX_CHAR_ICONS_TO_SHOW - 1.f,
            240 - (MAX_CHAR_ICONS_TO_SHOW / 2 * ICON_HEIGHT),
            240 + (MAX_CHAR_ICONS_TO_SHOW / 2 * ICON_HEIGHT));
        banner.SetXY(x, y);
      }
    } break;
    case FINISHED_CHOOSING:;  // do nothing
      break;
    default:
      FAIL_M(ssprintf(
          "Invalid character selection state: %i", selection_row_[pn]));
  }
}

bool ScreenSelectCharacter::MenuLeft(const InputEventPlus& input) {
  return MenuUp(input);
}

bool ScreenSelectCharacter::MenuRight(const InputEventPlus& input) {
  return MenuDown(input);
}

bool ScreenSelectCharacter::MenuUp(const InputEventPlus& input) {
  Move(input.pn_, -1);
  return true;
}

bool ScreenSelectCharacter::MenuDown(const InputEventPlus& input) {
  Move(input.pn_, +1);
  return true;
}

void ScreenSelectCharacter::Move(PlayerNumber pn, int delta_value) {
  PlayerNumber affected = GetAffectedPlayerNumber(pn);
  switch (selection_row_[pn]) {
    case CHOOSING_CPU_CHARACTER:
    case CHOOSING_HUMAN_CHARACTER: {
      std::vector<Character*> characters;
      CHARMAN->GetCharacters(characters);
      selected_character_[affected] += delta_value;
      wrap(selected_character_[affected], characters.size());
      AfterValueChange(pn);
      sound_change_.Play(true);
      break;
    }
    default:
      break;
  }
}

bool ScreenSelectCharacter::AllAreFinishedChoosing() const {
  FOREACH_HumanPlayer(
      p) if (selection_row_[p] != FINISHED_CHOOSING) return false;
  return true;
}

bool ScreenSelectCharacter::MenuStart(const InputEventPlus& input) {
  MakeSelection(input.pn_);
  return true;
}

void ScreenSelectCharacter::MakeSelection(PlayerNumber pn) {
  if (selection_row_[pn] == FINISHED_CHOOSING) {
    return;
  }

  // change row
  BeforeRowChange(pn);
  switch (selection_row_[pn]) {
    case CHOOSING_HUMAN_CHARACTER:
      selection_row_[pn] = GAMESTATE->AnyPlayersAreCpu()
                               ? CHOOSING_CPU_CHARACTER
                               : FINISHED_CHOOSING;
      break;
    case CHOOSING_CPU_CHARACTER:
      selection_row_[pn] = FINISHED_CHOOSING;
      break;
    default:
      break;
  }
  AfterRowChange(pn);
  AfterValueChange(pn);
  SCREENMAN->PlayStartSound();

  if (AllAreFinishedChoosing()) {
    FOREACH_PlayerNumber(p) {
      std::vector<Character*> characters;
      CHARMAN->GetCharacters(characters);
      Character* p_char = characters[selected_character_[p]];
      GAMESTATE->cur_characters_[p] = p_char;
    }

    StopTimer();
    this->PostScreenMessage(SM_BeginFadingOut, 0);
  }
}

bool ScreenSelectCharacter::MenuBack(const InputEventPlus& input) {
  Cancel(SM_GoToPrevScreen);
  return true;
}

void ScreenSelectCharacter::TweenOffScreen() {
  ScreenWithMenuElements::TweenOffScreen();

  FOREACH_PlayerNumber(p) {
    spr_card_[p].RunCommands(CARD_OFF_COMMAND(p));
    spr_title_[p].RunCommands(TITLE_OFF_COMMAND(p));
    spr_card_arrows_[p].RunCommands(CARD_ARROWS_OFF_COMMAND(p));
    if (GAMESTATE->play_mode_ == PLAY_MODE_BATTLE ||
        GAMESTATE->play_mode_ == PLAY_MODE_RAVE) {
      spr_attack_frame_[p].RunCommands(ATTACK_FRAME_OFF_COMMAND(p));
      for (int i = 0; i < NUM_ATTACK_LEVELS; i++) {
        for (int j = 0; j < NUM_ATTACKS_PER_LEVEL; j++) {
          attack_icons_[p][i][j].RunCommands(ATTACK_ICONS_OFF_COMMAND(p));
        }
      }
    }
    for (unsigned i = 0; i < MAX_CHAR_ICONS_TO_SHOW; i++) {
      spr_icons_[p][i].RunCommands(ICONS_OFF_COMMAND(p));
    }
  }
  spr_explanation_.RunCommands(EXPLANATION_OFF_COMMAND);
}

/*
 * (c) 2003-2004 Chris Danford
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
 */
