#include <gtest/gtest.h>

#if defined(_WIN32)

#include <chrono>
#include <cstring>
#include <span>
#include "IoContext.h" // winsock2 → windows 순서를 IoType.h 가 보장(DeleteFileA 포함)
#include "File/File.h"
#include "Coroutine/Task.h"
#include "Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	template <typename T>
	T Drive(IoContext& _context, ne::Task<T>& _task)
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline)
			(void)_context.RunOnce(std::chrono::milliseconds{ 50 });
		return _task.await_resume();
	}
}

// ── File: 비동기 Write → Read 왕복 (Level 3) ──
TEST(FileTest, WriteThenRead)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	IoContext context{ engine };

	const lpcstr_t path = "test_file_level3.bin";
	auto opened = File::Open(context, path, OpenMode::ReadWrite);
	ASSERT_TRUE(opened.IsOk()) << opened.Error().What();
	File file = std::move(opened.Value());

	const char payload[] = "level3-async-file";
	const std::size_t length = sizeof(payload) - 1;

	auto writeTask = file.Write(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), length }, 0);
	auto writeResult = Drive(context, writeTask);
	ASSERT_TRUE(writeResult.IsOk()) << writeResult.Error().What();
	EXPECT_EQ(writeResult.Value(), length);

	ne::byte_t buffer[32]{};
	auto readTask = file.Read(std::span<ne::byte_t>{ buffer, length }, 0);
	auto readResult = Drive(context, readTask);
	ASSERT_TRUE(readResult.IsOk()) << readResult.Error().What();
	EXPECT_EQ(readResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

	EXPECT_TRUE(file.Close().IsOk());
	EXPECT_FALSE(file.IsValid());
	::DeleteFileA(path);
}

TEST(FileTest, OpenNonExistentFails)
{
	IocpEngine engine;
	IoContext context{ engine };

	auto opened = File::Open(context, "no_such_file_level3.bin", OpenMode::Read);
	EXPECT_TRUE(opened.IsError());
}

#endif // _WIN32
