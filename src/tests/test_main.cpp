#include <gtest/gtest.h>

#include <string>

#include "LuaManager.h"
#include "RageFileManager.h"
#include "RageLog.h"

namespace {

void InitTestEnvironment(const std::string& argv0) {
  LUA = new LuaManager;
  LOG = new RageLog;
  LOG->SetShowLogOutput(false);
  LOG->SetFlushing(true);
  FILEMAN = new RageFileManager(argv0);
  FILEMAN->Mount("dir", ".", "/");
}

void ShutdownTestEnvironment() {
  delete FILEMAN;
  FILEMAN = nullptr;
  delete LOG;
  LOG = nullptr;
  delete LUA;
  LUA = nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  InitTestEnvironment(argc > 0 ? argv[0] : std::string());
  const int result = RUN_ALL_TESTS();
  ShutdownTestEnvironment();
  return result;
}
