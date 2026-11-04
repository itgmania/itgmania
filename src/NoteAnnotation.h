#ifndef NOTE_ANNOTATION_H
#define NOTE_ANNOTATION_H

#include <string>
#include <vector>

#include "StepParityDatastructs.h"
#include "TechCountsCategory.h"

struct NoteAnnotation {
  float beat_;
  std::vector<int8_t> where_the_feet_are_;
  std::vector<TechCountsCategory> tech_;

  static void FromString(
      const std::string& input, std::vector<NoteAnnotation>& out);
  static std::string ToString(std::vector<NoteAnnotation>& annotations);
};

struct NoteAnnotationCache {
  std::string compressed_;
  mutable std::vector<NoteAnnotation> decompressed_;
  const std::string& GetCompressed() const;
  const std::vector<NoteAnnotation>& GetNoteAnnotations() const;
  void Compress();
};

#endif
