//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include <iostream>
#include "Base/Type.h"
#include "Log/LogRecord.h"
#include "Log/Sink/ISink.h"

namespace ne::log
{
	/** @brief 표준 스트림 sink. ERROR 이상은 stderr, 그 외는 stdout 으로 보낸다. */
	class ConsoleSink final : public ISink
	{
	public:
		virtual void_t Write(const LogRecord& _record) override
		{
			std::ostream& os = (_record.level >= LogLevel::NE_ERROR) ? std::cerr : std::cout;
			os << FormatRecord(_record) << '\n';
		}

		virtual void_t Flush() override
		{
			std::cout.flush();
			std::cerr.flush();
		}
	};
}
