//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "Base/Interface/ICommand.h"



class TestCommand final : public ne::ICommand
{
public:
	virtual void Execute() override
	{
		isExecuted = true;
	}

	virtual void Undo() override
	{
		isExecuted = false;
	}

	[[nodiscard]] bool IsExecuted() const
	{
		return isExecuted;
	}

private:
	bool isExecuted = false;
};

class CommandTest : public ::testing::Test
{
protected:
	std::unique_ptr<ne::IInvoker> invoker;
	std::shared_ptr<TestCommand> command;

	void SetUp() override
	{
		invoker = std::make_unique<ne::IInvoker>();
		command = std::make_shared<TestCommand>();
	}
};



TEST_F(CommandTest, ExecuteCommand)
{
	invoker->Push(command);
	invoker->Execute();
	EXPECT_TRUE(command->IsExecuted());
}

TEST_F(CommandTest, UndoCommand)
{
	invoker->Push(command);
	invoker->Execute();
	invoker->Undo();
	EXPECT_FALSE(command->IsExecuted());
}

TEST_F(CommandTest, RedoCommand)
{
	invoker->Push(command);
	invoker->Execute();
	invoker->Undo();
	invoker->Redo();
	EXPECT_TRUE(command->IsExecuted());
}

TEST_F(CommandTest, UndoEmptyHistory)
{
	invoker->Undo();
	EXPECT_FALSE(command->IsExecuted());
}

TEST_F(CommandTest, RedoEmptyHistory)
{
	invoker->Redo();
	EXPECT_FALSE(command->IsExecuted());
}
