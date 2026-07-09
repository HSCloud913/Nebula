//
// Created by hscloud on 26. 7. 8.
//
// Level 2 — 값 기반 에러 처리. 예외 없이 IoResult<T> 로 표현하고 co_await 체인을 따라
// CO_TRY 로 자동 전파한다(Rust ? 스타일). 규칙 1: Base 의 Result.h + Error.h(IoError) 재사용.

#pragma once
#include <utility>
#include "Base/Result.h"
#include "Io/IoError.h"

BEGIN_NS(ne::io)
	// 스펙의 IoResult<T> = ne::Result<T, IoError>. std::variant 대신 기존 Result 를 그대로 쓴다.
	template <typename T>
	using IoResult = ne::Result<T, IoError>;

	// CO_TRY 전파용 어댑터 — 코루틴 반환 타입(IoResult<U>, U 임의)에 맞춰 암묵 변환된다.
	// Result 의 생성자가 private 이라 co_return 에 IoError 를 직접 못 넘기므로, 이 래퍼가
	// 어떤 IoResult<U> 로든 변환되며 내부에서 Error() 팩토리를 호출한다.
	struct IoFailure
	{
		IoError error;

		template <typename T>
		operator ne::Result<T, IoError>() && { return ne::Result<T, IoError>::Error(std::move(error)); }
	};

END_NS

// 값 결과 전파: 성공값을 _var 에 바인딩하고, 에러면 코루틴에서 즉시 co_return 으로 전파한다.
//   사용: CO_TRY(received, stream.Receive(buffer));
#define CO_TRY(_var, _expr)                                                              \
	auto _coTry_##_var = (_expr);                                                        \
	if (_coTry_##_var.IsError()) co_return ne::io::IoFailure{ std::move(_coTry_##_var.Error()) }; \
	auto _var = std::move(_coTry_##_var.Value())

// void 결과 전파: 에러면 co_return 으로 전파, 성공이면 통과.
#define CO_TRYV(_expr)                                                                   \
	do {                                                                                 \
		auto _coTryVoid = (_expr);                                                       \
		if (_coTryVoid.IsError()) co_return ne::io::IoFailure{ std::move(_coTryVoid.Error()) }; \
	} while (false)
