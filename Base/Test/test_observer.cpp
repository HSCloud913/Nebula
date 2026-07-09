//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "Base/Interface/IObserver.h"



class TestObserver :public ne::IObserver
{
public:
	explicit TestObserver(const ne::uint_t _id)
		: IObserver(_id)
	{
	}

public:
	ne::string_t stringData;
	ne::int_t numberData = 0;

public:
	virtual void Update(const std::any& _data) override
	{
		try
		{
			if (_data.type() == typeid(ne::string_t))
			{
				stringData = std::any_cast<ne::string_t>(_data);
			}
			else if (_data.type() == typeid(ne::int_t))
			{
				numberData = std::any_cast<ne::int_t>(_data);
			}
			else
			{
				stringData = "Unknown type";
			}
		} catch (const std::bad_any_cast& e)
		{
			stringData = "Bad cast!";
		}
	}
};

class TestSubject : public ne::ISubject
{

};



TEST(NebulaObserverTest, AttachObserver)
{
	TestSubject subject;
	auto observer1 = std::make_shared<TestObserver>(1);
	auto observer2 = std::make_shared<TestObserver>(2);

	subject.Attach(observer1);
	subject.Attach(observer2);

	subject.Notify(ne::string_t("123"));

	EXPECT_EQ(observer1->stringData, "123");
	EXPECT_EQ(observer2->stringData, "123");
}

TEST(NebulaObserverTest, DetachObserver)
{
	TestSubject subject;
	auto observer1 = std::make_shared<TestObserver>(1);
	auto observer2 = std::make_shared<TestObserver>(2);

	subject.Attach(observer1);
	subject.Attach(observer2);

	subject.Detach(1);

	subject.Notify(ne::string_t("123"));

	EXPECT_NE(observer1->stringData, "123");
	EXPECT_EQ(observer2->stringData, "123");
}
