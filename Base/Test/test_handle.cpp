//
// Created by hsclo on 24. 10. 21.
//

#include <gtest/gtest.h>
#include "NebulaHandle.h"

struct TestDeleter
{
	void operator()(int _handle) const
	{
		// 핸들을 삭제하는 로직 (예: 메모리 해제)
	}
};

using HandleType = int;

class NebulaHandleTest :public ::testing::Test
{
protected:
	static constexpr HandleType InvalidHandle = -1;

	// 핸들 객체를 테스트하는 데 사용할 수 있습니다.
	NebulaHandle<HandleType, TestDeleter, InvalidHandle> handle;
};



// 기본 생성자와 소멸자 테스트
TEST_F(NebulaHandleTest, DefaultConstructor)
{
	EXPECT_EQ(handle.Get(), InvalidHandle); // 기본 생성자는 InvalidHandle이어야 합니다.
}

// 이동 생성자 테스트
TEST_F(NebulaHandleTest, MoveConstructor)
{
	NebulaHandle<HandleType, TestDeleter, InvalidHandle> handle1(42); // 유효한 핸들
	NebulaHandle<HandleType, TestDeleter, InvalidHandle> handle2(std::move(handle1));

	EXPECT_EQ(handle2.Get(), 42); // 이동 후 핸들 값 확인
	EXPECT_EQ(handle1.Get(), InvalidHandle); // 원래 핸들은 InvalidHandle이어야 함
}

// 이동 대입 연산자 테스트
TEST_F(NebulaHandleTest, MoveAssignmentOperator)
{
	NebulaHandle<HandleType, TestDeleter, InvalidHandle> handle1(42);
	NebulaHandle<HandleType, TestDeleter, InvalidHandle> handle2 = std::move(handle1);

	EXPECT_EQ(handle2.Get(), 42); // 이동 후 핸들 값 확인
	EXPECT_EQ(handle1.Get(), InvalidHandle); // 원래 핸들은 InvalidHandle이어야 함
}

// 핸들 설정 테스트
TEST_F(NebulaHandleTest, AssignmentOperator)
{
	handle = 42; // 핸들을 설정
	EXPECT_EQ(handle.Get(), 42); // 핸들이 올바르게 설정되었는지 확인
}

// Close 메서드 테스트
TEST_F(NebulaHandleTest, CloseMethod)
{
	handle = 42; // 유효한 핸들 설정
	handle.~NebulaHandle(); // 핸들 닫기
	EXPECT_EQ(handle.Get(), InvalidHandle); // 닫은 후 핸들은 InvalidHandle이어야 함
}

// 핸들 변환 연산자 테스트
TEST_F(NebulaHandleTest, ConversionOperator)
{
	handle = 42; // 유효한 핸들 설정
	EXPECT_EQ(static_cast<HandleType>(handle), 42); // 변환 연산자가 올바르게 작동하는지 확인
}

// 존재하지 않는 핸들 테스트
TEST_F(NebulaHandleTest, InvalidHandle)
{
	NebulaHandle<HandleType, TestDeleter, InvalidHandle> invalidHandle; // 기본 생성자로 InvalidHandle 생성
	EXPECT_TRUE(static_cast<bool>(invalidHandle) == false); // InvalidHandle은 false로 평가되어야 함
}
