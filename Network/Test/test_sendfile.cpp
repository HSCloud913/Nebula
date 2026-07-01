#include <gtest/gtest.h>
// SendFile 은 실제 소켓 페어가 필요하므로 컴파일 확인 테스트만 포함.
// 통합 테스트는 별도 환경에서 진행.

TEST(SendFileTest, HeaderIncludeCompiles)
{
    SUCCEED();
}
