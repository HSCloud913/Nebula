//
// Created by hscloud on 26. 7. 8.
//
// Level 0 값 타입 — 엔진 제출 요청/완료 결과/기능 질의. 스펙 2.2~2.3 기준.
// 모든 타입은 값 기반(설계 규칙 4)이며 어떤 엔진에도 종속되지 않는다.

#pragma once
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Io/Buffer/BufferChain.h"

BEGIN_NS(ne::io)
	// 엔진이 제공하는 zero-copy / 오버헤드 감소 세부 기능. 엔진 서브클래스가 아니라 런타임
	// IEngine::Supports(Capability) 로만 질의한다(스펙 2.2). 기본 Read/Write 는 모든 엔진이 항상 지원.
	//   RecvOverheadReduced vs RecvTrueZeroCopy 를 명확히 구분한다 — 전자는 buffer pin/validate
	//   오버헤드 제거(복사는 여전히 있음), 후자만 진짜 recv zero-copy(mmap 기반).
	enum class Capability : uint_t
	{
		SendFileZeroCopy,
		// 파일→소켓 진짜 zero-copy (TransmitFile / sendfile / splice / SEND_ZC)
		SendMemZeroCopy,
		// 메모리→소켓 진짜 zero-copy (RIO / MSG_ZEROCOPY / SEND_ZC)
		RecvOverheadReduced,
		// registered buffer — 복사는 있으나 pin/validate 오버헤드 감소 (RIO / io_uring Fixed Buffer)
		RecvTrueZeroCopy,
		// 진짜 recv zero-copy (mmap 기반 — TCP_ZEROCOPY_RECEIVE)
	};

	// 제출할 I/O 연산 종류. 규칙 2에 따라 Recv 대신 Receive 로 명명한다.
	enum class OpCode : uint_t
	{
		Read,
		// 파일 read (offset 사용)
		Write,
		// 파일 write (offset 사용)
		Receive,
		// 소켓 수신
		Send,
		// 소켓 송신
		Accept,
		// 소켓 accept
		Connect,
		// 소켓 connect
		ReadFixed,
		// 등록 버퍼 read  (Level 3.5)
		WriteFixed,
		// 등록 버퍼 write (Level 3.5)
		SendZeroCopy,
		// 메모리→소켓 zero-copy send (Level 3.5)
		SendFile,
		// 파일→소켓 zero-copy 전송  (Level 3.5)
		SendTo,
		// 비연결형(UDP 등) 송신 — address/addressLength 가 매 호출 목적지
		ReceiveFrom,
		// 비연결형(UDP 등) 수신 — fromAddress/fromAddressLength 에 발신자 주소를 채움

		// readiness 대기(데이터를 옮기지 않고 "읽기/쓰기 가능"까지만 대기) — libssh2 등 readiness(reactor)
		// 모델 라이브러리를 completion 엔진 위에서 구동하기 위한 프리미티브. reactor(Epoll/WsaPoll)와
		// io_uring(POLL_ADD)은 native, IOCP 는 WaitReadable=0-byte WSARecv 로 근사(WaitWritable 은 근사 불가라
		// 즉시 ready 로 처리 — 아래 IocpEngine 주석 참조).
		WaitReadable,
		// handle 소켓이 읽기 가능해질 때까지 대기(완료 result 0 = ready)
		WaitWritable,
		// handle 소켓이 쓰기 가능해질 때까지 대기
	};

	// 엔진에 제출하는 단일 I/O 요청 (값 기반).
	// 완료 시 userData 를 그대로 Completion 으로 돌려주므로, 상위(Level 2)는 여기에 코루틴
	// 재개 컨텍스트 포인터를 실어 완료를 자기 코루틴으로 연결한다.
	//   handle : 소켓(socket_t)과 파일(file_t)을 함께 담기 위해 플랫폼 native 핸들을 64비트로
	//            정규화해 보관한다(Win64 SOCKET/HANDLE, POSIX fd 모두 수용). 엔진이 원 타입으로 복원.
	struct Request
	{
		OpCode op{ OpCode::Read };
		void_t* userData{ nullptr };
		ulonglong_t handle{ 0 };
		void_t* buffer{ nullptr };
		std::size_t length{ 0 };
		ulonglong_t offset{ 0 };
		ulonglong_t bufferId{ 0 };        // 등록 버퍼 handle 값(BufferHandle.value) / io_uring buf_index (Level 3.5, 0 = 미사용)
		const void_t* address{ nullptr }; // Connect 대상 sockaddr (Accept/Connect 전용)
		int_t addressLength{ 0 };
		ulonglong_t auxHandle{ 0 };          // SendFile 전용 — 원본 파일(handle 은 Send 계열과 동일하게 목적지 소켓)
		const BufferChain* chain{ nullptr }; // 지정 시 buffer/length 대신 이 체인으로 scatter/gather 수행

		// ReceiveFrom 전용 — 호출자가 마련한 sockaddr_storage(fromAddress)와 그 용량/실채움길이
		// (fromAddressLength, in: 버퍼 용량 / out: 엔진이 recvfrom·WSARecvFrom 완료 후 채운 실제
		// 길이)를 넘긴다. 완료까지 두 포인터가 가리키는 메모리가 살아있어야 한다(buffer 와 동일 계약).
		void_t* fromAddress{ nullptr };
		int_t* fromAddressLength{ nullptr };

		// Accept 전용(Windows) — true 면 엔진이 accept 소켓을 WSA_FLAG_REGISTERED_IO 로 생성한다
		// (RIO/SendZeroCopy 용). POSIX 엔진은 accept 가 fd 를 그대로 돌려주므로 이 값을 무시한다.
		bool_t isRegisteredIo{ false };
	};

	// 엔진이 돌려주는 단일 완료 결과 (값 기반).
	//   result >= 0 : 성공, 전송된 바이트 수.
	//   result <  0 : 실패, -(OS 에러코드). 상위에서 IoError 로 변환한다.
	struct Completion
	{
		void_t* userData{ nullptr };
		longlong_t result{ 0 };
	};

END_NS
