//
// Created by nebula on 24. 11. 10.
//

#pragma once
#include <memory>
#include <stack>
#include "Base/Type.h"

namespace ne
{
	/**
	 * @class ICommand
	 * @brief Command 패턴의 커맨드 인터페이스입니다.
	 *
	 * 실행(Execute)과 되돌리기(Undo)를 한 쌍으로 구현해야 하며, IInvoker가 이 둘을
	 * 스택으로 관리해 실행 취소/재실행을 제공합니다.
	 */
	class ICommand
	{
	public:
		explicit ICommand() = default;
		virtual ~ICommand() = default;

		NEBULA_NON_COPYABLE_MOVABLE(ICommand)

	public:
		virtual void_t Execute() = 0;
		virtual void_t Undo() = 0;
	};

	/**
	 * @class IInvoker
	 * @brief ICommand 실행 이력을 스택으로 관리해 실행/취소/재실행을 제공하는 호출자입니다.
	 *
	 * 커맨드는 두 스택 사이를 **이동**합니다(양쪽에 동시에 존재하지 않습니다):
	 * - `commandHistory` — 아직 실행되지 않았거나 취소되어 되돌아온 커맨드
	 * - `undoHistory` — 실행되어 되돌릴 수 있는 커맨드
	 *
	 * Push() 로 등록하면 commandHistory 로, Execute() 는 commandHistory → undoHistory 로,
	 * Undo() 는 undoHistory → commandHistory 로 옮깁니다. Redo() 는 취소된 커맨드를 다시
	 * 실행하는 것이므로 Execute() 와 같은 이동을 합니다(의미상의 별칭).
	 */
	class IInvoker
	{
	public:
		explicit IInvoker() = default;
		virtual ~IInvoker() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IInvoker)

	private:
		std::stack<std::shared_ptr<ICommand>> commandHistory;
		std::stack<std::shared_ptr<ICommand>> undoHistory;

	public:
		void_t Push(std::shared_ptr<ICommand> _command) { commandHistory.push(std::move(_command)); }

		void_t Execute()
		{
			if (commandHistory.empty()) return;

			auto command = commandHistory.top();
			command->Execute();
			commandHistory.pop();
			undoHistory.push(std::move(command));
		}

		/** @brief 마지막으로 실행된 커맨드를 되돌리고 실행 대기 스택으로 돌려보냅니다. */
		void_t Undo()
		{
			if (undoHistory.empty()) return;

			auto command = undoHistory.top();
			command->Undo();
			undoHistory.pop(); // pop 을 빠뜨리면 같은 커맨드가 스택에 중복 적재된다
			commandHistory.push(std::move(command));
		}

		/** @brief 취소된(또는 대기 중인) 커맨드를 다시 실행합니다 — Execute() 와 동일한 이동입니다. */
		void_t Redo() { Execute(); }
	};
}
