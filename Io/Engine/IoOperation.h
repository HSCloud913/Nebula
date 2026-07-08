//
// Created by hscloud on 26. 7. 8.
//
// Level 0 값 타입 — 엔진 제출 요청/완료 결과/기능 질의. 스펙 2.2~2.3 기준.
// 모든 타입은 값 기반(설계 규칙 4)이며 어떤 엔진에도 종속되지 않는다.

#pragma once
#include "Type.h"
#include "IoType.h"

BEGIN_NS(ne::io)
	// 엔진이 제공하는 zero-copy / 오버헤드 감소 세부 기능. 엔진 서브클래스가 아니라 런타임
	// IIoEngine::Supports(Capability) 로만 질의한다(스펙 2.2). 기본 Read/Write 는 모든 엔진이 항상 지원.
	//   RecvOverheadReduced vs RecvTrueZeroCopy 를 명확히 구분한다 — 전자는 buffer pin/validate
	//   오버헤드 제거(복사는 여전히 있음), 후자만 진짜 recv zero-copy(mmap 기반).
	enum class Capability : uint_t
	{
		SendFileZeroCopy,    // 파일→소켓 진짜 zero-copy (TransmitFile / sendfile / splice / SEND_ZC)
		SendMemZeroCopy,     // 메모리→소켓 진짜 zero-copy (RIO / MSG_ZEROCOPY / SEND_ZC)
		RecvOverheadReduced, // registered buffer — 복사는 있으나 pin/validate 오버헤드 감소 (RIO / io_uring Fixed Buffer)
		RecvTrueZeroCopy,    // 진짜 recv zero-copy (mmap 기반 — TCP_ZEROCOPY_RECEIVE)
	};

	// 제출할 I/O 연산 종류. 규칙 2에 따라 Recv 대신 Receive 로 명명한다.
	enum class OpCode : uint_t
	{
		Read,         // 파일 read (offset 사용)
		Write,        // 파일 write (offset 사용)
		Receive,      // 소켓 수신
		Send,         // 소켓 송신
		Accept,       // 소켓 accept
		Connect,      // 소켓 connect
		ReadFixed,    // 등록 버퍼 read  (Level 3.5)
		WriteFixed,   // 등록 버퍼 write (Level 3.5)
		SendZeroCopy, // 메모리→소켓 zero-copy send (Level 3.5)
		SendFile,     // 파일→소켓 zero-copy 전송  (Level 3.5)
	};

	// 엔진에 제출하는 단일 I/O 요청 (값 기반).
	// 완료 시 userData 를 그대로 IoCompletion 으로 돌려주므로, 상위(Level 2)는 여기에 코루틴
	// 재개 컨텍스트 포인터를 실어 완료를 자기 코루틴으로 연결한다.
	//   handle : 소켓(socket_t)과 파일(file_t)을 함께 담기 위해 플랫폼 native 핸들을 64비트로
	//            정규화해 보관한다(Win64 SOCKET/HANDLE, POSIX fd 모두 수용). 엔진이 원 타입으로 복원.
	struct IoRequest
	{
		OpCode      op{ OpCode::Read };
		void*       userData{ static_cast<void*>(nullptr) };
		ulonglong_t handle{ 0 };
		void*       buffer{ static_cast<void*>(nullptr) };
		std::size_t length{ 0 };
		ulonglong_t offset{ 0 };
		uint_t      bufferId{ 0 };                            // 등록 버퍼 index/id (Level 3.5, 0 = 미사용)
		const void* address{ static_cast<const void*>(nullptr) }; // Connect 대상 sockaddr (Accept/Connect 전용)
		int_t       addressLength{ 0 };
	};

	// 엔진이 돌려주는 단일 완료 결과 (값 기반).
	//   result >= 0 : 성공, 전송된 바이트 수.
	//   result <  0 : 실패, -(OS 에러코드). 상위에서 IoError 로 변환한다.
	struct IoCompletion
	{
		void*      userData{ static_cast<void*>(nullptr) };
		longlong_t result{ 0 };
	};
END_NS
