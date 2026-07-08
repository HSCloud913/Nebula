//
// Created by hscloud on 26. 7. 8.
//
// Level 3.5 — Zero-Copy 리소스. 섹터 정렬된 슬랩을 사전 할당하고 엔진에 일괄 등록한다
// (io_uring_register_buffers / RIORegisterBuffer). 등록에 성공하면 *_fixed/zero-copy fast path
// 를, 실패(미지원 엔진)하면 일반 경로 폴백을 위한 정렬 버퍼로 그대로 쓸 수 있다.

#pragma once
#include <cstddef>
#include <span>
#include <vector>
#include "Type.h"
#include "IoResult.h"

BEGIN_NS(ne::io)
	class BufferPool;

	// 풀에서 대여한 등록 버퍼. 소멸 시 자동으로 풀에 반환된다(move-only 값).
	class RegisteredBuffer
	{
	public:
		NEBULA_NON_COPYABLE(RegisteredBuffer)

		RegisteredBuffer() noexcept = default; // 무효(빈) 상태
		RegisteredBuffer(RegisteredBuffer&& _other) noexcept;
		RegisteredBuffer& operator=(RegisteredBuffer&& _other) noexcept;
		~RegisteredBuffer();

	private:
		friend class BufferPool;
		RegisteredBuffer(BufferPool* _pool, std::span<ne::byte_t> _view, uint_t _bufferId, bool_t _registered) noexcept
			: pool(_pool), bufferView(_view), bufferId(_bufferId), registered(_registered) {}

	private:
		BufferPool*           pool{ static_cast<BufferPool*>(nullptr) };
		std::span<ne::byte_t> bufferView;
		uint_t                bufferId{ 0 };
		bool_t                registered{ false };

	public:
		[[nodiscard]] std::span<ne::byte_t> View() const noexcept { return bufferView; }
		[[nodiscard]] uint_t Id() const noexcept { return bufferId; }        // 엔진 등록 인덱스(*_fixed 용)
		[[nodiscard]] bool_t IsRegistered() const noexcept { return registered; } // fast path 가능 여부
		[[nodiscard]] bool_t IsValid() const noexcept { return pool != nullptr; }
	};

	// IIoEngine 은 완전형이 필요 없음(포인터만). BufferPool.cpp 에서 include.
	class IIoEngine;

	class BufferPool
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(BufferPool) // 등록 버퍼가 back-pointer 로 참조 — 주소 고정(비이동)

		// _count 개의 (_bufferSize 를 _alignment 로 올림한) 정렬 버퍼를 한 슬랩에 할당하고 엔진에 등록 시도.
		BufferPool(IIoEngine& _engine, std::size_t _count, std::size_t _bufferSize, std::size_t _alignment = 4096) noexcept;
		~BufferPool();

	private:
		IIoEngine*          engine;
		ne::byte_t*         slab;         // 정렬된 단일 할당(count * stride)
		std::size_t         stride;       // 버퍼 1개 크기(정렬 올림)
		std::size_t         count;
		std::size_t         alignment;
		bool_t              registered;   // 엔진 등록 성공(fast path 가능)
		std::vector<uint_t> freeList;     // 사용 가능한 버퍼 인덱스

	public:
		[[nodiscard]] RegisteredBuffer Acquire() noexcept; // free list 에서 하나(없으면 무효 버퍼)

	private:
		friend class RegisteredBuffer;
		void_t ReturnBuffer(uint_t _bufferId) noexcept;    // RegisteredBuffer 소멸자가 호출

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return slab != nullptr; }       // 슬랩 할당 성공
		[[nodiscard]] bool_t IsRegistered() const noexcept { return registered; }        // 엔진 등록됨
		[[nodiscard]] std::size_t Available() const noexcept { return freeList.size(); }
	};

END_NS
