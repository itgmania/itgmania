#include "global.h"
#include "RageSoundDriver.h"
#include "RageSoundManager.h"
#include "RageLog.h"
#include "RageUtil.h"

#include "arch/arch_default.h"

#include <cstddef>
#include <vector>

// Function to initialize the default driver list
std::vector<RString> InitializeDefaultDriverList()
{
	std::vector<RString> driverList;
	split(DEFAULT_SOUND_DRIVER_LIST, ",", driverList);
	return driverList;
}

static std::vector<RString> defaultDriverList = InitializeDefaultDriverList();
DriverList RageSoundDriver::m_pDriverList;
static bool removedUnusableDriver = false;

// AttemptToInitializeDriver is a function that attempts to initialize a sound driver.
// The function takes a driver name as an argument and returns a pointer to the initialized driver.
// If initialization succeeds, we will receive an empty string from Init().
// If the driver fails to initialize, we will log the error message and delete the driver.
RageSoundDriver* AttemptToInitializeDriver(const RString& evaluated_driver)
{
	RageDriver* baseDriver = RageSoundDriver::m_pDriverList.Create(evaluated_driver);
	if (baseDriver == nullptr)
	{
		LOG->Trace("Unknown sound driver: %s", evaluated_driver.c_str());
		return nullptr;
	}
	RageSoundDriver* soundDriver = dynamic_cast<RageSoundDriver*>(baseDriver);
	
	const RString initializationResult = soundDriver->Init();
	if (initializationResult.empty())
	{
		return soundDriver;
	}

	LOG->Info("Couldn't load driver %s:", evaluated_driver.c_str());
	SAFE_DELETE(soundDriver);
	return nullptr;
}

// InitializeDriverFromList is a function that initializes a sound driver from a list of drivers.
// The function takes a vector of driver names as an argument and returns a pointer to the initialized driver,
// or returns a null pointer if no driver was successfully initialized.
RageSoundDriver* InitializeDriverFromList(const std::vector<RString>& driverList)
{
	for (const RString& driverName : driverList)
	{
		RageSoundDriver* initializedDriver = AttemptToInitializeDriver(driverName);
		if (initializedDriver != nullptr)
		{
			LOG->Trace("Sound driver '%s' was loaded successfully.", driverName.c_str());
			return initializedDriver;
		}
	}
	return nullptr; // No driver was successfully initialized.
}

// RageSoundDriver::Create is a function that creates a sound driver.
// It attempts to initialize a driver from the default driver list.
// If the driver is successfully initialized, the function will return the driver and exit.
// If the driver fails to initialize, the function will attempt use the custom driver list.
// The function will ultimately call sm_crash if all attempts to get a sound driver fail.
RageSoundDriver* RageSoundDriver::Create(const RString& drivers)
{
	// Attempt to initialize a driver from the default driver list
	RageSoundDriver* initializedDriver = InitializeDriverFromList(defaultDriverList);
	if (initializedDriver != nullptr)
	{
		// The function will exit here if the driver was successfully initialized via the default list.
		return initializedDriver;
	}

	// If no driver was initialized using the default list, prepare the custom driver list
	std::vector<RString> customDriverList;
	split(drivers, ",", customDriverList);
	std::size_t currentDriverIndex = 0;
	removedUnusableDriver = false;

	// Create a new list to hold only the usable drivers.
	std::vector<RString> usableDriverList;
	for (const auto& driverName : customDriverList)
	{
		auto currentSearchKey = istring(driverName);
		if (m_pDriverList.m_pRegistrees->find(currentSearchKey) != m_pDriverList.m_pRegistrees->end())
		{
			usableDriverList.push_back(driverName);
		}
		else
		{
			LOG->Warn("Removed unusable sound driver %s", driverName.c_str());
			removedUnusableDriver = true;
		}
	}

	// As per the original logic, if any unusable drivers were removed, we will update the custom driver list.
	if (removedUnusableDriver) {
		SOUNDMAN->fix_bogus_sound_driver_pref(join(",", usableDriverList));
	}

	// Attempt to initialize a driver from the custom driver list.
	initializedDriver = InitializeDriverFromList(usableDriverList);
	if (initializedDriver != nullptr)
	{
		return initializedDriver;
	}

	// The function will call sm_crash if all attempts to get a sound driver fail.
	FAIL_M("No sound driver could be loaded.");
	return nullptr;
}

RString RageSoundDriver::GetDefaultSoundDriverList()
{
	return DEFAULT_SOUND_DRIVER_LIST;
}

/*
 * (c) 2002-2006 Glenn Maynard, Steve Checkoway
 * Rewritten 2024 sukibaby
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
