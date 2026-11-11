#ifndef COURSEUTIL_H
#define COURSEUTIL_H

#include <string>
#include <vector>

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"

class Course;
class Profile;
class XNode;
class CourseEntry;
class Song;

bool CompareCoursePointersBySortValueAscending(
    const Course* song1, const Course* song2);
bool CompareCoursePointersBySortValueDescending(
    const Course* song1, const Course* song2);
bool CompareCoursePointersByTitle(const Course* course1, const Course* course2);

/** @brief Utility functions that deal with Courses. */
namespace CourseUtil {
void SortCoursePointerArrayByDifficulty(std::vector<Course*>& courses_in_out);
void SortCoursePointerArrayByType(std::vector<Course*>& courses_in_out);
void SortCoursePointerArrayByTitle(std::vector<Course*>& courses_in_out);
void SortCoursePointerArrayByAvgDifficulty(
    std::vector<Course*>& courses_in_out);
void SortCoursePointerArrayByTotalDifficulty(
    std::vector<Course*>& courses_in_out);
void SortCoursePointerArrayByRanking(std::vector<Course*>& courses_in_out);
void SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses_in_out, ProfileSlot slot, bool descending);
void SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses_in_out, const Profile* profile,
    bool descending);
void SortByMostRecentlyPlayedForMachine(std::vector<Course*>& courses_in_out);
// sm-ssc sort additions:
// void SortCoursePointerArrayBySectionName( std::vector<Course*>
// &vpCoursesInOut, SortOrder so );

void MoveRandomToEnd(std::vector<Course*>& courses_in_out);

void AutogenEndlessFromGroup(
    const std::string& group_name, Difficulty dc, Course& out);
void AutogenNonstopFromGroup(
    const std::string& group_name, Difficulty dc, Course& out);
void AutogenOniFromArtist(
    const std::string& artist_name, std::string artist_name_translit,
    std::vector<Song*> songs, Difficulty dc, Course& out);

bool ValidateEditCourseName(const std::string& answer, std::string& error_out);

void WarnOnInvalidMods(std::string mods);

// sm-ssc additions:
// std::string GetSectionNameFromCourseAndSort( const Course *pCourse, SortOrder
// so );
};  // namespace CourseUtil

/** @brief Utility functions that deal with Edit Courses. */
namespace EditCourseUtil {
void UpdateAndSetTrail();
void PrepareForPlay();
void LoadDefaults(Course& out);
bool RemoveAndDeleteFile(Course* course);
bool ValidateEditCourseName(const std::string& answer, std::string& error_out);
void GetAllEditCourses(std::vector<Course*>& courses_out);
bool Save(Course* course);
bool RenameAndSave(Course* course, std::string name);

extern int max_name_length;
extern int max_per_profile;
extern int min_workout_minutes;
extern int max_workout_minutes;

extern bool new_course_needs_name;  // if true, we are working with a Course
                                    // that has never been named
};  // namespace EditCourseUtil

class CourseID {
 public:
  CourseID() : path_(""), full_title_("") { Unset(); }
  void Unset() { FromCourse(nullptr); }
  void FromCourse(const Course* p);
  Course* ToCourse() const;
  const std::string& GetPath() const { return path_; }
  bool operator<(const CourseID& other) const {
    if (path_ != other.path_) {
      return path_ < other.path_;
    }
    return full_title_ < other.full_title_;
  }

  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);
  void FromPath(std::string path) { path_ = path; }
  bool IsValid() const;

 private:
  std::string path_;
  std::string full_title_;
};

#endif

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
