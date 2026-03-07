#include "ScoreKeeperShared.h"

#include <vector>

#include "Attack.h"
#include "GameState.h"
#include "NoteDataUtil.h"
#include "NoteTypes.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "ScoreKeeper.h"
#include "ScoreKeeperNormal.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"
#include "TimingData.h"

/* In Routine, we have two Players, but the master one handles all of the
 * scoring.  The other one will just receive misses for everything, and
 * shouldn't do anything. */
ScoreKeeperShared::ScoreKeeperShared(
    PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats)
    : ScoreKeeperNormal(pPlayerState, pPlayerStageStats) {}

void ScoreKeeperShared::Load(
    const std::vector<Song*>& apSongs, const std::vector<Steps*>& apSteps,
    const std::vector<AttackArray>& asModifiers) {
  ScoreKeeperNormal::Load(apSongs, apSteps, asModifiers);

  const Style* pStyle =
      GAMESTATE->GetCurrentStyle(m_pPlayerState->m_PlayerNumber);
  if (pStyle == nullptr ||
      pStyle->m_StyleType != StyleType_TwoPlayersSharedSides) {
    return;
  }

  // Routine shared scoring receives judgments from both players. Recompute
  // possible points using both composite parts so percentage stays bounded.
  int iTotalPossibleDancePoints = 0;
  int iTotalPossibleGradePoints = 0;
  for (unsigned i = 0; i < apSteps.size(); ++i) {
    Song* pSong = apSongs[i];
    Steps* pSteps = apSteps[i];
    ASSERT(pSong != nullptr);
    ASSERT(pSteps != nullptr);

    NoteData ndTemp;
    pSteps->GetNoteData(ndTemp);

    NoteData ndTransformedForStyle;
    pStyle->GetTransformedNoteDataForStyle(
        m_pPlayerState->m_PlayerNumber, ndTemp, ndTransformedForStyle);

    std::vector<NoteData> vParts;
    if (ndTransformedForStyle.IsComposite()) {
      NoteDataUtil::SplitCompositeNoteData(ndTransformedForStyle, vParts);
    } else {
      vParts.push_back(ndTransformedForStyle);
    }

    for (const NoteData& part : vParts) {
      NoteData ndPre = part;
      NoteDataUtil::TransformNoteData(
          ndPre, *(pSteps->GetTimingData()), asModifiers[i],
          pSteps->m_StepsType, pSong);

      NoteData ndPost = ndPre;
      NoteDataUtil::TransformNoteData(
          ndPost, *(pSteps->GetTimingData()),
          m_pPlayerState->m_PlayerOptions.GetStage(), pSteps->m_StepsType);

      GAMESTATE->SetProcessedTimingData(
          const_cast<TimingData*>(pSteps->GetTimingData()));
      iTotalPossibleDancePoints += GetPossibleDancePoints(
          &ndPre, &ndPost, pSteps->GetTimingData(),
          pSong->m_fMusicLengthSeconds);
      iTotalPossibleGradePoints += GetPossibleGradePoints(
          &ndPre, &ndPost, pSteps->GetTimingData(),
          pSong->m_fMusicLengthSeconds);
      GAMESTATE->SetProcessedTimingData(nullptr);
    }
  }

  m_pPlayerStageStats->m_iPossibleDancePoints = iTotalPossibleDancePoints;
  m_pPlayerStageStats->m_iPossibleGradePoints = iTotalPossibleGradePoints;
}

// These ScoreKeepers don't get to draw.
void ScoreKeeperShared::DrawPrimitives() {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::DrawPrimitives();
}

void ScoreKeeperShared::Update(float fDelta) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::Update(fDelta);
}

void ScoreKeeperShared::OnNextSong(
    int iSongInCourseIndex, const Steps* pSteps, const NoteData* pNoteData) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::OnNextSong(iSongInCourseIndex, pSteps, pNoteData);
}

void ScoreKeeperShared::HandleTapScore(const TapNote& tn) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::HandleTapScore(tn);
}

void ScoreKeeperShared::HandleTapRowScore(const NoteData& nd, int iRow) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::HandleTapRowScore(nd, iRow);
}

void ScoreKeeperShared::HandleHoldScore(const TapNote& tn) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //    return;
  //  }
  ScoreKeeperNormal::HandleHoldScore(tn);
}

void ScoreKeeperShared::HandleHoldActiveSeconds(float fMusicSecondsHeld) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::HandleHoldActiveSeconds(fMusicSecondsHeld);
}

void ScoreKeeperShared::HandleHoldCheckpointScore(
    const NoteData& nd, int iRow, int iNumHoldsHeldThisRow,
    int iNumHoldsMissedThisRow) {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::HandleHoldCheckpointScore(
      nd, iRow, iNumHoldsHeldThisRow, iNumHoldsMissedThisRow);
}

void ScoreKeeperShared::HandleTapScoreNone() {
  // if (m_pPlayerState->m_PlayerNumber != GAMESTATE->GetMasterPlayerNumber()) {
  //   return;
  // }
  ScoreKeeperNormal::HandleTapScoreNone();
}

/*
 * (c) 2006-2010 Steve Checkoway, Glenn Maynard
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
