//
// Created by hscloud on 26. 7. 10.
//

#pragma once
#include <span>
#include "Base/Type.h"
#include "Io/IoResult.h"
#include "Io/Buffer/BufferView.h"
#include "Io/Engine/IEngine.h"
#include "Io/Engine/IRegisteredBufferProvider.h"

BEGIN_NS(ne::io)
	/**
	 * @class RegisteredBuffer
	 * @brief 엔진에 사전 등록된 zero-copy 메모리 영역을 관리하는 RAII 래퍼.
	 *
	 * RIO(Windows)/io_uring Fixed Buffer(Linux)처럼 사전 등록이 필요한 엔진에서
	 * SendZeroCopy/ReadFixed/WriteFixed 의 fast path 를 쓰기 위한 공개 표면이다.
	 * 엔진이 등록 버퍼를 지원하지 않으면 Register() 가 IoErrorKind::UNSUPPORTED 로 실패하며,
	 * 이동만 가능하고 복사할 수 없다. 소멸 시 등록을 자동으로 해제한다.
	 *
	 * @note 등록에 사용한 메모리 영역은 이 객체와 이로부터 만든 모든 BufferView 가 쓰이는 동안,
	 * 그리고 소멸(등록 해제)되기 전까지 주소가 바뀌면 안 된다 — 호출자가 보장해야 한다.
	 */
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
		[[nodiscard]] static IoResult<RegisteredBuffer> Register(IEngine& _engine, const std::span<ne::byte_t> _region) noexcept
		{
			auto* provider = _engine.AsRegisteredBufferProvider();
			if (provider == nullptr) return IoResult<RegisteredBuffer>::Error(IoError{ IoErrorKind::UNSUPPORTED, "engine has no registered buffer provider" });

			auto result = provider->RegisterBuffer(_region);
			if (result.IsError()) return IoResult<RegisteredBuffer>::Error(std::move(result.Error()));

			return IoResult<RegisteredBuffer>::Ok(RegisteredBuffer{ *provider, result.Value(), _region });
		}

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
