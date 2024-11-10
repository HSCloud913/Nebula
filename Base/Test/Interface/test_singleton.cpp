//
// Created by hsclo on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "Interface/ISingleton.h"



class ConcreteSingleton :public ne::ISingleton<ConcreteSingleton>
{
	friend class ISingleton;

private:
	int value = 0;

public:
	[[nodiscard]] int GetValue() const
	{
		return value;
	}
	void SetValue(const int _value)
	{
		value = _value;
	}
};



TEST(NebulaSingletonTest, SameInstance)
{
	ConcreteSingleton& instance1 = ConcreteSingleton::GetInstance();
	ConcreteSingleton& instance2 = ConcreteSingleton::GetInstance();
	EXPECT_EQ(&instance1, &instance2);
}

TEST(NebulaSingletonTest, UpdatesCorrectly)
{
	ConcreteSingleton& instance = ConcreteSingleton::GetInstance();
	instance.SetValue(42);
	EXPECT_EQ(instance.GetValue(), 42);
}

TEST(NebulaSingletonTest, NoCopyOrAssignmentAllowed)
{
	ConcreteSingleton& instance1 = ConcreteSingleton::GetInstance();
	ConcreteSingleton& instance2 = ConcreteSingleton::GetInstance();

	// 복사 생성자 호출 시 컴파일 에러
	// ConcreteSingleton instanceCopy = instance1;  // 컴파일 에러

	// 대입 연산자 호출 시 컴파일 에러
	// instance2 = instance1;  // 컴파일 에러

	// 두 인스턴스가 동일한 인스턴스여야 함
	EXPECT_EQ(&instance1, &instance2);
}

TEST(NebulaSingletonTest, ThreadSafe)
{
	constexpr int count = 100; // 스레드 수
	std::vector<std::thread> threads;
	std::vector<ConcreteSingleton*> instances(count);

	for (int i = 0; i < count; ++i)
	{
		threads.emplace_back([&, i]()
		{
			instances[i] = &ConcreteSingleton::GetInstance();
		});
	}

	for (auto& thread : threads) thread.join();

	// 모든 스레드가 동일한 인스턴스를 얻었는지 확인
	for (int i = 1; i < count; ++i)
	{
		EXPECT_EQ(instances[i], instances[0]);
	}
}

TEST(NebulaSingletonTest, ThreadSafeValue)
{
	constexpr int count = 100;
	std::vector<std::thread> threads;

	// 스레드에서 각각 값을 설정
	for (int i = 0; i < count; ++i)
	{
		threads.emplace_back([&]()
		{
			ConcreteSingleton& instance = ConcreteSingleton::GetInstance();
			instance.SetValue(42);
		});
	}

	for (auto& thread : threads) thread.join();

	// 모든 스레드에서 동일한 값이 설정되었는지 확인
	ConcreteSingleton& instance = ConcreteSingleton::GetInstance();
	EXPECT_EQ(instance.GetValue(), 42);
}
