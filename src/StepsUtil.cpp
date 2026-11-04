#include "StepsUtil.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "PlayerNumber.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "UnlockManager.h"
#include "XmlFile.h"
#include "global.h"

bool StepsCriteria::Matches(const Song* pSong, const Steps* pSteps) const {
  if (difficulty_ != Difficulty_Invalid &&
      pSteps->GetDifficulty() != difficulty_) {
    return false;
  }

  if (difficulties_.size() > 0 &&
      std::find(
          difficulties_.begin(), difficulties_.end(),
          pSteps->GetDifficulty()) == difficulties_.end()) {
    return false;
  }

  if (low_meter_ != -1 && pSteps->GetMeter() < low_meter_) {
    return false;
  }
  if (high_meter_ != -1 && pSteps->GetMeter() > high_meter_) {
    return false;
  }
  if (st_ != StepsType_Invalid && pSteps->steps_type_ != st_) {
    return false;
  }
  switch (locked_) {
    DEFAULT_FAIL(locked_);
    case Locked_Locked:
      if (UNLOCKMAN && !UNLOCKMAN->StepsIsLocked(pSong, pSteps)) {
        return false;
      }
      break;
    case Locked_Unlocked:
      if (UNLOCKMAN && UNLOCKMAN->StepsIsLocked(pSong, pSteps)) {
        return false;
      }
      break;
    case Locked_DontCare:
      break;
  }

  return true;
}

void StepsUtil::GetAllMatching(
    const SongCriteria& soc, const StepsCriteria& stc,
    std::vector<SongAndSteps>& out) {
  std::vector<std::string> groupNames = soc.group_names_;
  if (groupNames.size() == 0) {
    groupNames.push_back(GROUP_ALL);
  }
  std::vector<Song*> songs;
  for (unsigned i = 0; i < groupNames.size(); i++) {
    const std::vector<Song*>& groupSongs = SONGMAN->GetSongs(groupNames[i]);
    songs.insert(songs.end(), groupSongs.begin(), groupSongs.end());
  }

  for (Song* so : songs) {
    if (!soc.Matches(so)) {
      continue;
    }
    GetAllMatching(so, stc, out);  // TODO: Look into why this can't be const.
  }
}

void StepsUtil::GetAllMatching(
    Song* pSong, const StepsCriteria& stc, std::vector<SongAndSteps>& out) {
  const std::vector<Steps*>& vSteps =
      (stc.st_ == StepsType_Invalid ? pSong->GetAllSteps()
                                    : pSong->GetStepsByStepsType(stc.st_));

  for (Steps* st : vSteps) {
    if (stc.Matches(pSong, st)) {
      out.push_back(SongAndSteps(pSong, st));
    }
  }
}

void StepsUtil::GetAllMatchingEndless(
    Song* pSong, const StepsCriteria& stc, std::vector<SongAndSteps>& out) {
  const std::vector<Steps*>& vSteps =
      (stc.st_ == StepsType_Invalid ? pSong->GetAllSteps()
                                    : pSong->GetStepsByStepsType(stc.st_));
  const size_t previousSize = out.size();
  int successful = false;

  GetAllMatching(pSong, stc, out);
  if (out.size() != previousSize) {
    successful = true;
  }

  if (!successful && vSteps.size() > 0) {
    Difficulty difficulty = (*(vSteps.begin()))->GetDifficulty();
    Difficulty previousDifficulty = difficulty;
    int lowestDifficultyIndex = 0;
    std::vector<Difficulty> difficulties;
    for (auto st = vSteps.begin(); st != vSteps.end(); ++st) {
      previousDifficulty = difficulty;
      difficulty = (*st)->GetDifficulty();
      if ((st - vSteps.begin()) == 0) {
        continue;
      }
      if (difficulty < previousDifficulty) {
        lowestDifficultyIndex = st - vSteps.begin();
      }
    }
    out.push_back(SongAndSteps(pSong, vSteps.at(lowestDifficultyIndex)));
  }
}

bool StepsUtil::CompareNotesPointersByRadarValues(
    const Steps* pSteps1, const Steps* pSteps2) {
  float fScore1 = 0;
  float fScore2 = 0;

  fScore1 += pSteps1->GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds];
  fScore2 += pSteps2->GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds];

  return fScore1 < fScore2;
}

bool StepsUtil::CompareNotesPointersByMeter(
    const Steps* pSteps1, const Steps* pSteps2) {
  return pSteps1->GetMeter() < pSteps2->GetMeter();
}

bool StepsUtil::CompareNotesPointersByDifficulty(
    const Steps* pSteps1, const Steps* pSteps2) {
  return pSteps1->GetDifficulty() < pSteps2->GetDifficulty();
}

void StepsUtil::SortNotesArrayByDifficulty(std::vector<Steps*>& arraySteps) {
  /* Sort in reverse order of priority. Sort by description first, to get
   * a predictable order for songs with no radar values (edits). */
  stable_sort(
      arraySteps.begin(), arraySteps.end(), CompareStepsPointersByDescription);
  stable_sort(
      arraySteps.begin(), arraySteps.end(), CompareNotesPointersByRadarValues);
  stable_sort(
      arraySteps.begin(), arraySteps.end(), CompareNotesPointersByMeter);
  stable_sort(
      arraySteps.begin(), arraySteps.end(), CompareNotesPointersByDifficulty);
}

