#include <gtest/gtest.h>
#include "File/MemoryMapFile.h"
#include <filesystem>
#include <fstream>

using namespace ne::io;
namespace fs = std::filesystem;

TEST(MmapFileTest, OpenAndRead)
{
    const ne::string_t path = "test_mmap_tmp.bin";
    {
        std::ofstream ofs(path, std::ios::binary);
        const char payload[] = "hello mmap";
        ofs.write(payload, sizeof(payload) - 1);
    }

    auto r = MemoryMapFile::Open(path);
    ASSERT_TRUE(r.IsOk()) << r.Error().What();

    MemoryMapFile f = std::move(r.Value());
    EXPECT_TRUE(f.IsOpen());
    EXPECT_EQ(f.Size(), 10u);
    EXPECT_EQ(f.Data()[0], static_cast<ne::byte_t>('h'));

    f.Close();
    fs::remove(path);
}

TEST(MmapFileTest, OpenNonExistent)
{
    auto r = MemoryMapFile::Open("does_not_exist_12345.bin");
    EXPECT_TRUE(r.IsError());
}
