//
// Created by hscloud on 26. 7. 10.
//
// Level 1 — thread-per-core 실행기 풀. 워커마다 독립된 (엔진 + Context + TimerWheel + 스레드)를
// 소유한다. 각 Context 는 단일 스레드 전제(스펙 3)이므로 코어 간 공유가 없고, 워커끼리는 자원을
// 나눠 갖지 않는다(무공유). 코루틴/소켓은 자신이 속한 Context 스레드 위에서만 구동해야 하며,
// 코어 간 이동이 필요하면 목적지 Context::Post() 로만 명시적으로 넘긴다(암묵적 마이그레이션 금지).
//
// 사용:
//   ContextPool pool;                 // 코어 수만큼 워커
//   pool.Start();                     // 각 워커 스레드에서 Context::Run() 시작
//   Context& ctx = pool.Acquire();    // 새 연결을 배정할 Context(round-robin)
//   ... ctx 위에서 Socket 생성/구동 ...
//   pool.Stop();                      // (소멸자도 호출) 모든 Context Stop + join
//
// 테스트/단일 스레드 구동: Start() 없이 Acquire()/At() 로 얻은 Context 를 직접 RunOnce() 로
// 돌릴 수도 있다(스레드를 스폰하지 않으므로 결정론적).

#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>
#include "Base/Type.h"
#include "Io/Engine/IEngine.h"

namespace ne::time
{
	class TimerWheel;
}

BEGIN_NS(ne::io)
	class Context;

	class ContextPool
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(ContextPool)

		// _size 워커 수. 0 이면 hardware_concurrency(그마저 0 이면 1)로 결정한다. 생성 시점에
		// 워커별 엔진/Context/TimerWheel 을 만들어 두며, 스레드는 Start() 에서 스폰한다.
		explicit ContextPool(std::size_t _size = 0);
		~ContextPool();

	private:
		// 하나의 실행 단위. 멤버 선언 순서 = 파괴 역순 의존성: thread 가 가장 먼저 파괴돼야 하고
		// (join 은 Stop 이 이미 수행), context 는 engine 참조를 들고 있으므로 engine 보다 먼저 파괴된다.
		struct Worker
		{
			std::unique_ptr<IEngine> engine;
			std::unique_ptr<ne::time::TimerWheel> timerWheel;
			std::unique_ptr<Context> context;
			std::thread thread;
		};

	private:
		std::vector<Worker> workers;
		std::atomic<std::size_t> cursor{ 0 }; // Acquire() round-robin 커서(여러 스레드가 분배해도 안전하게 원자적)
		bool_t isRunning{ false };            // Start/Stop 은 소유 스레드에서만 호출하는 전제(멱등)

	public:
		// 각 워커 스레드를 스폰해 Context::Run() 을 시작한다. 이미 실행 중이면 no-op.
		void_t Start();
		// 모든 Context 를 Stop() 하고 워커 스레드를 join 한다. 이미 정지 상태면 no-op. 소멸자가 호출한다.
		void_t Stop();

		// 다음 Context 를 round-robin 으로 고른다 — 새 연결을 워커에 고르게 배정하는 용도.
		[[nodiscard]] Context& Acquire() noexcept;
		// 인덱스로 특정 워커의 Context 를 얻는다(전용 acceptor 배치 등). _index < Size() 여야 한다.
		[[nodiscard]] Context& At(std::size_t _index) noexcept;

	public:
		[[nodiscard]] std::size_t Size() const noexcept { return workers.size(); }
		[[nodiscard]] bool_t IsRunning() const noexcept { return isRunning; }
	};

END_NS
