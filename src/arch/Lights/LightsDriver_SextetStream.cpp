#include "LightsDriver_SextetStream.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "LightsManager.h"
#include "Preference.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SextetUtils.h"
#include "arch/Lights/LightsDriver.h"

// Private members/methods are kept out of the header using an opaque pointer
// `_impl`. Google "pimpl idiom" for an explanation of what's going on and why
// it is (or might be) useful.

// Implementation class

namespace {
class SextetImpl {
 protected:
  uint8_t last_output_[kFullSextetCount];

#ifdef _WIN32
  HANDLE out_;
#else
  RageFile* out_;
#endif

 public:
#ifdef _WIN32
  SextetImpl(HANDLE file){
#else
  SextetImpl(RageFile* file) {
#endif
      out_ = file;

  // Ensure a non-match the first time
  last_output_[0] = 0;
}

virtual ~SextetImpl() {
  if (out_ != nullptr) {
#ifdef _WIN32
    CloseHandle(out_);
#else
      out_->Flush();
      out_->Close();
      RageUtil::SafeDelete(out_);
#endif
  }
}

void Set(const LightsState* ls) {
  uint8_t buffer[kFullSextetCount];

  packLine(buffer, ls);

  // Only write if the message has changed since the last write.
  if (memcmp(buffer, last_output_, kFullSextetCount) != 0) {
    if (out_ != nullptr) {
#ifdef _WIN32
      DWORD written;
      bool success = WriteFile(
          out_,              // pipe handle
          buffer,            // message
          kFullSextetCount,  // message length
          &written,          // bytes written
          NULL);             // not overlapped

#else
        out_->Write(buffer, kFullSextetCount);
        out_->Flush();
#endif
    }

    // Remember last message
    memcpy(last_output_, buffer, kFullSextetCount);
  }
}
};  // namespace
}  // namespace

// LightsDriver_SextetStream interface
// (Wrapper for SextetImpl)
LightsDriver_SextetStream::LightsDriver_SextetStream() { impl_ = nullptr; }

LightsDriver_SextetStream::~LightsDriver_SextetStream() {
  if (static_cast<SextetImpl*>(impl_) != nullptr) {
    delete static_cast<SextetImpl*>(impl_);
  }
}

void LightsDriver_SextetStream::Set(const LightsState* ls) {
  if (static_cast<SextetImpl*>(impl_) != nullptr) {
    static_cast<SextetImpl*>(impl_)->Set(ls);
  }
}

// LightsDriver_SextetStreamToFile implementation

REGISTER_LIGHTS_DRIVER_CLASS(SextetStreamToFile);

#if defined(_WIN32)
#define DEFAULT_OUTPUT_FILENAME "\\\\.\\pipe\\StepMania-Lights-SextetStream"
#else
#define DEFAULT_OUTPUT_FILENAME "Data/StepMania-Lights-SextetStream.out"
#endif
static Preference<std::string> sextet_stream_output_filename(
    "SextetStreamOutputFilename", DEFAULT_OUTPUT_FILENAME);

inline RageFile* openOutputStream(const std::string& filename) {
  RageFile* file = new RageFile;

  if (!file->Open(filename, RageFile::WRITE | RageFile::STREAMED)) {
    LOG->Warn(
        "Error opening file '%s' for output: %s", filename.c_str(),
        file->GetError().c_str());
    RageUtil::SafeDelete(file);
    file = nullptr;
  }

  return file;
}

LightsDriver_SextetStreamToFile::LightsDriver_SextetStreamToFile() {
#ifdef WINDOWS
  impl_ = new SextetImpl(CreateFile(
      sextet_stream_output_filename.Get().c_str(),  // pipe name
      GENERIC_WRITE,
      0,              // no sharing
      NULL,           // default security attributes
      OPEN_EXISTING,  // opens existing pipe
      0,              // default attributes
      NULL));
#else
  impl_ = new SextetImpl(openOutputStream(sextet_stream_output_filename));
#endif
}

/*
 * Copyright © 2014 Peter S. May
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
