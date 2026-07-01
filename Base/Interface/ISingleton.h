//
// Created by nebula on 24. 5. 17.
//

#pragma once
#include <memory>
#include "Type.h"

BEGIN_NS(ne)
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
