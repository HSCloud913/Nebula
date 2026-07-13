//
// Created by hscloud on 26. 7. 11.
//

#include "Network/Dns/Dns.h"

#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>
#include "Base/Error.h"
#include "Concurrency/ThreadPool.h"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(IS_POSIX)
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <sys/socket.h>
#endif



BEGIN_NS(ne::network::dns)
	namespace
	{
		// DNS 조회(getaddrinfo) 전용 워커 풀 — 호출 스레드(이벤트 루프)를 막지 않기 위해 블로킹
		// 작업을 여기로 넘기고, 완료되면 작업 스레드에서 코루틴을 재개한다.
		ne::ThreadPool& WorkerPool()
		{
			static ne::ThreadPool pool(2);
			return pool;
		}

		template <typename F>
		class ResolveAwaitable
		{
		public:
			explicit ResolveAwaitable(F _function) noexcept
				: function(std::move(_function)) {}

			[[nodiscard]] ne::bool_t await_ready() const noexcept { return false; }

			void_t await_suspend(const std::coroutine_handle<> _handle)
			{
				WorkerPool().Enqueue([this, _handle]() mutable
				{
					result.emplace(function());
					_handle.resume();
				});
			}

			[[nodiscard]] auto await_resume() noexcept { return std::move(*result); }

		private:
			F function;
			std::optional<std::invoke_result_t<F>> result;
		};

		ne::io::IoResult<std::vector<Candidate>> ResolveBlocking(const ne::string_t& _host)
		{
			in_addr v4{};
			if (::inet_pton(AF_INET, _host.c_str(), &v4) == 1) return ne::io::IoResult<std::vector<Candidate>>::Ok({ Candidate{ AF_INET, _host } });

			in6_addr v6{};
			if (::inet_pton(AF_INET6, _host.c_str(), &v6) == 1) return ne::io::IoResult<std::vector<Candidate>>::Ok({ Candidate{ AF_INET6, _host } });

			// 호스트 이름 — 소켓 종류를 아직 모르므로 AF_UNSPEC 으로 A/AAAA 레코드를 전부 모은다.
			addrinfo hints{};
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo* result = nullptr;
			if (::getaddrinfo(_host.c_str(), nullptr, &hints, &result) != 0)
				return ne::io::IoResult<std::vector<Candidate>>::Error(ne::io::IoError{ ne::OsError{ ne::LastOsError() } }.Context("[Dns/Resolve]"));

			std::vector<Candidate> candidates;
			char_t buffer[INET6_ADDRSTRLEN]{};
			for (const addrinfo* candidate = result; candidate != nullptr; candidate = candidate->ai_next)
			{
				const void_t* address = candidate->ai_family == AF_INET6
					? static_cast<const void_t*>(&reinterpret_cast<const sockaddr_in6*>(candidate->ai_addr)->sin6_addr)
					: static_cast<const void_t*>(&reinterpret_cast<const sockaddr_in*>(candidate->ai_addr)->sin_addr);

				if (::inet_ntop(candidate->ai_family, address, buffer, sizeof(buffer)) != nullptr) candidates.push_back(Candidate{ candidate->ai_family, buffer });
			}

			::freeaddrinfo(result);

			if (candidates.empty()) return ne::io::IoResult<std::vector<Candidate>>::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "no address resolved" }.Context("[Dns/Resolve]"));

			return ne::io::IoResult<std::vector<Candidate>>::Ok(std::move(candidates));
		}
	}



	ne::Task<ne::io::IoResult<std::vector<Candidate>>> Resolve(const string_view_t _host)
	{
		ne::string_t hostString(_host);
		co_return co_await ResolveAwaitable([hostString = std::move(hostString)] { return ResolveBlocking(hostString); });
	}

END_NS
