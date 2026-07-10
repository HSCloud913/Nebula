//
// Created by hscloud on 26. 6. 30.
//

#include <gtest/gtest.h>
#include "Base/Type.h"

#if defined(_WIN32)
#	include <winsock2.h>

struct WinsockEnvironment :public ::testing::Environment
{
	void SetUp() override
	{
		WSADATA data{};
		::WSAStartup(MAKEWORD(2, 2), &data);
	}
	void TearDown() override { ::WSACleanup(); }
};
#endif

ne::int_t main(ne::int_t _argc, ne::char_t** _argv)
{
	::testing::InitGoogleTest(&_argc, _argv);

	#if defined(_WIN32)
	::testing::AddGlobalTestEnvironment(new WinsockEnvironment);
	#endif

	return RUN_ALL_TESTS();
}
