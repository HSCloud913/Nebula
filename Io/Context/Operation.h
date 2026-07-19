//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Io/Buffer/BufferChain.h"

BEGIN_NS(ne::io)
	/**
	 * @class Capability
	 * @brief 엔진이 제공하는 zero-copy / 오버헤드 감소 세부 기능을 나타내는 열거형.
	 *
	 * 엔진 서브클래스가 아니라 런타임 IEngine::Supports(Capability) 질의로만 확인한다.
	 * 기본 Read/Write 는 모든 엔진이 항상 지원한다.
	 */
	enum class Capability : uint_t
	{
		SEND_FILE_ZERO_COPY,
		SEND_MEM_ZERO_COPY,
		RECEIVE_OVERHEAD_REDUCED,
		RECEIVE_TRUE_ZERO_COPY,
	};

	/**
	 * @class RequestKind
	 * @brief 엔진에 제출할 수 있는 I/O 연산 종류를 나타내는 열거형.
	 */
	enum class RequestKind : uint_t
	{
		ACCEPT,
		CONNECT,
		READ,
		WRITE,
		READ_FIXED,
		WRITE_FIXED,
		WAIT_READABLE,
		WAIT_WRITABLE,
		RECEIVE,
		SEND,
		RECEIVE_FROM,
		SEND_TO,
		SEND_ZERO_COPY,
		SEND_FILE,
	};

	/**
	 * @class Request
	 * @brief 엔진에 제출하는 단일 I/O 요청을 나타내는 값 타입.
	 *
	 * 연산 종류(requestKind)에 따라 사용되는 필드가 달라지는 다목적 구조체이다. 완료 시 userData 를
	 * 그대로 Completion 으로 돌려받으므로, 상위 계층은 여기에 코루틴 재개용 컨텍스트 포인터를
	 * 실어 완료를 자신의 코루틴으로 연결한다.
	 */
	struct Request
	{
		RequestKind requestKind{ RequestKind::READ };
		void_t* userData{ nullptr };
		ulonglong_t handle{ 0 };
		void_t* buffer{ nullptr };
		std::size_t length{ 0 };
		ulonglong_t offset{ 0 };
		ulonglong_t bufferId{ 0 };
		const void_t* address{ nullptr };
		int_t addressLength{ 0 };
		ulonglong_t auxHandle{ 0 };
		const BufferChain* chain{ nullptr };
		void_t* fromAddress{ nullptr };
		int_t* fromAddressLength{ nullptr };
		bool_t isRegisteredIo{ false };
	};

	/**
	 * @class Completion
	 * @brief 엔진이 돌려주는 단일 I/O 완료 결과를 나타내는 값 타입.
	 *
	 * result 가 0 이상이면 성공(전송된 바이트 수), 음수이면 실패(-(OS 에러코드))를 뜻하며
	 * 상위 계층에서 IoError 로 변환한다.
	 */
	struct Completion
	{
		void_t* userData{ nullptr };
		longlong_t result{ 0 };
	};

END_NS
