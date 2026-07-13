#include <gtest/gtest.h>

#if defined(_WIN32)

#include <chrono>
#include <cstring>
#include <span>
#include "Io/Context/Context.h" // winsock2 → windows 순서를 IoType.h 가 보장(DeleteFileA 포함)
#include "Io/File/File.h"
#include "Base/Coroutine/Task.h"
#include "Io/Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	template <typename T>
	T Drive(Context& _context, ne::Task<T>& _task)
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 50 });
		return _task.await_resume();
	}
}

// ── File: 비동기 Write → Read 왕복 (Level 3) ──
TEST(FileTest, WriteThenRead)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	const lpcstr_t path = "test_file_level3.bin";
	auto opened = File::Open(context, path, OpenMode::READ_WRITE);
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

// ── File: scatter/gather Writev → Readv 왕복 ──
TEST(FileTest, WritevThenReadvRoundTrip)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	const lpcstr_t path = "test_file_level3_vectored.bin";
	auto opened = File::Open(context, path, OpenMode::READ_WRITE);
	ASSERT_TRUE(opened.IsOk()) << opened.Error().What();
	File file = std::move(opened.Value());

	char partA[] = "hello-";
	char partB[] = "vectored-";
	char partC[] = "world";
	BufferChain writeChain;
	writeChain.Append(BufferView{ reinterpret_cast<ne::byte_t*>(partA), sizeof(partA) - 1 });
	writeChain.Append(BufferView{ reinterpret_cast<ne::byte_t*>(partB), sizeof(partB) - 1 });
	writeChain.Append(BufferView{ reinterpret_cast<ne::byte_t*>(partC), sizeof(partC) - 1 });
	const std::size_t totalLength = writeChain.TotalSize();

	auto writeTask = file.Writev(writeChain, 0);
	auto writeResult = Drive(context, writeTask);
	ASSERT_TRUE(writeResult.IsOk()) << writeResult.Error().What();
	EXPECT_EQ(writeResult.Value(), totalLength);

	ne::byte_t bufferA[6]{};
	ne::byte_t bufferB[9]{};
	ne::byte_t bufferC[5]{};
	BufferChain readChain;
	readChain.Append(BufferView{ bufferA, sizeof(bufferA) });
	readChain.Append(BufferView{ bufferB, sizeof(bufferB) });
	readChain.Append(BufferView{ bufferC, sizeof(bufferC) });

	auto readTask = file.Readv(readChain, 0);
	auto readResult = Drive(context, readTask);
	ASSERT_TRUE(readResult.IsOk()) << readResult.Error().What();
	EXPECT_EQ(readResult.Value(), totalLength);
	EXPECT_EQ(std::memcmp(bufferA, partA, sizeof(bufferA)), 0);
	EXPECT_EQ(std::memcmp(bufferB, partB, sizeof(bufferB)), 0);
	EXPECT_EQ(std::memcmp(bufferC, partC, sizeof(bufferC)), 0);

	EXPECT_TRUE(file.Close().IsOk());
	::DeleteFileA(path);
}
TEST(FileTest, OpenNonExistentFails)
{
	IocpEngine engine;
	Context context{ engine };

	auto opened = File::Open(context, "no_such_file_level3.bin", OpenMode::READ);
	EXPECT_TRUE(opened.IsError());
}

#endif // _WIN32
