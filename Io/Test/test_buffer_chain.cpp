//
// Created by hscloud on 26. 7. 6.
//

#include <gtest/gtest.h>
#include <cstring>
#include "Io/Buffer/BufferChain.h"

using namespace ne::io;

namespace
{
	BufferView MakeView(ne::byte_t* _ptr, const std::size_t _length)
	{
		return { _ptr, _length };
	}
}

TEST(BufferChainTest, SuffixZeroReturnsSameContent)
{
	ne::byte_t a[4] = { 1, 2, 3, 4 };
	ne::byte_t b[3] = { 5, 6, 7 };

	BufferChain chain;
	chain.Append(MakeView(a, sizeof(a)));
	chain.Append(MakeView(b, sizeof(b)));

	const auto suffix = chain.Suffix(0);
	ASSERT_EQ(suffix.Segments().size(), 2u);
	EXPECT_EQ(suffix.TotalSize(), chain.TotalSize());
	EXPECT_EQ(suffix.Segments()[0].ptr, a);
	EXPECT_EQ(suffix.Segments()[1].ptr, b);
}

TEST(BufferChainTest, SuffixMidSegmentSlicesFirstSegment)
{
	ne::byte_t a[4] = { 1, 2, 3, 4 };
	ne::byte_t b[3] = { 5, 6, 7 };

	BufferChain chain;
	chain.Append(MakeView(a, sizeof(a)));
	chain.Append(MakeView(b, sizeof(b)));

	// a 의 앞 2바이트를 건너뛰면: a[2..4) + b 전체 = 남은 5바이트.
	const auto suffix = chain.Suffix(2);
	ASSERT_EQ(suffix.Segments().size(), 2u);
	EXPECT_EQ(suffix.TotalSize(), 5u);
	EXPECT_EQ(suffix.Segments()[0].ptr, a + 2);
	EXPECT_EQ(suffix.Segments()[0].length, 2u);
	EXPECT_EQ(suffix.Segments()[1].ptr, b);
	EXPECT_EQ(suffix.Segments()[1].length, 3u);
}

TEST(BufferChainTest, SuffixOnSegmentBoundaryDropsFirstSegmentEntirely)
{
	ne::byte_t a[4] = { 1, 2, 3, 4 };
	ne::byte_t b[3] = { 5, 6, 7 };

	BufferChain chain;
	chain.Append(MakeView(a, sizeof(a)));
	chain.Append(MakeView(b, sizeof(b)));

	// 정확히 a 의 길이만큼 건너뛰면 b만 남는다.
	const auto suffix = chain.Suffix(4);
	ASSERT_EQ(suffix.Segments().size(), 1u);
	EXPECT_EQ(suffix.TotalSize(), 3u);
	EXPECT_EQ(suffix.Segments()[0].ptr, b);
}

TEST(BufferChainTest, SuffixFullLengthIsEmpty)
{
	ne::byte_t a[4] = { 1, 2, 3, 4 };
	ne::byte_t b[3] = { 5, 6, 7 };

	BufferChain chain;
	chain.Append(MakeView(a, sizeof(a)));
	chain.Append(MakeView(b, sizeof(b)));

	const auto suffix = chain.Suffix(chain.TotalSize());
	EXPECT_TRUE(suffix.IsEmpty());
	EXPECT_EQ(suffix.TotalSize(), 0u);
}

TEST(BufferChainTest, TotalSizeSumsAllSegments)
{
	ne::byte_t a[4]{};
	ne::byte_t b[3]{};
	ne::byte_t c[5]{};

	BufferChain chain;
	chain.Append(MakeView(a, sizeof(a)));
	chain.Append(MakeView(b, sizeof(b)));
	chain.Append(MakeView(c, sizeof(c)));

	EXPECT_EQ(chain.TotalSize(), 12u);
}
