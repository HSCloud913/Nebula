//
// Created by nebula on 24. 11. 3.
//

#include <bits/c++config.h>   // 매크로 정의 + 인클루드 가드 트립
#ifdef _GLIBCXX_HAVE_POSIX_SEMAPHORE
#  undef _GLIBCXX_HAVE_POSIX_SEMAPHORE
#endif
#include <gtest/gtest.h>
#include <algorithm>
#include <string>

#include "Ipc/SharedMemory.h"

using ne::ipc::SharedMemory;

namespace
{
	std::span<const std::byte> AsBytes(const std::string& _string) { return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size()); }

	std::string AsString(const std::span<const std::byte> _bytes) { return std::string(reinterpret_cast<const ne::char_t*>(_bytes.data()), _bytes.size()); }
}

TEST(SharedMemoryTest, ViewIsSizedAsRequested)
{
	const auto memory = SharedMemory("nebula-shm-test-size", 128);
	EXPECT_EQ(memory.GetView().size(), 128u);
}

TEST(SharedMemoryTest, SharedBetweenTwoHandles)
{
	auto writer = SharedMemory("nebula-shm-test-share", 64);
	const auto message = std::string("nebula-shared-memory");
	std::ranges::copy(AsBytes(message), writer.GetView().begin());

	auto reader = SharedMemory("nebula-shm-test-share", 64);
	EXPECT_EQ(AsString(reader.GetView().first(message.size())), message);
}
