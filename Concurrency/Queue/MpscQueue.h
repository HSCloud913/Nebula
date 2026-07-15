//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include "Base/Type.h"

BEGIN_NS(ne::concurrency)
	/**
	 * @class MpscQueue
	 * @brief Michael-Scott 큐 기반의 다중 생산자·단일 소비자(MPSC) lock-free 큐입니다.
	 *
	 * @tparam T 큐에 저장할 값 타입.
	 * @note Enqueue()는 여러 스레드에서 동시 호출해도 안전하지만, Dequeue()/IsEmpty()는
	 * 단일 소비자 스레드에서만 호출해야 합니다.
	 */
	template <typename T>
	class MpscQueue
	{
	private:
		struct Node
		{
			Node() = default;
			explicit Node(T _val)
				: value(std::move(_val)) {}

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
