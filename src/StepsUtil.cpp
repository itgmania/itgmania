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

bool StepsCriteria::Matches(const Song* song, const Steps* steps) const {
  if (difficulty_ != Difficulty_Invalid &&
      steps->GetDifficulty() != difficulty_) {
    return false;
  }

  if (difficulties_.size() > 0 &&
      std::find(
          difficulties_.begin(), difficulties_.end(), steps->GetDifficulty()) ==
          difficulties_.end()) {
    return false;
  }

  if (low_meter_ != -1 && steps->GetMeter() < low_meter_) {
    return false;
  }
  if (high_meter_ != -1 && steps->GetMeter() > high_meter_) {
    return false;
  }
  if (st_ != StepsType_Invalid && steps->steps_type_ != st_) {
    return false;
  }
  switch (locked_) {
    DEFAULT_FAIL(locked_);
    case Locked_Locked:
      if (UNLOCKMAN && !UNLOCKMAN->StepsIsLocked(song, steps)) {
        return false;
      }
      break;
    case Locked_Unlocked:
      if (UNLOCKMAN && UNLOCKMAN->StepsIsLocked(song, steps)) {
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
  std::vector<std::string> group_names = soc.group_names_;
  if (group_names.size() == 0) {
    group_names.push_back(GROUP_ALL);
  }
  std::vector<Song*> songs;
  for (unsigned i = 0; i < group_names.size(); i++) {
    const std::vector<Song*>& group_songs = SONGMAN->GetSongs(group_names[i]);
    songs.insert(songs.end(), group_songs.begin(), group_songs.end());
  }

  for (Song* so : songs) {
    if (!soc.Matches(so)) {
      continue;
    }
    GetAllMatching(so, stc, out);  // TODO: Look into why this can't be const.
  }
}

void StepsUtil::GetAllMatching(
    Song* song, const StepsCriteria& stc, std::vector<SongAndSteps>& out) {
  const std::vector<Steps*>& steps =
      (stc.st_ == StepsType_Invalid ? song->GetAllSteps()
                                    : song->GetStepsByStepsType(stc.st_));

  for (Steps* st : steps) {
    if (stc.Matches(song, st)) {
      out.push_back(SongAndSteps(song, st));
    }
  }
}

void StepsUtil::GetAllMatchingEndless(
    Song* song, const StepsCriteria& stc, std::vector<SongAndSteps>& out) {
  const std::vector<Steps*>& steps =
      (stc.st_ == StepsType_Invalid ? song->GetAllSteps()
                                    : song->GetStepsByStepsType(stc.st_));
  const size_t previous_size = out.size();
  int successful = false;

  GetAllMatching(song, stc, out);
  if (out.size() != previous_size) {
    successful = true;
  }

  if (!successful && steps.size() > 0) {
    Difficulty difficulty = (*(steps.begin()))->GetDifficulty();
    Difficulty previous_difficulty = difficulty;
    int lowest_difficulty_index = 0;
    std::vector<Difficulty> difficulties;
    for (auto st = steps.begin(); st != steps.end(); ++st) {
      previous_difficulty = difficulty;
      difficulty = (*st)->GetDifficulty();
      if ((st - steps.begin()) == 0) {
        continue;
      }
      if (difficulty < previous_difficulty) {
        lowest_difficulty_index = st - steps.begin();
      }
    }
    out.push_back(SongAndSteps(song, steps.at(lowest_difficulty_index)));
  }
}

bool StepsUtil::CompareNotesPointersByRadarValues(
    const Steps* steps1, const Steps* steps2) {
  float score1 = 0;
  float score2 = 0;

  score1 += steps1->GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds];
  score2 += steps2->GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds];

  return score1 < score2;
}

bool StepsUtil::CompareNotesPointersByMeter(
    const Steps* steps1, const Steps* steps2) {
  return steps1->GetMeter() < steps2->GetMeter();
}

