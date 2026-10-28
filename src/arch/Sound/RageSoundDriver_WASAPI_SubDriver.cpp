#include "RageSoundDriver_WASAPI_SubDriver.h"

#include <map>

#include "RageUtil.h"

namespace {
using WasapiSubDriverRegistry = std::map<std::string, WasapiSubDriverFactory>;

WasapiSubDriverRegistry& GetWasapiSubDriverRegistry() {
  static WasapiSubDriverRegistry registry;
  return registry;
}
}  // namespace

void RegisterWasapiSubDriver(
    const std::string& sName, WasapiSubDriverFactory pFactory) {
  GetWasapiSubDriverRegistry()[sName] = pFactory;
}

std::unique_ptr<WasapiSubDriver> CreateWasapiSubDriverByName(
    const std::string& sName) {
  std::string sCanonicalName = sName;
  Trim(sCanonicalName);

  const auto& registry = GetWasapiSubDriverRegistry();
  const auto it = registry.find(sCanonicalName);
  if (it != registry.end()) {
    return it->second();
  }

  return nullptr;
}
