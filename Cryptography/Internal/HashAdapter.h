//
// Created by hscloud on 26. 8. 3.
//

#pragma once
#include <cstdio>
#include "Base/Type.h"

namespace ne::crypto::internal
{
	/**
	 * @class HashWrapper
	 * @brief 해시 계산의 공통 인터페이스입니다(HashFactory 반환 타입).
	 *
	 * 원샷(FromString/FromFile)과 증분(Init/Update/Final) 두 방식을 제공합니다. 증분 경로는 큰
	 * 메시지를 전체 버퍼링 없이 처리해야 하는 곳(파일 해시, 스트리밍 HMAC)이 씁니다.
	 */
	class HashWrapper
	{
	public:
		HashWrapper() = default;
		virtual ~HashWrapper() = default;

	public:
		/** @brief 증분 계산을 시작(또는 재시작)합니다. */
		virtual void_t Init() = 0;

		/** @brief 메시지 조각을 이어 넣습니다. Init() 이후 여러 번 호출할 수 있습니다. */
		virtual void_t Update(string_view_t _chunk) = 0;

		/** @brief 누적된 메시지의 해시를 소문자 hex 로 확정합니다. */
		[[nodiscard]] virtual string_t Final() = 0;

	public:
		/** @brief 문자열 전체를 한 번에 해시합니다. */
		[[nodiscard]] string_t FromString(const string_view_t _text)
		{
			Init();
			Update(_text);

			return Final();
		}

		/** @brief 파일 전체를 조각째 읽어 해시합니다. 파일을 열 수 없으면 빈 문자열을 반환합니다. */
		[[nodiscard]] string_t FromFile(const string_view_t _path)
		{
			const string_t path(_path); // fopen 은 널 종단 경로 필요

#if defined(_WIN32)
			FILE* file = nullptr;
			if (::fopen_s(&file, path.c_str(), "rb") != 0) file = nullptr;
#else
			FILE* file = std::fopen(path.c_str(), "rb");
#endif
			if (file == nullptr) return {};

			Init();

			char_t buffer[4096];
			std::size_t length = 0;
			do
			{
				length = std::fread(buffer, 1, sizeof(buffer), file);
				if (length > 0) Update(string_view_t(buffer, length));
			} while (length > 0);

			std::fclose(file);
			return Final();
		}
	};

	/**
	 * @class HashAdapter
	 * @brief Init/AddBuffer/Get 계약을 따르는 알고리즘(CRC32/MD5/SHA1/SHA2/SHA3)을 HashWrapper 로 감싸는 어댑터입니다.
	 *
	 * 알고리즘을 값으로 소유하므로 팩토리 생성 시 힙 할당이 어댑터 1회로 끝납니다(과거
	 * 알고리즘별 Wrapper 5종이 각자 알고리즘을 다시 힙에 들던 구조를 대체, 2026-08-03).
	 */
	template <typename TAlgorithm>
	class HashAdapter final : public HashWrapper
	{
	public:
		template <typename... TArgs>
		explicit HashAdapter(TArgs... _args)
			: algorithm(_args...) {}

	private:
		TAlgorithm algorithm;

	public:
		void_t Init() override { algorithm.Init(); }

		void_t Update(const string_view_t _chunk) override { algorithm.AddBuffer(_chunk.data(), _chunk.size()); }

		[[nodiscard]] string_t Final() override { return algorithm.Get(); }
	};
}