bool StepsUtil::CompareNotesPointersByDifficulty(
    const Steps* steps1, const Steps* steps2) {
  return steps1->GetDifficulty() < steps2->GetDifficulty();
}

void StepsUtil::SortNotesArrayByDifficulty(std::vector<Steps*>& array_steps) {
  /* Sort in reverse order of priority. Sort by description first, to get
   * a predictable order for songs with no radar values (edits). */
  stable_sort(
      array_steps.begin(), array_steps.end(),
      CompareStepsPointersByDescription);
  stable_sort(
      array_steps.begin(), array_steps.end(),
      CompareNotesPointersByRadarValues);
  stable_sort(
      array_steps.begin(), array_steps.end(), CompareNotesPointersByMeter);
  stable_sort(
      array_steps.begin(), array_steps.end(), CompareNotesPointersByDifficulty);
}

bool StepsUtil::CompareStepsPointersByTypeAndDifficulty(
    const Steps* step1, const Steps* step2) {
  if (step1->steps_type_ < step2->steps_type_) {
    return true;
  }
  if (step1->steps_type_ > step2->steps_type_) {
    return false;
  }
  return step1->GetDifficulty() < step2->GetDifficulty();
}

void StepsUtil::SortStepsByTypeAndDifficulty(
    std::vector<Steps*>& array_song_pointers) {
  sort(
      array_song_pointers.begin(), array_song_pointers.end(),
      CompareStepsPointersByTypeAndDifficulty);
}

bool StepsUtil::CompareStepsPointersByDescription(
    const Steps* step1, const Steps* step2) {
  return CompareNoCase(step1->GetDescription(), step2->GetDescription()) < 0;
}

void StepsUtil::SortStepsByDescription(
    std::vector<Steps*>& array_song_pointers) {
  sort(
      array_song_pointers.begin(), array_song_pointers.end(),
      CompareStepsPointersByDescription);
}

void StepsUtil::RemoveLockedSteps(
    const Song* song, std::vector<Steps*>& steps) {
  for (int i = steps.size() - 1; i >= 0; i--) {
    if (UNLOCKMAN->StepsIsLocked(song, steps[i])) {
      steps.erase(steps.begin() + i);
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

Steps* StepsID::ToSteps(const Song* p, bool allow_null) const {
  if (st_ == StepsType_Invalid || dc_ == Difficulty_Invalid) {
    return nullptr;
  }

  SongID song_id;
  song_id.FromSong(p);

  Steps* ret = nullptr;
  if (dc_ == Difficulty_Edit) {
    ret = SongUtil::GetOneSteps(
        p, st_, dc_, -1, -1, description_, "", hash_, true);
  } else {
    ret = SongUtil::GetOneSteps(p, st_, dc_, -1, -1, "", "", 0, true);
  }

  if (!allow_null && ret == nullptr) {
    FAIL_M(ssprintf("%i, %i, \"%s\"", st_, dc_, description_.c_str()));
  }

  return ret;
}

XNode* StepsID::CreateNode() const {
  XNode* node = new XNode("Steps");

  node->AppendAttr("StepsType", GAMEMAN->GetStepsTypeInfo(st_).name_);
  node->AppendAttr("Difficulty", DifficultyToString(dc_));
  if (dc_ == Difficulty_Edit) {
    node->AppendAttr("Description", description_);
    node->AppendAttr("Hash", hash_);
  }

  return node;
}

void StepsID::LoadFromNode(const XNode* node) {
  ASSERT(node->GetName() == "Steps");

  std::string temp;

  node->GetAttrValue("StepsType", temp);
  st_ = GAMEMAN->StringToStepsType(temp);

  node->GetAttrValue("Difficulty", temp);
  dc_ = StringToDifficulty(temp);

  if (dc_ == Difficulty_Edit) {
    node->GetAttrValue("Description", description_);
    node->GetAttrValue("Hash", hash_);
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
