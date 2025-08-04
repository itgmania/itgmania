#include "global.h"
#include "LightsDriver_SextetStream.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SextetUtils.h"

#include <cstdint>
#include <cstring>

// Private members/methods are kept out of the header using an opaque pointer `_impl`.
// Google "pimpl idiom" for an explanation of what's going on and why it is (or might be) useful.


// Implementation class

namespace
{
	class SextetImpl
	{
	protected:
		uint8_t lastOutput[FULL_SEXTET_COUNT];

#ifdef _WIN32
		HANDLE out;
#else
		RageFile* out;
#endif

	public:

#ifdef _WIN32
		SextetImpl(HANDLE file) {
#else
		SextetImpl(RageFile * file) {
#endif
			out = file;

			// Ensure a non-match the first time
			lastOutput[0] = 0;
		}

		virtual ~SextetImpl() {
			if(out != nullptr)
			{
#ifdef _WIN32
				CloseHandle(out);
#else
				out->Flush();
				out->Close();
				RageUtil::SafeDelete(out);
#endif
			}
		}

		void Set(const LightsState * ls)
		{
			uint8_t buffer[FULL_SEXTET_COUNT];

			packLine(buffer, ls);

			// Only write if the message has changed since the last write.
			if(memcmp(buffer, lastOutput, FULL_SEXTET_COUNT) != 0)
			{
				if(out != nullptr)
				{
#ifdef _WIN32
					DWORD cbWritten;
					bool fSuccess = WriteFile(
						out,                  // pipe handle 
						buffer,             // message 
						FULL_SEXTET_COUNT,              // message length 
						&cbWritten,             // bytes written 
						NULL);                  // not overlapped 

#else
					out->Write(buffer, FULL_SEXTET_COUNT);
					out->Flush();
#endif
				}

				// Remember last message
				memcpy(lastOutput, buffer, FULL_SEXTET_COUNT);
			}
		}
	};
}

// LightsDriver_SextetStream interface
// (Wrapper for SextetImpl)
LightsDriver_SextetStream::LightsDriver_SextetStream()
{
	_impl = nullptr;
}

LightsDriver_SextetStream::~LightsDriver_SextetStream()
{
	if(static_cast<SextetImpl*>(_impl) != nullptr)
	{
		delete static_cast<SextetImpl*>(_impl);
	}
}

void LightsDriver_SextetStream::Set(const LightsState *ls)
{
	if(static_cast<SextetImpl*>(_impl) != nullptr)
	{
		static_cast<SextetImpl*>(_impl)->Set(ls);
	}
}

// LightsDriver_SextetStreamToFile implementation

REGISTER_LIGHTS_DRIVER_CLASS(SextetStreamToFile);

#if defined(_WIN32)
	#define DEFAULT_OUTPUT_FILENAME "\\\\.\\pipe\\StepMania-Lights-SextetStream"
#else
	#define DEFAULT_OUTPUT_FILENAME "Data/StepMania-Lights-SextetStream.out"
#endif
static Preference<RString> g_sSextetStreamOutputFilename("SextetStreamOutputFilename", DEFAULT_OUTPUT_FILENAME);

inline RageFile * openOutputStream(const RString& filename)
{
	RageFile * file = new RageFile;

	if(!file->Open(filename, RageFile::WRITE|RageFile::STREAMED))
	{
		LOG->Warn("Error opening file '%s' for output: %s", filename.c_str(), file->GetError().c_str());
		RageUtil::SafeDelete(file);
		file = nullptr;
	}

	return file;
}

#ifdef _WIN32
LightsDriver_SextetStreamToFile::LightsDriver_SextetStreamToFile(HANDLE file)
{
	_impl = new SextetImpl(file);
}
#else
LightsDriver_SextetStreamToFile::LightsDriver_SextetStreamToFile(RageFile* file)
{
	_impl = new SextetImpl(file);
}
#endif // _WIN32





LightsDriver_SextetStreamToFile::LightsDriver_SextetStreamToFile(const RString& filename)
{
#ifdef WINDOWS
	_impl = new SextetImpl(CreateFile(
		filename.c_str(),  // pipe name 
		GENERIC_WRITE,
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe 
		0,              // default attributes 
		NULL));
#else
	_impl = new SextetImpl(openOutputStream(filename));
#endif
}

LightsDriver_SextetStreamToFile::LightsDriver_SextetStreamToFile()
{
#ifdef WINDOWS
	_impl = new SextetImpl(CreateFile(
		g_sSextetStreamOutputFilename.Get().c_str(),   // pipe name 
		GENERIC_WRITE,
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe 
		0,              // default attributes 
		NULL));
#else
	_impl = new SextetImpl(openOutputStream(g_sSextetStreamOutputFilename));
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
