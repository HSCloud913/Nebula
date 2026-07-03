//
// Created by nebula on 24. 11. 3.
//

//#	define SEM_VALUE_MAX   INT_MAX
#include <bits/c++config.h>   // 매크로 정의 + 인클루드 가드 트립
#ifdef _GLIBCXX_HAVE_POSIX_SEMAPHORE
#  undef _GLIBCXX_HAVE_POSIX_SEMAPHORE
#endif
#include <gtest/gtest.h>

#include "Type.h"



ne::int_t main(ne::int_t _argc, ne::char_t** _argv)
{
	::testing::InitGoogleTest(&_argc, _argv);

	return RUN_ALL_TESTS();
}
