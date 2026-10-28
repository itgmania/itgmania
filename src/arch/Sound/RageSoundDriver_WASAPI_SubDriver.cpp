#include "RageSoundDriver_WASAPI_SubDriver.h"

#include "RageUtil.h"

namespace {
const char* const kWasapiSubDriverSharedLowLatency = "LowestLatency";
const char* const kWasapiSubDriverShared = "Shared";
const char* const kWasapiSubDriverExclusive = "Exclusive";
}  // namespace

std::unique_ptr<WasapiSubDriver> CreateWasapiSubDriverByName(
    const std::string& sName) {
  std::string sCanonicalName = sName;
  Trim(sCanonicalName);

  if (sCanonicalName == kWasapiSubDriverSharedLowLatency) {
    return CreateWasapiSharedLLSubDriver();
  }

  if (sCanonicalName == kWasapiSubDriverShared) {
    return CreateWasapiSharedSubDriver();
  }

  if (sCanonicalName == kWasapiSubDriverExclusive) {
    return CreateWasapiExclusiveSubDriver();
  }

  return nullptr;
}

const char* GetWasapiSubDriverValidNames() {
  static const std::string names =
      std::string(kWasapiSubDriverSharedLowLatency) + "," +
      kWasapiSubDriverShared + "," +
      kWasapiSubDriverExclusive;
  return names.c_str();
}
