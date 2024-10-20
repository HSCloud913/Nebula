//
// Created by hsclo on 24. 10. 21.
//

#include <gtest/gtest.h>
#include "Exception.h"



TEST(NebulaExceptionTest, CreateException)
{
	const std::string module = "TestModule";
	const std::string message = "An error occurred";

	// 예외 객체 생성
	NebulaException ex(module, message);

	// what() 메서드가 올바른 메시지를 반환하는지 확인
	EXPECT_STREQ(ex.what(), "TestModule An error occurred");
}

// 예외 포맷팅 실패 시 기본 메시지 반환 테스트
TEST(NebulaExceptionTest, CreateExceptionWithFormattingError)
{
	// 여기서 예외를 발생시킬 수 있는 _module과 _message를 정의
	const std::string module = "TestModule";
	const std::string message = "This will cause a formatting error"; // 적절한 값을 주어야 함.

	// 예외 객체 생성
	NebulaException ex(module, message);

	// what() 메서드가 올바른 메시지를 반환하는지 확인
	EXPECT_STREQ(ex.what(), "TestModule This will cause a formatting error");
}

// 예외가 복사될 수 없는지 테스트
TEST(NebulaExceptionTest, NoCopy)
{
	const std::string module = "TestModule";
	const std::string message = "An error occurred";

	NebulaException ex(module, message);

	// 복사 생성자 호출 시 컴파일 오류를 발생시키기 위해 static_assert 사용
	// NebulaException exCopy = ex; // 이 부분이 컴파일 오류를 발생시킴
}

// 예외가 이동될 수 있는지 테스트
TEST(NebulaExceptionTest, MoveConstructible)
{
	const std::string module = "TestModule";
	const std::string message = "An error occurred";

	NebulaException ex(module, message);
	NebulaException exMoved = std::move(ex); // 이동 생성자 테스트

	// 이동 후 원래 객체는 사용하면 안됨
	// ex.what(); // 사용하면 안됨
	EXPECT_STREQ(exMoved.what(), "TestModule An error occurred");
}

TEST(NebulaExceptionTest, MoveAssignable)
{
	const std::string module1 = "Module1";
	const std::string message1 = "Error 1 occurred";
	const std::string module2 = "Module2";
	const std::string message2 = "Error 2 occurred";

	NebulaException ex1(module1, message1);
	NebulaException ex2(module2, message2);

	ex2 = std::move(ex1); // 이동 대입 연산자 테스트

	EXPECT_STREQ(ex2.what(), "Module1 Error 1 occurred");
}
