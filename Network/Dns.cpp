//
// Created by hscloud on 26. 7. 11.
//

#include "Network/Dns.h"

#include <atomic>
#include <coroutine>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "Base/Error.h"
#include "Concurrency/ThreadPool.h"

#if defined(_WIN32)
#include "Base/WinsockApi.h"
#elif defined(IS_POSIX)
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <sys/socket.h>
#endif



namespace ne::network::dns
{
	namespace
	{
		using ResolveResult = ne::io::IoResult<std::vector<Candidate>>;

		ne::concurrency::ThreadPool& WorkerPool()
		{
			static ne::concurrency::ThreadPool pool(2);
			return pool;
		}

		ResolveResult ResolveBlocking(const ne::string_t& _host)
		{
			in_addr v4{};
			if (::inet_pton(AF_INET, _host.c_str(), &v4) == 1) return ResolveResult::Ok({ Candidate{ AF_INET, _host } });

			in6_addr v6{};
			if (::inet_pton(AF_INET6, _host.c_str(), &v6) == 1) return ResolveResult::Ok({ Candidate{ AF_INET6, _host } });

			addrinfo hints{};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo* result = nullptr;
			if (::getaddrinfo(_host.c_str(), nullptr, &hints, &result) != 0) return ResolveResult::Error(ne::io::IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Dns/Resolve]"));

			std::vector<Candidate> candidates;
			ne::char_t buffer[INET6_ADDRSTRLEN]{};
			for (const addrinfo* candidate = result; candidate != nullptr; candidate = candidate->ai_next)
			{
				const ne::void_t* address = candidate->ai_family == AF_INET6 ? static_cast<const ne::void_t*>(&reinterpret_cast<const sockaddr_in6*>(candidate->ai_addr)->sin6_addr) : static_cast<const ne::void_t*>(&reinterpret_cast<const sockaddr_in*>(candidate->ai_addr)->sin_addr);

				if (::inet_ntop(candidate->ai_family, address, buffer, sizeof(buffer)) != nullptr) candidates.push_back(Candidate{ candidate->ai_family, buffer });
			}

			::freeaddrinfo(result);

			if (candidates.empty()) return ResolveResult::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "no address resolved" }.Context("[Dns/Resolve]"));

			return ResolveResult::Ok(std::move(candidates));
		}

		/**
		 * @class ResolveAwaitable
		 * @brief 블로킹 getaddrinfo 를 워커 스레드에 맡기고, 결과를 **호출자 루프 스레드에서** 재개하는 awaiter.
		 *
		 * @note 과거에는 워커 스레드가 직접 _handle.resume() 을 호출했다. 그러면 호출 코루틴의 나머지
		 * 전부 — PlainStream::Connect 의 Socket::Create/Connect 를 포함해 — 가 워커 스레드에서 실행된다.
		 * Io 엔진은 단일 스레드 전제(io_uring 은 SQ 링이 그래서 깨진다)이고, 리액터 엔진에서는 블로킹
		 * connect 가 DNS 풀의 워커 두 개를 잡아먹는다. IExecutor::Post 로 루프에 되돌려 준다.
		 *
		 * @note 상태를 shared_ptr 로 heap 에 둔 이유는, 코루틴 프레임이 결과 도착 전에 파괴될 수 있기
		 * 때문이다(취소/타임아웃). 그 경우 워커는 유효한 메모리에 결과를 쓰고 재개는 생략한다.
		 */
		class ResolveAwaitable
		{
		public:
			ResolveAwaitable(ne::string_t _host, ne::IExecutor& _executor)
				: shared(std::make_shared<Shared>())
				, host(std::move(_host))
				, executor(_executor) {}

			~ResolveAwaitable()
			{
				// PENDING 상태에서 사라지면 워커가 재개를 생략하도록 표시한다.
				if (shared != nullptr) (void_t)shared->state.exchange(State::ABANDONED, std::memory_order_acq_rel);
			}

			NEBULA_NON_COPYABLE_MOVABLE(ResolveAwaitable)

		private:
			enum class State : ne::byte_t
			{
				PENDING,
				DELIVERED,
				ABANDONED,
			};

			struct Shared
			{
				std::optional<ResolveResult> result;
				std::coroutine_handle<> handle{};
				std::atomic<State> state{ State::PENDING };
			};

		private:
			std::shared_ptr<Shared> shared;
			ne::string_t host;
			ne::IExecutor& executor;

		public:
			[[nodiscard]] bool await_ready() const noexcept { return false; }

			void await_suspend(const std::coroutine_handle<> _handle)
			{
				shared->handle = _handle;

				WorkerPool().Enqueue([state = shared, host = host, executor = &executor]() mutable
				{
					auto value = ResolveBlocking(host);

					// 결과를 먼저 채운 뒤 소유권을 인계한다. 직전 상태가 ABANDONED 면 재개할 코루틴이
					// 없으므로 아무것도 하지 않는다(shared_ptr 이 메모리를 정리한다).
					state->result.emplace(std::move(value));
					if (state->state.exchange(State::DELIVERED, std::memory_order_acq_rel) == State::ABANDONED) return;

					executor->Post(state->handle);
				});
			}

			[[nodiscard]] ResolveResult await_resume() noexcept { return std::move(*shared->result); }
		};
	}



	ne::Task<ResolveResult> Resolve(const string_view_t _host, ne::IExecutor& _executor)
	{
		co_return co_await ResolveAwaitable{ ne::string_t{ _host }, _executor };
	}
}
