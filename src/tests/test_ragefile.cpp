#include <gtest/gtest.h>

#include <string>

#include "RageFile.h"
#include "RageFileManager.h"

namespace {

const char* kTestDir = "/unit_test_ragefile/";

void RemoveTestDir() {
  // FIXME: CreateDir leaves a newdir.temp.newdir file behind, and the stale
  // cache hides it from DeleteRecursive. Flush first so cleanup can see it.
  FILEMAN->FlushDirCache(kTestDir);
  FILEMAN->DeleteRecursive(kTestDir);
}

class RageFileManagerCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { RemoveTestDir(); }
  void TearDown() override { RemoveTestDir(); }
};

}  // namespace

TEST_F(RageFileManagerCacheTest, CreateDirUpdatesParentCache) {
  ASSERT_FALSE(FILEMAN->DoesFileExist(kTestDir));

  FILEMAN->CreateDir(kTestDir);

  EXPECT_TRUE(FILEMAN->DoesFileExist(kTestDir));
  EXPECT_TRUE(FILEMAN->IsADirectory(kTestDir));
  EXPECT_EQ(FILEMAN->GetFileType(kTestDir), RageFileManager::TYPE_DIR);
}

TEST_F(RageFileManagerCacheTest, CannotOpenDirectoryForWrite) {
  FILEMAN->CreateDir(kTestDir);

  RageFile file;
  EXPECT_FALSE(file.Open(kTestDir, RageFile::WRITE));
}

TEST_F(RageFileManagerCacheTest, ImplicitDirectoryCreationUpdatesCache) {
  const std::string nestedFile = std::string(kTestDir) + "a/b/c/file.txt";

  RageFile file;
  ASSERT_TRUE(file.Open(nestedFile, RageFile::WRITE));
  ASSERT_EQ(file.Write("hello"), 5);
  file.Close();

  EXPECT_TRUE(FILEMAN->IsADirectory(std::string(kTestDir) + "a/"));
  EXPECT_TRUE(FILEMAN->IsADirectory(std::string(kTestDir) + "a/b/"));
  EXPECT_TRUE(FILEMAN->IsADirectory(std::string(kTestDir) + "a/b/c/"));
  EXPECT_TRUE(FILEMAN->IsAFile(nestedFile));

  RageFile readFile;
  ASSERT_TRUE(readFile.Open(nestedFile, RageFile::READ));
  std::string data;
  ASSERT_NE(readFile.Read(data), -1);
  EXPECT_EQ(data, "hello");
}
