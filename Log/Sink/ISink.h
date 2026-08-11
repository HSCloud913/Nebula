//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include "Base/Type.h"
#include "Log/LogRecord.h"

namespace ne::log
{
	/**
	 * @class ISink
	 * @brief 로그 출력 대상 추상화입니다.
	 *
	 * Logger 의 백엔드 스레드가 레코드마다 Write() 를, 강제/FATAL flush 시 Flush() 를 호출합니다.
	 * @note 구현체는 스레드 안전할 필요가 없습니다 — Write()/Flush() 는 항상 Logger 백엔드(단일 스레드)
	 *       또는 join 후 소멸자 스레드에서만, 서로 겹치지 않게 호출됩니다.
	 */
	class ISink
	{
	public:
		ISink() = default;
		virtual ~ISink() = default;

		NEBULA_NON_COPYABLE_MOVABLE(ISink)

	public:
		virtual void_t Write(const LogRecord& _record) = 0;
		virtual void_t Flush() = 0;
	};
}
