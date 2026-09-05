#include "RageSoundDriver_WASAPI_SubDriver.h"

#include "RageUtil.h"
#include "StdString.h"

namespace {
const char* const kWasapiSubDriverSharedLL = "SharedLL";
const char* const kWasapiSubDriverShared = "Shared";
const char* const kWasapiSubDriverExclusive = "Exclusive";
}  // namespace

std::unique_ptr<WasapiSubDriver> CreateWasapiSubDriverByName(
    const std::string& sName) {
  std::string sCanonicalName = sName;
  Trim(sCanonicalName);

  if (EqualsNoCase(sCanonicalName, kWasapiSubDriverSharedLL)) {
    return CreateWasapiSharedLLSubDriver();
  }

  if (EqualsNoCase(sCanonicalName, kWasapiSubDriverShared)) {
    return CreateWasapiSharedSubDriver();
  }

  if (EqualsNoCase(sCanonicalName, kWasapiSubDriverExclusive)) {
    return CreateWasapiExclusiveSubDriver();
  }

  return nullptr;
}

const char* GetWasapiSubDriverValidNames() {
  return "SharedLL,Shared,Exclusive";
}
