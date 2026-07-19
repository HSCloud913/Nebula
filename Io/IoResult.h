//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <utility>
#include "Base/Result.h"
#include "Io/IoError.h"

BEGIN_NS(ne::io)
	template <typename T>
	using IoResult = ne::Result<T, IoError>;

	/**
	 * @class IoFailure
	 * @brief CO_TRY/CO_TRYV 매크로가 에러 전파에 쓰는 암묵 변환 어댑터.
	 *
	 * ne::Result 의 생성자가 private 이라 co_return 에 IoError 를 직접 넘길 수 없어,
	 * 이 래퍼가 어떤 IoResult<U> 로든 암묵 변환되며 내부에서 Error() 팩토리를 호출한다.
	 */
	struct IoFailure
	{
		IoError error;

		template <typename T>
		operator ne::Result<T, IoError>() && { return ne::Result<T, IoError>::Error(std::move(error)); }
	};

END_NS

#define CO_TRY(_var, _expr)                                                              \
	auto _coTry_##_var = (_expr);                                                        \
	if (_coTry_##_var.IsError()) co_return ne::io::IoFailure{ std::move(_coTry_##_var.Error()) }; \
	auto _var = std::move(_coTry_##_var.Value())

#define CO_TRYV(_expr)                                                                   \
	do {                                                                                 \
		auto _coTryVoid = (_expr);                                                       \
		if (_coTryVoid.IsError()) co_return ne::io::IoFailure{ std::move(_coTryVoid.Error()) }; \
	} while (false)
