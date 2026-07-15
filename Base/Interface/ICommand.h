//
// Created by nebula on 24. 11. 10.
//

#pragma once
#include <stack>
#include "Base/Type.h"

BEGIN_NS(ne)
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

	public:
		virtual void_t Execute() = 0;
		virtual void_t Undo() = 0;
	};

	/**
	 * @class IInvoker
	 * @brief ICommand 실행 이력을 스택으로 관리해 실행/취소/재실행을 제공하는 호출자입니다.
	 *
	 * Push()로 커맨드를 등록하고, Execute()가 대기 중인 커맨드를 실행하며, Undo()/Redo()가
	 * commandHistory와 undoHistory 두 스택 사이에서 커맨드를 이동시킵니다.
	 */
	class IInvoker
	{
	public:
		explicit IInvoker() = default;
		virtual ~IInvoker() = default;

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

		void_t Undo()
		{
			if (undoHistory.empty()) return;

			auto command = undoHistory.top();
			command->Undo();
			undoHistory.push(std::move(command));
		}

		void_t Redo()
		{
			if (undoHistory.empty()) return;

			auto command = undoHistory.top();
			command->Execute();
			undoHistory.pop();
			commandHistory.push(std::move(command));
		}
	};

END_NS