bool StepsUtil::CompareStepsPointersByTypeAndDifficulty(
    const Steps* pStep1, const Steps* pStep2) {
  if (pStep1->steps_type_ < pStep2->steps_type_) {
    return true;
  }
  if (pStep1->steps_type_ > pStep2->steps_type_) {
    return false;
  }
  return pStep1->GetDifficulty() < pStep2->GetDifficulty();
}

void StepsUtil::SortStepsByTypeAndDifficulty(
    std::vector<Steps*>& arraySongPointers) {
  sort(
      arraySongPointers.begin(), arraySongPointers.end(),
      CompareStepsPointersByTypeAndDifficulty);
}

bool StepsUtil::CompareStepsPointersByDescription(
    const Steps* pStep1, const Steps* pStep2) {
  return CompareNoCase(pStep1->GetDescription(), pStep2->GetDescription()) < 0;
}

void StepsUtil::SortStepsByDescription(std::vector<Steps*>& arraySongPointers) {
  sort(
      arraySongPointers.begin(), arraySongPointers.end(),
      CompareStepsPointersByDescription);
}

void StepsUtil::RemoveLockedSteps(
    const Song* pSong, std::vector<Steps*>& vpSteps) {
  for (int i = vpSteps.size() - 1; i >= 0; i--) {
    if (UNLOCKMAN->StepsIsLocked(pSong, vpSteps[i])) {
      vpSteps.erase(vpSteps.begin() + i);
    }
  }
}

////////////////////////////////
// StepsID
////////////////////////////////

void StepsID::FromSteps(const Steps* p) {
  if (p == nullptr) {
    st_ = StepsType_Invalid;
    dc_ = Difficulty_Invalid;
    description_ = "";
    hash_ = 0;
  } else {
    st_ = p->steps_type_;
    dc_ = p->GetDifficulty();
    if (dc_ == Difficulty_Edit) {
      description_ = p->GetDescription();
      hash_ = p->GetHash();
    } else {
      description_ = "";
      hash_ = 0;
    }
  }
}

/* XXX: Don't allow duplicate edit descriptions, and don't allow edit
 * descriptions to be difficulty names (eg. "Hard").  If we do that, this will
 * be completely unambiguous.
 *
 * XXX: Unless two memcards are inserted and there's overlap in the names.  In
 * that case, maybe both edits should be renamed to "Pn: foo"; as long as we
 * don't write them back out (which we don't do except in the editor), it won't
 * be permanent. We could do this during the actual Steps::GetID() call,
 * instead, but then it'd have to have access to Song::m_LoadedFromProfile. */

Steps* StepsID::ToSteps(const Song* p, bool bAllowNull) const {
  if (st_ == StepsType_Invalid || dc_ == Difficulty_Invalid) {
    return nullptr;
  }

  SongID songID;
  songID.FromSong(p);

  Steps* pRet = nullptr;
  if (dc_ == Difficulty_Edit) {
    pRet = SongUtil::GetOneSteps(
        p, st_, dc_, -1, -1, description_, "", hash_, true);
  } else {
    pRet = SongUtil::GetOneSteps(p, st_, dc_, -1, -1, "", "", 0, true);
  }

  if (!bAllowNull && pRet == nullptr) {
    FAIL_M(ssprintf("%i, %i, \"%s\"", st_, dc_, description_.c_str()));
  }

  return pRet;
}

XNode* StepsID::CreateNode() const {
  XNode* pNode = new XNode("Steps");

  pNode->AppendAttr("StepsType", GAMEMAN->GetStepsTypeInfo(st_).name_);
  pNode->AppendAttr("Difficulty", DifficultyToString(dc_));
  if (dc_ == Difficulty_Edit) {
    pNode->AppendAttr("Description", description_);
    pNode->AppendAttr("Hash", hash_);
  }

  return pNode;
}

void StepsID::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "Steps");

  std::string sTemp;

  pNode->GetAttrValue("StepsType", sTemp);
  st_ = GAMEMAN->StringToStepsType(sTemp);

  pNode->GetAttrValue("Difficulty", sTemp);
  dc_ = StringToDifficulty(sTemp);

  if (dc_ == Difficulty_Edit) {
    pNode->GetAttrValue("Description", description_);
    pNode->GetAttrValue("Hash", hash_);
  } else {
    description_ = "";
    hash_ = 0;
  }
}

std::string StepsID::ToString() const {
  std::string s = GAMEMAN->GetStepsTypeInfo(st_).name_;
  s += " " + DifficultyToString(dc_);
  if (dc_ == Difficulty_Edit) {
    s += " " + description_;
    s += ssprintf(" %u", hash_);
  }
  return s;
}

bool StepsID::IsValid() const {
  return st_ != StepsType_Invalid && dc_ != Difficulty_Invalid;
}

bool StepsID::operator<(const StepsID& rhs) const {
#define COMP(a)               \
  if (a < rhs.a) return true; \
  if (a > rhs.a) return false;
  COMP(st_);
  COMP(dc_);
  COMP(description_);
  // See explanation in class declaration. -Kyz
  if (hash_ != 0 && rhs.hash_ != 0) {
    COMP(hash_);
  }
#undef COMP
  return false;
}

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
