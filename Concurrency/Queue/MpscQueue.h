//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <cstddef>
#include "Base/Type.h"

BEGIN_NS(ne::concurrency)
	// Michael-Scott 큐 기반 MPSC lock-free 큐.
	// Push: 다중 스레드 안전. TryPop: 단일 소비자 전용.
	template <typename T>
	class MpscQueue
	{
	private:
		struct Node
		{
			Node() = default;
			explicit Node(T _val) : value(std::move(_val)) {}

			std::atomic<Node*> next{ nullptr };
			T value{};
		};

	public:
		MpscQueue()
		{
			Node* dummyNode = new Node{};
			head.store(dummyNode, std::memory_order_relaxed);
			tail.store(dummyNode, std::memory_order_relaxed);
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
		void_t Enqueue(T _value)
		{
			Node* node = new Node(std::move(_value));
			Node* previousNode = tail.exchange(node, std::memory_order_acq_rel);
			previousNode->next.store(node, std::memory_order_release);
		}

		[[nodiscard]] bool_t Dequeue(T& _out) noexcept
		{
			Node* headNode = head.load(std::memory_order_relaxed);
			Node* nextNode = headNode->next.load(std::memory_order_acquire);
			if (!nextNode) return false;

			_out = std::move(nextNode->value);
			head.store(nextNode, std::memory_order_relaxed);
			delete headNode;

			return true;
		}

		[[nodiscard]] bool_t IsEmpty() const noexcept
		{
			Node* headNode = head.load(std::memory_order_relaxed);

			return headNode->next.load(std::memory_order_acquire) == nullptr;
		}
	};
END_NS
