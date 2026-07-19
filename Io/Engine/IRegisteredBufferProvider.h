//
// Created by hscloud on 26. 7. 7.
//

#pragma once
#include <cstdint>
#include <span>
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Io/IoError.h"

BEGIN_NS(ne::io)
	struct BufferHandle
	{
		uint64_t value{ 0 };

		/** @brief 등록된 버퍼를 가리키는 유효한 핸들인지 확인한다(0이면 미등록/무효). */
		[[nodiscard]] bool_t IsValid() const noexcept { return value != 0; }
	};

	/**
	 * @class IRegisteredBufferProvider
	 * @brief 사전 등록(zero-copy) 버퍼를 다루는 엔진 부가 인터페이스.
	 *
	 * RIO(IocpEngine)/io_uring Fixed Buffer(IoUringEngine)처럼 커널/드라이버에 버퍼 영역을 미리
	 * 등록해야 하는 엔진만 구현하며, IEngine::AsRegisteredBufferProvider() 로 접근한다(미지원
	 * 엔진은 nullptr). RegisterBuffer 로 등록한 영역 내부의 포인터만 SubmitSendRegistered/
	 * SubmitReceiveRegistered 에 넘길 수 있고, 그 영역은 완료 및 UnregisterBuffer 전까지 주소가
	 * 바뀌면 안 된다.
	 */
	class IRegisteredBufferProvider
	{
	public:
		IRegisteredBufferProvider() = default;
		virtual ~IRegisteredBufferProvider() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IRegisteredBufferProvider)

	public:
		/**
		 * @brief 사용자 메모리 영역을 커널/드라이버에 사전 등록해 zero-copy 송수신에 쓸 수 있게 한다.
		 *
		 * RIO 는 RIORegisterBuffer, io_uring 은 io_uring_register_buffers_update_tag 로 매핑되며,
		 * 둘 다 등록 시 커널 쪽에 페이지 고정/매핑 비용이 들기 때문에 요청마다가 아니라 버퍼 풀
		 * 단위로 한 번만 호출하는 것을 전제로 한다. 반환된 BufferHandle 이 유효한 동안에는
		 * _region 의 주소가 바뀌거나 해제되면 안 된다(진행 중인 I/O 가 그 주소를 직접 참조한다).
		 * @param _region 등록할 메모리 구간(비어 있으면 실패).
		 * @return 성공 시 이후 Submit*Registered 호출에 사용할 BufferHandle, 실패 시 IoError.
		 */
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept = 0;
		/**
		 * @brief RegisterBuffer() 로 등록했던 영역의 등록을 해제한다.
		 * @param _handle 해제할 버퍼 핸들. 진행 중인 I/O 가 아직 그 영역을 참조하고 있다면 먼저
		 * 완료를 기다린 뒤 호출해야 한다(그렇지 않으면 커널이 이미 해제된 등록을 참조할 수 있음).
		 */
		virtual void_t UnregisterBuffer(BufferHandle _handle) noexcept = 0;

	public:
		/**
		 * @brief 등록된 버퍼 내부의 포인터로 zero-copy 송신을 제출한다.
		 * @param _socket 대상 소켓.
		 * @param _handle RegisterBuffer() 로 얻은, _buffer 가 속한 영역의 핸들.
		 * @param _buffer 실제 전송할 데이터 시작 주소(반드시 _handle 로 등록된 영역 내부).
		 * @param _length 전송 길이.
		 * @param _userData 완료 통지 시 식별에 쓸 사용자 데이터(IEngine::Completion::userData 로 되돌아옴).
		 * @return 제출 자체의 성공/실패. 실제 전송 결과는 IEngine::WaitCompletions() 로 비동기 통지된다.
		 */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitSendRegistered(socket_t _socket, BufferHandle _handle, const void_t* _buffer, std::size_t _length, void_t* _userData) noexcept = 0;
		/**
		 * @brief 등록된 버퍼 내부의 포인터로 zero-copy 수신을 제출한다.
		 * @param _socket 대상 소켓.
		 * @param _handle RegisterBuffer() 로 얻은, _buffer 가 속한 영역의 핸들.
		 * @param _buffer 수신 데이터를 받을 주소(반드시 _handle 로 등록된 영역 내부).
		 * @param _length 수신 가능한 최대 길이.
		 * @param _userData 완료 통지 시 식별에 쓸 사용자 데이터.
		 * @return 제출 자체의 성공/실패. 실제 수신 결과는 IEngine::WaitCompletions() 로 비동기 통지된다.
		 */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitReceiveRegistered(socket_t _socket, BufferHandle _handle, void_t* _buffer, std::size_t _length, void_t* _userData) noexcept = 0;

	public:
		/**
		 * @brief 소켓과 연관된 provider 내부 상태(예: RIO Request Queue)를 정리한다.
		 *
		 * 소켓이 닫히기 전에 호출해 provider 가 더 이상 그 소켓에 대한 리소스를 참조하지
		 * 않도록 한다. 기본 구현은 아무 것도 하지 않으므로, 소켓별 상태를 갖지 않는
		 * provider(io_uring Fixed Buffer 등)는 재정의할 필요가 없다.
		 * @param _socket 해제할 소켓.
		 */
		virtual void_t ReleaseSocket(socket_t _socket) noexcept {}
	};

END_NS
