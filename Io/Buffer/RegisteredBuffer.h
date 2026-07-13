//
// Created by hscloud on 26. 7. 10.
//
// Level 3.5 — 등록 버퍼(zero-copy) RAII. RIO(Windows)/io_uring Fixed Buffer(Linux) 처럼 사전
// 등록이 필요한 엔진에서 SendZeroCopy/ReadFixed/WriteFixed 의 fast path 를 쓰기 위한 공개 표면.
// 엔진이 등록 버퍼를 지원하지 않으면(EpollEngine/WsaPollEngine) Register() 가 IoErrorKind::UNSUPPORTED
// 로 실패한다 — 호출자는 그 경우 일반 Send/Receive(비등록 경로)로 폴백해야 한다.

#pragma once
#include <span>
#include "Base/Type.h"
#include "Io/IoResult.h"
#include "Io/Buffer/BufferView.h"
#include "Io/Engine/IEngine.h"
#include "Io/Engine/IRegisteredBufferProvider.h"

BEGIN_NS(ne::io)
	class RegisteredBuffer
	{
	private:
		RegisteredBuffer(IRegisteredBufferProvider& _provider, const BufferHandle _handle, const std::span<ne::byte_t> _region) noexcept
			: provider(&_provider)
			, handle(_handle)
			, region(_region) {}

	public:
		RegisteredBuffer() = default;
		~RegisteredBuffer() { Reset(); }

		RegisteredBuffer(RegisteredBuffer&& _other) noexcept
			: provider(_other.provider)
			, handle(_other.handle)
			, region(_other.region)
		{
			_other.provider = nullptr;
			_other.handle = BufferHandle{};
			_other.region = {};
		}

		RegisteredBuffer& operator=(RegisteredBuffer&& _other) noexcept
		{
			if (this != &_other)
			{
				Reset();
				provider = _other.provider;
				handle = _other.handle;
				region = _other.region;
				_other.provider = nullptr;
				_other.handle = BufferHandle{};
				_other.region = {};
			}

			return *this;
		}

		NEBULA_NON_COPYABLE(RegisteredBuffer)

	private:
		IRegisteredBufferProvider* provider{ nullptr };
		BufferHandle handle{};
		std::span<ne::byte_t> region;

	public:
		// _region 을 엔진에 등록한다. 엔진이 등록 버퍼 provider 가 없으면(AsRegisteredBufferProvider()
		// == nullptr) UNSUPPORTED. 수명 불변식(IRegisteredBufferProvider 계약과 동일): _region 은 이
		// RegisteredBuffer 및 이걸로 만든 모든 BufferView 가 쓰이는 동안, 그리고 소멸(등록 해제)되기
		// 전까지 주소가 바뀌면 안 된다 — 호출자가 보장한다.
		[[nodiscard]] static IoResult<RegisteredBuffer> Register(IEngine& _engine, const std::span<ne::byte_t> _region) noexcept
		{
			auto* provider = _engine.AsRegisteredBufferProvider();
			if (provider == nullptr) return IoResult<RegisteredBuffer>::Error(IoError{ IoErrorKind::UNSUPPORTED, "engine has no registered buffer provider" });

			auto result = provider->RegisterBuffer(_region);
			if (result.IsError()) return IoResult<RegisteredBuffer>::Error(std::move(result.Error()));

			return IoResult<RegisteredBuffer>::Ok(RegisteredBuffer{ *provider, result.Value(), _region });
		}

		// 등록된 영역 내부의 부분 view. _length == 0 이면 _offset 부터 끝까지. [_offset, _offset+_length)
		// 는 region 범위를 벗어나면 안 된다(BufferView::Slice 와 동일 계약).
		[[nodiscard]] BufferView View(const std::size_t _offset = 0, const std::size_t _length = 0) const noexcept
		{
			const std::size_t length = (_length == 0) ? (region.size() - _offset) : _length;
			return BufferView{ region.data() + _offset, length };
		}

	private:
		void_t Reset() noexcept
		{
			if (provider != nullptr && handle.IsValid()) provider->UnregisterBuffer(handle);
			provider = nullptr;
			handle = BufferHandle{};
			region = {};
		}

	public:
		[[nodiscard]] BufferHandle Handle() const noexcept { return handle; }
		[[nodiscard]] std::span<ne::byte_t> Region() const noexcept { return region; }
		[[nodiscard]] bool_t IsValid() const noexcept { return handle.IsValid(); }
	};

END_NS
