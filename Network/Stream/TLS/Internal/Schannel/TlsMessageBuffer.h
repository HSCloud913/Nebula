//
// Created by hscloud on 25. 6. 30.
//

#pragma once

#ifdef _WIN32
#	include <cassert>
#	include <cstring>
#	include <span>
#	include <vector>
#	include "Base/Type.h"

namespace ne::network::internal
{
	/**
	 * @class TlsMessageBuffer
	 * @brief Schannel 핸드셰이크/레코드 처리용 가변 크기 바이트 버퍼입니다.
	 *
	 * data는 buffer 중 아직 복호화되지 않은(다음 처리 대상) **암호문** 구간을 가리키는 span이며, 버퍼가
	 * 부족해지면 Resize()가 data가 가리키던 상대적 위치를 유지한 채 buffer를 늘립니다.
	 *
	 * plaintext는 그와 별개로, 한 레코드를 복호화했지만 호출자 버퍼가 작아 아직 전달하지 못한 **평문**을
	 * 보관합니다. TLS 레코드 평문은 최대 16KiB인데 호출자는 보통 1~4KiB 버퍼를 넘기므로, 이 잔여분을
	 * 보관하지 않으면 남은 평문이 소실되어 스트림이 조용히 손상됩니다(과거 그 상태였습니다).
	 */
	class TlsMessageBuffer final
	{
	public:
		TlsMessageBuffer() = default;
		~TlsMessageBuffer() = default;

		NEBULA_DEFAULT_MOVE(TlsMessageBuffer)
		NEBULA_NON_COPYABLE(TlsMessageBuffer)

	private:
		explicit TlsMessageBuffer(const std::size_t _size)
			: buffer(_size) {}

	public:
		std::span<ne::byte_t> data{};

	private:
		std::vector<ne::byte_t> buffer;
		std::vector<ne::byte_t> plaintext; // 아직 호출자에게 넘기지 못한 복호화 결과
		std::size_t plaintextOffset{ 0 };  // plaintext 중 이미 넘긴 바이트 수

	public:
		/** @brief buffer 를 최소 _size 바이트로 늘립니다(_size 는 현재 크기 이상이어야 함). data 가 가리키던 상대 위치는 유지됩니다. */
		void_t Resize(std::size_t _size);

	public:
		[[nodiscard]] bool_t HasPlaintext() const noexcept { return plaintextOffset < plaintext.size(); }

		/** @brief 복호화된 평문 전체를 보관해 이후 TakePlaintext() 로 조금씩 꺼내 쓰게 합니다. */
		void_t StorePlaintext(const ne::byte_t* _data, const std::size_t _length)
		{
			plaintext.assign(_data, _data + _length);
			plaintextOffset = 0;
		}

		/**
		 * @brief 보관된 평문에서 최대 _capacity 바이트를 _out 으로 옮기고 옮긴 길이를 돌려줍니다.
		 * @note 전부 소비되면 보관 버퍼를 비워 다음 레코드를 받을 준비를 합니다.
		 */
		[[nodiscard]] std::size_t TakePlaintext(ne::byte_t* _out, const std::size_t _capacity)
		{
			const std::size_t remaining = plaintext.size() - plaintextOffset;
			const std::size_t taken = remaining < _capacity ? remaining : _capacity;

			std::memcpy(_out, plaintext.data() + plaintextOffset, taken);
			plaintextOffset += taken;

			if (plaintextOffset >= plaintext.size())
			{
				plaintext.clear();
				plaintextOffset = 0;
			}

			return taken;
		}

		[[nodiscard]] std::span<ne::byte_t> GetBuffer() { return buffer; }
		[[nodiscard]] std::vector<ne::byte_t>::iterator Begin() { return buffer.begin(); }
		[[nodiscard]] std::vector<ne::byte_t>::iterator End() { return buffer.end(); }
		/** @brief 16KiB(TLS 레코드 최대 크기) 초기 용량으로 버퍼를 생성합니다. */
		[[nodiscard]] static TlsMessageBuffer Allocate() { return TlsMessageBuffer(1 << 14); }
	};
}

#endif // _WIN32
