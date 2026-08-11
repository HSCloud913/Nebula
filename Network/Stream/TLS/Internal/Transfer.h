//
// Created by hscloud on 26. 8. 11.
//

#pragma once
#include <cstddef>
#include <stop_token>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Io/Diagnostic/Error.h"
#include "Memory/Buffer/BufferView.h"
#include "Network/Stream/PlainStream.h"

namespace ne::network::internal
{
	// Schannel/OpenSSL 은 둘 다 "레코드 바이트를 내놓고/받아달라"는 스타일이라, TLS 백엔드가 전송 계층에
	// 요구하는 것은 바이트 스트림 전송뿐이다. 그 두 연산을 여기 모아 백엔드가 소켓을 직접 만지지 않게 한다.
	//
	// @note 과거에는 readiness 대기(WaitWritable/WaitReadable) + 원시 ::send/::recv 로 구현했다. 그 방식은
	//       소켓이 블로킹이어야 성립하는데, 블로킹 소켓은 리액터 엔진의 이벤트 루프를 정지시킨다. 반대로
	//       소켓을 논블로킹으로 두면 ::send 가 EWOULDBLOCK 을 돌려주고, IOCP 의 WAIT_WRITABLE 은 즉시
	//       성공을 반환하므로 바쁜 대기로 폭주한다. 완료 기반 PlainStream::Send/Receive 로 바꾸면 두 문제가
	//       동시에 사라지고, 프로액터/리액터 어느 엔진에서도 같은 코드로 동작한다.
	inline ne::Task<ne::io::IoResult<ne::void_t>> SendAll(ne::network::PlainStream& _transport, const ne::byte_t* _data, const std::size_t _length, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<ne::void_t>;

		std::size_t sent = 0;
		while (sent < _length)
		{
			// Send 는 부분 전송을 허용하므로 요청 길이를 다 보낼 때까지 반복한다.
			auto result = co_await _transport.Send(ne::memory::BufferView{ const_cast<ne::byte_t*>(_data) + sent, _length - sent }, _stopToken);
			if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[TlsStream/SendAll]"));

			// 0 바이트 전송은 진전이 없다는 뜻 — 무한 루프를 막기 위해 연결 종료로 취급한다.
			if (result.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::CONNECTION_CLOSED, "peer closed while sending TLS record" }.Context("[TlsStream/SendAll]"));

			sent += result.Value();
		}

		co_return R::Ok();
	}

	/** @brief 한 번 수신해 받은 바이트 수를 돌려준다. 0 은 상대가 send 방향을 닫았다는 뜻(EOF)이다. */
	inline ne::Task<ne::io::IoResult<std::size_t>> RecvSome(ne::network::PlainStream& _transport, ne::byte_t* _data, const std::size_t _capacity, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		auto result = co_await _transport.Receive(ne::memory::BufferView{ _data, _capacity }, _stopToken);
		if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[TlsStream/RecvSome]"));

		co_return R::Ok(result.Value());
	}
}
