//
// Created by hsclo on 24. 5. 17.
//

#ifndef ISINGLETON_H
#define ISINGLETON_H

#include <memory>
#include "Type.h"

BEGIN_NS(ne)
	template <typename T>
	class ISingleton
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(ISingleton)

	protected:
		explicit ISingleton() = default;
		virtual ~ISingleton() = default;

	public:
		static T& GetInstance() noexcept
		{
			static auto instance = std::make_unique<T>();

			return *instance;
		}
	};

END_NS

#endif //ISINGLETON_H
