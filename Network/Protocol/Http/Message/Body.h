//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <functional>
#include <string_view>
#include <variant>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Memory/Buffer/BufferChain.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"

namespace ne::network::http
{
	/**
	 * @brief 스트리밍 본문 생산자 — 호출할 때마다 다음 조각을 만드는 Task 를 돌려줍니다.
	 *
	 * Ok(비어 있지 않은 벡터)=다음 조각, Ok(빈 벡터)=본문 끝(EOF), Error=생산 실패(전송 중단).
	 * 상태(진행 위치 등)는 생산자 클로저가 소유합니다. 전송 계층이 조각을 순차로 당겨 쓰므로
	 * (HTTP/1.1 은 chunked, HTTP/2 는 DATA 프레임) 본문 전체를 메모리에 올리지 않습니다.
	 */
	using BodyProducer = std::function<ne::Task<HttpResult<std::vector<byte_t>>>()>;

	/**
	 * @class Body
	 * @brief HTTP 메시지 본문을 나타내는 값 타입입니다. 메모리를 직접 소유하거나, 외부 버퍼를 참조하거나, 스트리밍 생산자를 담을 수 있습니다.
	 *
	 * ne::memory::BufferChain/BufferView 는 비소유 타입이라 본문 바이트 자체를 담을 수 없습니다. 이
	 * 클래스는 그래서 세 가지 생성 방법을 제공합니다: std::vector<byte_t> 로 만들면 Body 가
	 * 데이터를 직접 소유하고(대부분의 경우), BufferChain 으로 만들면 호출자가 관리하는 외부 메모리를
	 * zero-copy 로 참조만 하며(전송 완료까지 호출자가 수명을 보장해야 함), BodyProducer 로 만들면
	 * 전송 시점에 조각 단위로 당겨 보내는 스트리밍 본문이 됩니다(크기 미상 — 서버 응답 전용).
	 *
	 * 전송 코드는 스트리밍 여부(IsStreaming())만 분기하면 되고, 바이트 본문은 항상 View() 로 얻은
	 * BufferChain 하나만 다루면 됩니다.
	 */
	class Body
	{
	public:
		Body() = default;
		explicit Body(std::vector<byte_t> _owned) noexcept
			: storage(std::move(_owned)) {}
		explicit Body(ne::memory::BufferChain _view) noexcept
			: storage(std::move(_view)) {}
		explicit Body(BodyProducer _producer) noexcept
			: storage(std::move(_producer)) {}

		NEBULA_DEFAULT_COPY_MOVE(Body)

	private:
		std::variant<std::monostate, std::vector<byte_t>, ne::memory::BufferChain, BodyProducer> storage;

	public:
		// 텍스트/JSON 등 문자열 본문을 만드는 편의 팩토리. 내부적으로 std::vector<byte_t> 로 복사해 소유한다.
		[[nodiscard]] static Body FromString(const string_view_t _text) { return Body(std::vector<byte_t>(_text.begin(), _text.end())); }

		// 스트리밍 본문 팩토리 — 전송 시점에 _producer 를 반복 호출해 조각 단위로 보낸다(BodyProducer 참조).
		[[nodiscard]] static Body FromProducer(BodyProducer _producer) { return Body(std::move(_producer)); }

	public:
		[[nodiscard]] ne::memory::BufferChain View() const noexcept
		{
			if (const auto* view = std::get_if<ne::memory::BufferChain>(&storage)) return *view;

			ne::memory::BufferChain chain;
			if (const auto* owned = std::get_if<std::vector<byte_t>>(&storage); owned && !owned->empty()) chain.Append({ const_cast<byte_t*>(owned->data()), owned->size() }); // Send 경로 전용 — BufferView 는 방향을 구분하지 않는 기존 관례를 따름

			return chain;
		}

		[[nodiscard]] std::size_t Size() const noexcept
		{
			if (const auto* owned = std::get_if<std::vector<byte_t>>(&storage)) return owned->size();
			if (const auto* view = std::get_if<ne::memory::BufferChain>(&storage)) return view->TotalSize();

			return 0; // 스트리밍 본문은 크기 미상 — 0 으로 취급(IsEmpty 는 별도 분기)
		}

		[[nodiscard]] bool_t IsStreaming() const noexcept { return std::holds_alternative<BodyProducer>(storage); }

		/** @brief 스트리밍 본문이면 생산자를, 아니면 nullptr 를 반환합니다. */
		[[nodiscard]] const BodyProducer* Producer() const noexcept { return std::get_if<BodyProducer>(&storage); }

		[[nodiscard]] bool_t IsEmpty() const noexcept { return !IsStreaming() && Size() == 0; }
	};
}
