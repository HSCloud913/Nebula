//
// Created by nebula on 24. 11. 10.
//

#pragma once
#include <stack>
#include "Base/Type.h"

BEGIN_NS(ne)
	class ICommand
	{
	public:
		explicit ICommand() = default;
		virtual ~ICommand() = default;

	public:
		virtual void_t Execute() = 0;
		virtual void_t Undo() = 0;
	};

	class IInvoker
	{
	public:
		explicit IInvoker() = default;
		virtual ~IInvoker() = default;

	private:
		std::stack<std::shared_ptr<ICommand>> commandHistory;
		std::stack<std::shared_ptr<ICommand>> undoHistory;

	public:
		void_t Push(std::shared_ptr<ICommand> _command)
		{
			commandHistory.push(std::move(_command));
		}

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
