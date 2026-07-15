//
// Created by nebula on 24. 5. 17.
//

#pragma once
#include <memory>
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class ISingleton
	 * @brief 파생 클래스 T를 프로세스 전역에서 단일 인스턴스로 관리하는 CRTP 베이스입니다.
	 *
	 * 최초 GetInstance() 호출 시 T를 지연 생성하며, 이후 동일 인스턴스를 반환합니다.
	 * 복사/이동은 금지됩니다.
	 *
	 * @tparam T ISingleton을 상속하는 파생 클래스 자기 자신(CRTP).
	 */
	template <typename T>
	class ISingleton
	{
	protected:
		explicit ISingleton() = default;
		virtual ~ISingleton() = default;

	public:
		NEBULA_NON_COPYABLE_MOVABLE(ISingleton)

	public:
		static T& GetInstance() noexcept
		{
			static auto instance = std::unique_ptr<T>(new T());

			return *instance;
		}
	};

END_NS
