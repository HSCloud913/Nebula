#include <gtest/gtest.h>
#include "AsyncFile.h"
#include <cstring>
#include <filesystem>

using namespace ne::io;

namespace fs = std::filesystem;

TEST(AsyncFileTest, CreateAndWrite)
{
    const ne::string_t path = "test_asyncfile_tmp.bin";

    auto cr = AsyncFile::Create(path);
    ASSERT_TRUE(cr.IsOk()) << cr.Error().What();

    AsyncFile file = std::move(cr.Value());
    EXPECT_TRUE(file.IsOpen());

    const ne::byte_t data[] = { 1, 2, 3, 4, 5 };
    auto task = file.Write(std::span<const ne::byte_t>(data, sizeof(data)), 0);
    task.Resume();
    EXPECT_TRUE(task.IsReady());

    (void)file.Close();
    fs::remove(path);
}

TEST(AsyncFileTest, OpenNonExistent)
{
    auto r = AsyncFile::Open("does_not_exist_12345.bin");
    EXPECT_TRUE(r.IsError());
}
