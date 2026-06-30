//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <cstddef>
#include "Type.h"

BEGIN_NS(ne::concurrency)
	// Michael-Scott 큐 기반 MPSC lock-free 큐.
	// Push: 다중 스레드 안전. TryPop: 단일 소비자 전용.
	template <typename T>
	class MpscQueue
	{
	private:
		struct Node
		{
			std::atomic<Node*> next{ nullptr };
			T value{};

			Node() = default;
			explicit Node(T _val) : value(std::move(_val)) {}
		};

	public:
		MpscQueue()
		{
			Node* dummy = new Node{};
			head.store(dummy, std::memory_order_relaxed);
			tail.store(dummy, std::memory_order_relaxed);
		}

		~MpscQueue()
		{
			T ignored{};
			while (Dequeue(ignored)) {}
			delete head.load(std::memory_order_relaxed);
		}

		NEBULA_NON_COPYABLE_MOVABLE(MpscQueue)

	private:
		alignas(64) std::atomic<Node*> head;
		alignas(64) std::atomic<Node*> tail;

	public:
		void Enqueue(T _value)
		{
			Node* node = new Node(std::move(_value));
			Node* prev = tail.exchange(node, std::memory_order_acq_rel);
			prev->next.store(node, std::memory_order_release);
		}

		[[nodiscard]] bool_t Dequeue(T& _out) noexcept
		{
			Node* h = head.load(std::memory_order_relaxed);
			Node* next = h->next.load(std::memory_order_acquire);
			if (!next) return false;

			_out = std::move(next->value);
			head.store(next, std::memory_order_relaxed);
			delete h;
			return true;
		}

		[[nodiscard]] bool_t IsEmpty() const noexcept
		{
			Node* h = head.load(std::memory_order_relaxed);
			return h->next.load(std::memory_order_acquire) == nullptr;
		}
	};
END_NS
