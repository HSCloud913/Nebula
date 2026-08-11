//
// Created by hscloud on 26. 6. 30.
//

// Windows 에서 소켓 API 는 프로세스당 한 번 WSAStartup 이 선행되어야 하므로, 개별 테스트가 아니라
// gtest 전역 환경으로 등록한다. main() 을 직접 두지 않고 정적 초기화 시점에 등록하므로
// GTest::gtest_main 을 그대로 링크할 수 있다(전역 환경 목록은 RUN_ALL_TESTS() 에서 소비된다).

#include <gtest/gtest.h>

#if defined(_WIN32)
#include "Base/WinsockApi.h"

namespace
{
	struct WinsockEnvironment final :public ::testing::Environment
	{
		void SetUp() override
		{
			WSADATA data{};
			::WSAStartup(MAKEWORD(2, 2), &data);
		}

		void TearDown() override { ::WSACleanup(); }
	};

	const ::testing::Environment* winsockEnvironment = ::testing::AddGlobalTestEnvironment(new WinsockEnvironment);
}
#endif
