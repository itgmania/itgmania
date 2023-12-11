#ifndef COURSEUTIL_H
#define COURSEUTIL_H

#include "global.h"

#include <vector>

#include "Course.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "Song.h"
#include "XmlFile.h"

// TODO(teejusb): Remove forward declaration. Profile.h uses CourseID defined
// below so we have a circular dependency.
class Profile;

bool CompareCoursePointersBySortValueAscending(
    const Course* song1, const Course* song2);
bool CompareCoursePointersBySortValueDescending(
    const Course* song1, const Course* song2);
bool CompareCoursePointersByTitle(
    const Course* course1, const Course* course2);

// Utility functions that deal with Courses.
namespace CourseUtil {

void SortCoursePointerArrayByDifficulty(std::vector<Course*>& courses);
void SortCoursePointerArrayByType(std::vector<Course*>& courses);
void SortCoursePointerArrayByTitle(std::vector<Course*>& courses);
void SortCoursePointerArrayByAvgDifficulty(std::vector<Course*>& courses);
void SortCoursePointerArrayByTotalDifficulty(std::vector<Course*>& courses);
void SortCoursePointerArrayByRanking(std::vector<Course*>& courses);
void SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses, ProfileSlot slot, bool descending);
void SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses, const Profile* profile, bool descending);
void SortByMostRecentlyPlayedForMachine(std::vector<Course*>& courses);

void MoveRandomToEnd(std::vector<Course*>& courses);

void MakeDefaultEditCourseEntry(CourseEntry& out);

void AutogenEndlessFromGroup(
    const RString& group_name, Difficulty difficulty, Course& out);
void AutogenNonstopFromGroup(
    const RString& group_name, Difficulty difficulty, Course& out);
void AutogenOniFromArtist(
    const RString& artist_name, RString artist_name_translit,
    std::vector<Song*> songs, Difficulty difficulty, Course& out);

void WarnOnInvalidMods(RString mods);

};  // namespace CourseUtil

// Utility functions that deal with Edit Courses.
namespace EditCourseUtil {

void UpdateAndSetTrail();
void PrepareForPlay();
void LoadDefaults(Course& out);
bool RemoveAndDeleteFile(Course* course);
bool ValidateEditCourseName(const RString& answer, RString& error);
void GetAllEditCourses(std::vector<Course*>& courses);
bool Save(Course* course);
bool RenameAndSave(Course* course, RString name);

extern int MAX_NAME_LENGTH;
extern int MAX_PER_PROFILE;
extern int MIN_WORKOUT_MINUTES;
extern int MAX_WORKOUT_MINUTES;

// if true, we are working with a Course that has never been named.
extern bool s_bNewCourseNeedsName;

};  // namespace EditCourseUtil

class CourseID {
 public:
  CourseID() : path_(""), full_title_("") { Unset(); }
  void Unset() { FromCourse(nullptr); }
  void FromCourse(const Course* course);
  Course* ToCourse() const;
  const RString& GetPath() const { return path_; }
  bool operator<(const CourseID& other) const {
    if (path_ != other.path_) {
      return path_ < other.path_;
    }
    return full_title_ < other.full_title_;
  }

  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);
  void FromPath(RString path) { path_ = path; }
  RString ToString() const;
  bool IsValid() const;

 private:
  RString path_;
  RString full_title_;
};

#endif  // COURSEUTIL_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
