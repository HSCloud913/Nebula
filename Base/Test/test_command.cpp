//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "Base/Interface/ICommand.h"



class TestCommand final :public ne::ICommand
{
public:
	virtual void Execute() override { isExecuted = true; }

	virtual void Undo() override { isExecuted = false; }

	[[nodiscard]] bool IsExecuted() const { return isExecuted; }

private:
	bool isExecuted = false;
};

class CommandTest :public ::testing::Test
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

// Undo/Redo 를 반복해도 상태가 어긋나지 않아야 한다.
// (과거 Undo() 가 pop 없이 push 해서 같은 커맨드가 스택에 중복 적재되고, Redo 후에는 커맨드가
//  양쪽 스택에 동시에 존재했다 — 1회 왕복만 검증하던 탓에 드러나지 않았다.)
TEST_F(CommandTest, RepeatedUndoRedoCycles)
{
	invoker->Push(command);

	for (int cycle = 0; cycle < 3; ++cycle)
	{
		invoker->Execute();
		EXPECT_TRUE(command->IsExecuted()) << "cycle " << cycle;

		invoker->Undo();
		EXPECT_FALSE(command->IsExecuted()) << "cycle " << cycle;
	}

	invoker->Redo();
	EXPECT_TRUE(command->IsExecuted());
}

// 커맨드가 하나뿐이면 Undo 를 두 번 호출해도 두 번째는 되돌릴 대상이 없어 아무 일도 없어야 한다.
TEST_F(CommandTest, UndoDoesNotRepeatSameCommand)
{
	invoker->Push(command);
	invoker->Execute();

	invoker->Undo();
	ASSERT_FALSE(command->IsExecuted());

	// 두 번째 Undo 는 대상이 없다 — 여기서 같은 커맨드를 또 Undo 하면 스택이 중복 적재된 상태다.
	invoker->Undo();
	EXPECT_FALSE(command->IsExecuted());

	// 대기 스택에는 커맨드가 정확히 하나만 남아 있어야 한다: Redo 는 성공하고, 그 다음 Redo 는 대상이 없다.
	invoker->Redo();
	EXPECT_TRUE(command->IsExecuted());

	command->Undo(); // 커맨드 자체를 직접 되돌려 관찰 상태만 초기화
	invoker->Redo(); // 대기 스택이 비었으므로 아무 일도 없어야 한다
	EXPECT_FALSE(command->IsExecuted());
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
