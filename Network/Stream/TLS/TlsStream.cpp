//
// Created by hscloud on 25. 6. 29.
//
// TlsStream 의 **백엔드 무관** 구현입니다. 플랫폼별 핸드셰이크/암복호화는
// Internal/Schannel/SchannelStream.cpp(Windows) 와 Internal/OpenSsl/OpenSslStream.cpp(POSIX) 가
// 각각 담당하며, CMake 가 둘 중 하나만 컴파일합니다.

#include "Network/Stream/Tls/TlsStream.h"

#include <cstddef>
#include <utility>



namespace ne::network
{
	// ─── Sendv/Receivev — 백엔드 무관 공통 구현. TLS 레코드는 세그먼트별로 암복호화해야 하므로
	// (BufferChain 을 하나로 펼치는 경로가 없음) 세그먼트 순서대로 Send()/Receive() 를 반복 호출한다. ───
	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		std::size_t total = 0;
		for (const auto& segment : _chain.Segments())
		{
			auto result = co_await Send(segment, _stopToken);
			if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[TlsStream/Sendv]"));

			total += result.Value();
			if (result.Value() < segment.length) break; // 상대 종료 등으로 짧게 끝남 — 더 진행하지 않음
		}

		co_return R::Ok(total);
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		std::size_t total = 0;
		for (const auto& segment : _chain.Segments())
		{
			auto result = co_await Receive(segment, _stopToken);
			if (result.IsError()) co_return R::Error(std::move(result.Error()).Context("[TlsStream/Receivev]"));

			total += result.Value();
			if (result.Value() < segment.length) break; // EOF 또는 짧은 읽기 — 세그먼트 경계에서 멈춤
		}

		co_return R::Ok(total);
	}
}
