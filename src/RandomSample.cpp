#include "RandomSample.h"

#include <algorithm>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageSound.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "StdString.h"

RandomSample::RandomSample() { index_last_played_ = -1; }

RandomSample::~RandomSample() { UnloadAll(); }

bool RandomSample::Load(std::string file_path, int max_to_load) {
  if (GetExtension(file_path) == "") {
    return LoadSoundDir(file_path, max_to_load);
  } else {
    return LoadSound(file_path);
  }
}

void RandomSample::UnloadAll() {
  for (unsigned i = 0; i < samples_.size(); i++) {
    delete samples_[i];
  }
  samples_.clear();
}

bool RandomSample::LoadSoundDir(std::string dir, int max_to_load) {
  if (dir == "") {
    return true;
  }

#if 0
	/* (don't want to do this just yet) */
	/* If this is actually a directory, add a backslash to the filename,
	 * so we'll look for eg. themes\Default\sounds\sDir\*.mp3.  Otherwise,
	 * don't, so we'll look for all of the files starting with sDir,
	 * eg. themes\Default\sounds\sDir*.mp3. */
	if(IsADirectory(sDir) && sDir[sDir.size()-1] != "/" )
		sDir += "/";
#else
  // make sure there's a slash at the end of this path
  if (Right(dir, 1) != "/") {
    dir += "/";
  }
#endif

  std::vector<std::string> array_sound_files;
  GetDirListing(dir + "*.mp3", array_sound_files);
  GetDirListing(dir + "*.oga", array_sound_files);
  GetDirListing(dir + "*.ogg", array_sound_files);
  GetDirListing(dir + "*.wav", array_sound_files);

  std::shuffle(
      array_sound_files.begin(), array_sound_files.end(),
      random_number_generator);
  const unsigned int new_size = std::min<unsigned int>(
      array_sound_files.size(), static_cast<unsigned int>(max_to_load));
  array_sound_files.resize(new_size);

  for (unsigned i = 0; i < array_sound_files.size(); i++) {
    LoadSound(dir + array_sound_files[i]);
  }

  return true;
}

bool RandomSample::LoadSound(std::string sound_file_path) {
  LOG->Trace("RandomSample::LoadSound( %s )", sound_file_path.c_str());

  RageSound* ss = new RageSound;
  if (!ss->Load(sound_file_path)) {
    LOG->Trace(
        "Error loading \"%s\": %s", sound_file_path.c_str(),
        ss->GetError().c_str());
    delete ss;
    return false;
  }

  samples_.push_back(ss);

  return true;
}

int RandomSample::GetNextToPlay() {
  // play one of the samples
  if (samples_.empty()) {
    return -1;
  }

  int index_to_play = 0;
  for (int i = 0; i < 5; i++) {
    index_to_play = RandomInt(samples_.size());
    if (index_to_play != index_last_played_) {
      break;
    }
  }

  index_last_played_ = index_to_play;
  return index_to_play;
}

void RandomSample::PlayRandom() {
  int index_to_play = GetNextToPlay();
  if (index_to_play == -1) {
    return;
  }
  samples_[index_to_play]->Play(true);
}

void RandomSample::Stop() {
  if (index_last_played_ == -1) {  // nothing is currently playing
    return;
  }

  samples_[index_last_played_]->Stop();
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
