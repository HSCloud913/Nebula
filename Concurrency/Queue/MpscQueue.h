//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include "Base/Type.h"
#include "Concurrency/Queue/SingleRoleGuard.h"

namespace ne::concurrency
{
	/**
	 * @class MpscQueue
	 * @brief Michael-Scott 큐 기반의 다중 생산자·단일 소비자(MPSC) lock-free 큐입니다.
	 *
	 * @tparam T 큐에 저장할 값 타입.
	 * @note Enqueue()는 여러 스레드에서 동시 호출해도 안전하지만, Dequeue()/IsEmpty()는 단일
	 * 소비자(동시에 한 스레드)에서만 호출해야 합니다. 디버그 빌드에서는 이 제약 위반(두 스레드가
	 * 동시에 소비)을 assert 로 감지합니다(릴리스에서는 검사가 사라져 오버헤드 0).
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
			while (DequeueImpl(ignored)) {}
			delete head.load(std::memory_order_relaxed);
		}

		NEBULA_NON_COPYABLE_MOVABLE(MpscQueue)

	private:
		alignas(64) std::atomic<Node*> head;
		alignas(64) std::atomic<Node*> tail;
		mutable SingleRoleGuard consumerGuard; // Dequeue/IsEmpty 단일 소비자 검사(디버그 전용)

	public:
		void_t Enqueue(T _value) // 다중 생산자 안전 — 역할 제약 없음
		{
			Node* node = new Node(std::move(_value));
			Node* previousNode = tail.exchange(node, std::memory_order_acq_rel);
			previousNode->next.store(node, std::memory_order_release);
		}

		[[nodiscard]] bool_t Dequeue(T& _out) noexcept
		{
			[[maybe_unused]] const auto guard = consumerGuard.Enter("MpscQueue::Dequeue must be called from a single consumer thread");
			return DequeueImpl(_out);
		}

		[[nodiscard]] bool_t IsEmpty() const noexcept
		{
			[[maybe_unused]] const auto guard = consumerGuard.Enter("MpscQueue::IsEmpty must be called from a single consumer thread");
			Node* headNode = head.load(std::memory_order_relaxed);
			return headNode->next.load(std::memory_order_acquire) == nullptr;
		}

	private:
		[[nodiscard]] bool_t DequeueImpl(T& _out) noexcept
		{
			Node* headNode = head.load(std::memory_order_relaxed);
			Node* nextNode = headNode->next.load(std::memory_order_acquire);
			if (!nextNode) return false;

			_out = std::move(nextNode->value);
			head.store(nextNode, std::memory_order_relaxed);
			delete headNode;

			return true;
		}
	};
}
