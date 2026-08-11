//
// Created by hscloud on 26. 7. 24.
//

#include "Network/Protocol/Http/Internal/Transport.h"

#include <charconv>
#include <utility>
#include "Io/Socket.h"
#include "Network/Dns.h"
#include "Network/Stream/PlainStream.h"
#include "Network/Stream/Tls/TlsStream.h"

#if defined(_WIN32)
#include "Base/WinsockApi.h"
#elif defined(IS_POSIX)
#   include <netinet/in.h>
#   include <sys/socket.h>
#endif



namespace ne::network::http::internal
{
	namespace
	{
		// TlsStream::Connect 는 이미 TCP 연결된 Socket 을 받으므로, PlainStream::Connect 의 DNS 후보
		// 페일오버 루프를 TLS 경로에서도 그대로 재현한다.
		ne::Task<HttpResult<ne::io::Socket>> ConnectSocket(const string_view_t _host, const uint16_t _port, ne::io::Context& _context, std::stop_token _stopToken)
		{
			using R = HttpResult<ne::io::Socket>;

			auto resolved = co_await ne::network::dns::Resolve(_host, _context);
			if (resolved.IsError()) co_return R::Error(HttpError(std::move(resolved.Error())).Context("[Transport/ConnectSocket]"));

			ne::io::IoError lastError{ ne::io::IoErrorKind::OS_FAILURE, "no candidate address" };
			for (const auto& [family, ip] : resolved.Value())
			{
				auto created = ne::io::Socket::Create(_context, family, SOCK_STREAM, IPPROTO_TCP);
				if (created.IsError())
				{
					lastError = std::move(created.Error());
					continue;
				}

				ne::io::Socket sock = std::move(created.Value());
				if (auto connected = co_await sock.Connect(ip, _port, _stopToken); connected.IsError())
				{
					lastError = std::move(connected.Error());
					continue;
				}

				co_return R::Ok(std::move(sock));
			}

			co_return R::Error(HttpError(std::move(lastError)).Context("[Transport/ConnectSocket]"));
		}
	}



	std::optional<ParsedUrl> ParseUrl(const string_view_t _url)
	{
		string_view_t rest = _url;
		bool_t isSecure;

		if (rest.starts_with("https://"))
		{
			isSecure = true;
			rest.remove_prefix(8);
		}
		else if (rest.starts_with("http://"))
		{
			isSecure = false;
			rest.remove_prefix(7);
		}
		else return std::nullopt;

		const auto slash = rest.find('/');
		const string_view_t authority = slash == string_view_t::npos ? rest : rest.substr(0, slash);
		string_t target = slash == string_view_t::npos ? "/" : string_t(rest.substr(slash));

		string_view_t host = authority;
		uint16_t port = isSecure ? 443 : 80;
		if (const auto colon = authority.find(':'); colon != string_view_t::npos)
		{
			host = authority.substr(0, colon);
			const auto portText = authority.substr(colon + 1);
			std::from_chars(portText.data(), portText.data() + portText.size(), port);
		}

		if (host.empty()) return std::nullopt;

		return ParsedUrl{ isSecure, string_t(host), port, std::move(target) };
	}

	ne::Task<HttpResult<std::unique_ptr<ne::network::IStream>>> EstablishStream(const string_view_t _host, const uint16_t _port, const bool_t _isSecure, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = HttpResult<std::unique_ptr<ne::network::IStream>>;

		if (_isSecure)
		{
			auto socket = co_await ConnectSocket(_host, _port, _context, _stopToken);
			if (socket.IsError()) co_return R::Error(std::move(socket.Error()));

			auto tls = co_await ne::network::TlsStream::Connect(std::move(socket.Value()), _context, _host, {}, _stopToken);
			if (tls.IsError()) co_return R::Error(HttpError(std::move(tls.Error())).Context("[Transport/EstablishStream]"));

			co_return R::Ok(std::make_unique<ne::network::TlsStream>(std::move(tls.Value())));
		}

		auto plain = co_await ne::network::PlainStream::Connect(_host, _port, _context, _stopToken);
		if (plain.IsError()) co_return R::Error(HttpError(std::move(plain.Error())).Context("[Transport/EstablishStream]"));

		co_return R::Ok(std::make_unique<ne::network::PlainStream>(std::move(plain.Value())));
	}

	ne::Task<HttpResult<EstablishedStream>> EstablishStream(const string_view_t _host, const uint16_t _port, const bool_t _isSecure, std::vector<string_t> _alpnProtocols, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = HttpResult<EstablishedStream>;

		if (_isSecure)
		{
			auto socket = co_await ConnectSocket(_host, _port, _context, _stopToken);
			if (socket.IsError()) co_return R::Error(std::move(socket.Error()));

			ne::network::TlsConfig config;
			config.alpnProtocols = std::move(_alpnProtocols);

			auto tls = co_await ne::network::TlsStream::Connect(std::move(socket.Value()), _context, _host, config, _stopToken);
			if (tls.IsError()) co_return R::Error(HttpError(std::move(tls.Error())).Context("[Transport/EstablishStream]"));

			string_t negotiated{ tls.Value().NegotiatedProtocol() };
			co_return R::Ok(EstablishedStream{ std::make_unique<ne::network::TlsStream>(std::move(tls.Value())), std::move(negotiated) });
		}

		auto plain = co_await ne::network::PlainStream::Connect(_host, _port, _context, _stopToken);
		if (plain.IsError()) co_return R::Error(HttpError(std::move(plain.Error())).Context("[Transport/EstablishStream]"));

		co_return R::Ok(EstablishedStream{ std::make_unique<ne::network::PlainStream>(std::move(plain.Value())), string_t{} });
	}



	// ───────────────────────── 리다이렉트 유틸 ─────────────────────────

	std::optional<string_t> ResolveLocation(const string_view_t _baseUrl, const string_view_t _location)
	{
		if (_location.empty()) return std::nullopt;

		// 절대 URL 은 그대로 사용(스킴/호스트 교체 리다이렉트 허용).
		if (_location.starts_with("http://") || _location.starts_with("https://")) return string_t(_location);

		const auto base = ParseUrl(_baseUrl);
		if (!base) return std::nullopt;

		const uint16_t defaultPort = base->isSecure ? 443 : 80;
		string_t origin = base->isSecure ? string_t("https://") : string_t("http://");
		origin += base->host;
		if (base->port != defaultPort) origin += ":" + std::to_string(base->port);

		if (_location.front() == '/') return origin + string_t(_location);

		// 상대 경로 — base target 의 디렉터리(마지막 '/' 까지, 쿼리 제외)에 붙인다.
		string_view_t path = base->target;
		if (const auto query = path.find('?'); query != string_view_t::npos) path = path.substr(0, query);
		const auto lastSlash = path.rfind('/');
		const string_t directory = (lastSlash == string_view_t::npos) ? string_t("/") : string_t(path.substr(0, lastSlash + 1));

		return origin + directory + string_t(_location);
	}

	bool_t IsRedirect(const int_t _statusCode) noexcept
	{
		return _statusCode == 301 || _statusCode == 302 || _statusCode == 303 || _statusCode == 307 || _statusCode == 308;
	}

	void_t AdaptRequestForRedirect(const int_t _statusCode, http::Request& _request)
	{
		const bool_t toGet = _statusCode == 303 || ((_statusCode == 301 || _statusCode == 302) && _request.method == Method::POST);
		if (!toGet) return; // 307/308 — 메서드/본문 유지

		_request.method = Method::GET;
		_request.body = Body{};
		_request.headers.Remove("Content-Length");
		_request.headers.Remove("Content-Type");
	}
}
