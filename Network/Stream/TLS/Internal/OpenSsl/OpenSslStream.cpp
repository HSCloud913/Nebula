//
// Created by hscloud on 25. 6. 29.
//
// TlsStream 의 POSIX(OpenSSL) 백엔드 구현입니다 — Windows 판은 Internal/Schannel/SchannelStream.cpp.
// CMake 가 플랫폼에 따라 둘 중 하나만 컴파일하며, OpenSSL 이 없는 POSIX 구성에서는 이 파일 끝의
// 스텁이 모든 연산을 UNSUPPORTED 로 실패시킵니다.

#include "Network/Stream/Tls/TlsStream.h"

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#if defined(NEBULA_WITH_OPENSSL)
#   include "Network/Stream/Tls/Internal/Transfer.h"
#   include <openssl/err.h>
#   include <openssl/ssl.h>
#   include <openssl/x509v3.h>
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <sys/socket.h>

using namespace ne::network::internal; // Transfer(SendAll/RecvSome)
#endif

namespace ne::network
{
#if defined(NEBULA_WITH_OPENSSL)
	// OpenSSL 스레드 로컬 에러 큐에서 마지막 에러를 꺼내 이 라이브러리의 공통 IoError 로 변환한다.
	static ne::io::IoError SslError(const string_view_t _ctx)
	{
		const auto code = static_cast<ne::ulong_t>(ERR_get_error());
		const char* message = ERR_error_string(code, nullptr);

		return ne::io::IoError{ ne::OsError{ code, message ? message : "SSL error" } }.Context(_ctx);
	}

	// 접속 대상이 DNS 이름이 아니라 IP 리터럴인지 판정한다 — SNI 제외 및 IP SAN 검증 분기에 쓴다.
	static bool_t IsAddressLiteral(const string_t& _host)
	{
		in_addr v4{};
		if (::inet_pton(AF_INET, _host.c_str(), &v4) == 1) return true;

		in6_addr v6{};
		return ::inet_pton(AF_INET6, _host.c_str(), &v6) == 1;
	}



	TlsStream::~TlsStream()
	{
		if (ssl)
		{
			SSL_shutdown(static_cast<SSL*>(ssl));
			SSL_free(static_cast<SSL*>(ssl));
		}
		if (ctx) SSL_CTX_free(static_cast<SSL_CTX*>(ctx));
	}

	TlsStream& TlsStream::operator=(TlsStream&& _other) noexcept
	{
		if (this != &_other)
		{
			if (ssl)
			{
				SSL_shutdown(static_cast<SSL*>(ssl));
				SSL_free(static_cast<SSL*>(ssl));
			}
			if (ctx) SSL_CTX_free(static_cast<SSL_CTX*>(ctx));

			transport = std::move(_other.transport);
			sniHost = std::move(_other.sniHost);
			alpnCandidates = std::move(_other.alpnCandidates);
			negotiatedProtocol = std::move(_other.negotiatedProtocol);
			allocator = _other.allocator;
			ctx = std::exchange(_other.ctx, nullptr);
			ssl = std::exchange(_other.ssl, nullptr);
		}

		return *this;
	}



	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Connect(ne::io::Socket&& _socket, ne::io::Context& _context, const string_view_t _host, const TlsConfig& _config, std::stop_token _stopToken, ne::memory::IAllocator* _allocator)
	{
		using R = ne::io::IoResult<TlsStream>;

		SSL_CTX* sslCtx = SSL_CTX_new(TLS_client_method());
		if (!sslCtx) co_return R::Error(SslError("[TlsStream/Connect]"));

		SSL_CTX_set_verify(sslCtx, _config.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);

		// caFile 이 지정되면 그것만 신뢰하고, 없으면 **시스템 신뢰 저장소**를 읽는다. 과거에는 후자가
		// 빠져 있어, verifyPeer=true(기본값) + caFile 미지정이면 신뢰 앵커가 하나도 없는 상태로
		// 검증을 요구해 모든 실제 HTTPS 접속이 "unable to get local issuer certificate" 로 실패했다.
		// Windows/Schannel 은 시스템 저장소를 자동으로 쓰므로, 조용한 플랫폼 비대칭이기도 했다.
		if (!_config.caFile.empty())
		{
			if (SSL_CTX_load_verify_locations(sslCtx, _config.caFile.c_str(), nullptr) != 1)
			{
				SSL_CTX_free(sslCtx);
				co_return R::Error(SslError("[TlsStream/Connect/CA]"));
			}
		}
		else if (_config.verifyPeer && SSL_CTX_set_default_verify_paths(sslCtx) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return R::Error(SslError("[TlsStream/Connect/DefaultTrustStore]"));
		}
		if (!_config.certFile.empty())
		{
			if (SSL_CTX_use_certificate_file(sslCtx, _config.certFile.c_str(), SSL_FILETYPE_PEM) != 1 || SSL_CTX_use_PrivateKey_file(sslCtx, _config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1)
			{
				SSL_CTX_free(sslCtx);
				co_return R::Error(SslError("[TlsStream/Connect/Cert]"));
			}
		}

		if (!_config.alpnProtocols.empty())
		{
			std::vector<byte_t> wire;
			for (const auto& proto : _config.alpnProtocols)
			{
				wire.push_back(static_cast<byte_t>(proto.size()));
				wire.insert(wire.end(), proto.begin(), proto.end());
			}
			SSL_CTX_set_alpn_protos(sslCtx, wire.data(), static_cast<unsigned int>(wire.size()));
		}

		SSL* tempSsl = SSL_new(sslCtx);
		if (!tempSsl)
		{
			SSL_CTX_free(sslCtx);
			co_return R::Error(SslError("[TlsStream/Connect/SSL]"));
		}

		auto transportResult = PlainStream::Create(std::move(_socket), _context, _allocator);
		if (transportResult.IsError())
		{
			SSL_free(tempSsl);
			SSL_CTX_free(sslCtx);
			co_return R::Error(std::move(transportResult.Error()).Context("[TlsStream/Connect]"));
		}

		PlainStream plainTransport = std::move(transportResult.Value());
		SSL_set_fd(tempSsl, static_cast<int>(plainTransport.Handle()));

		if (!_host.empty())
		{
			const string_t host(_host);

			// SNI 는 IP 리터럴에 쓰지 않는다(RFC 6066 §3) — 서버가 거부할 수 있다.
			if (!IsAddressLiteral(host)) SSL_set_tlsext_host_name(tempSsl, host.c_str());

			// **인증서 이름 검증**: 체인 검증(SSL_CTX_set_verify)만으로는 "신뢰된 CA 가 발급한 아무 이름의
			// 인증서"도 통과하므로 MITM 을 막지 못한다. 검증 대상 이름을 지정해야 OpenSSL 이 SAN/CN 을
			// 대조하고, 불일치 시 핸드셰이크가 실패한다.
			if (_config.verifyPeer)
			{
				// 부분 와일드카드(`w*.example.com`)는 거부한다 — 표준이 요구하지 않고 위험만 크다.
				SSL_set_hostflags(tempSsl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

				// IP 로 접속하는 경우 DNS 이름이 아니라 인증서의 IP SAN 과 대조해야 한다.
				const int_t nameResult = IsAddressLiteral(host) ? SSL_set1_ip_asc(tempSsl, host.c_str()) : SSL_set1_host(tempSsl, host.c_str());
				if (nameResult != 1)
				{
					SSL_free(tempSsl);
					SSL_CTX_free(sslCtx);
					co_return R::Error(SslError("[TlsStream/Connect/HostVerify]"));
				}
			}
		}

		TlsStream stream(std::move(plainTransport), sslCtx, tempSsl, _allocator);
		stream.sniHost = string_t(_host);
		stream.alpnCandidates = _config.alpnProtocols;

		if (auto result = co_await stream.Handshake(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));

		co_return R::Ok(std::move(stream));
	}

	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Accept(ne::io::Socket&& _socket, ne::io::Context& _context, const TlsConfig& _config, std::stop_token _stopToken, ne::memory::IAllocator* _allocator)
	{
		using R = ne::io::IoResult<TlsStream>;

		SSL_CTX* sslCtx = SSL_CTX_new(TLS_server_method());
		if (!sslCtx) co_return R::Error(SslError("[TlsStream/Accept]"));

		SSL_CTX_set_verify(sslCtx, _config.verifyPeer ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT : SSL_VERIFY_NONE, nullptr);
		if (!_config.caFile.empty() && SSL_CTX_load_verify_locations(sslCtx, _config.caFile.c_str(), nullptr) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return R::Error(SslError("[TlsStream/Accept/CA]"));
		}
		if (SSL_CTX_use_certificate_file(sslCtx, _config.certFile.c_str(), SSL_FILETYPE_PEM) != 1 || SSL_CTX_use_PrivateKey_file(sslCtx, _config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return R::Error(SslError("[TlsStream/Accept/Cert]"));
		}

		SSL* tempSsl = SSL_new(sslCtx);
		if (!tempSsl)
		{
			SSL_CTX_free(sslCtx);
			co_return R::Error(SslError("[TlsStream/Accept/SSL]"));
		}

		auto transportResult = PlainStream::Create(std::move(_socket), _context, _allocator);
		if (transportResult.IsError())
		{
			SSL_free(tempSsl);
			SSL_CTX_free(sslCtx);
			co_return R::Error(std::move(transportResult.Error()).Context("[TlsStream/Accept]"));
		}

		PlainStream plainTransport = std::move(transportResult.Value());
		SSL_set_fd(tempSsl, static_cast<int>(plainTransport.Handle()));

		TlsStream stream(std::move(plainTransport), sslCtx, tempSsl, _allocator);
		stream.alpnCandidates = _config.alpnProtocols;

		// 콜백은 SSL_accept() 진행 중(아래 루프, stream 이 아직 이 지역 변수 위치에 있는 동안)에만 호출된다 —
		// TLS 1.3 은 재협상이 없으므로 co_return 이후(스트림이 이동된 뒤) 다시 불릴 일이 없다.
		if (!stream.alpnCandidates.empty())
		{
			SSL_CTX_set_alpn_select_cb(sslCtx,
										[](SSL*, const unsigned char** _out, unsigned char* _outLen, const unsigned char* _in, unsigned int _inLen, void* _arg) -> int
										{
											const auto* candidates = static_cast<std::vector<string_t>*>(_arg);
											for (const auto& proto : *candidates)
											{
												for (unsigned int i = 0; i < _inLen;)
												{
													const unsigned char segmentLength = _in[i];
													if (segmentLength == proto.size() && i + 1u + segmentLength <= _inLen && std::memcmp(_in + i + 1, proto.data(), segmentLength) == 0)
													{
														*_out = _in + i + 1;
														*_outLen = segmentLength;
														return SSL_TLSEXT_ERR_OK;
													}
													i += 1u + segmentLength;
												}
											}
											return SSL_TLSEXT_ERR_NOACK;
										},
										&stream.alpnCandidates);
		}

		while (true)
		{
			const int sslResult = SSL_accept(tempSsl);
			if (sslResult == 1) break;

			const int sslError = SSL_get_error(tempSsl, sslResult);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await stream.transport.WaitReadable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await stream.transport.WaitWritable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}

			co_return R::Error(SslError("[TlsStream/Accept/Handshake]"));
		}

		const unsigned char* alpnData = nullptr;
		unsigned int alpnLen = 0;
		SSL_get0_alpn_selected(tempSsl, &alpnData, &alpnLen);
		if (alpnData && alpnLen > 0) stream.negotiatedProtocol.assign(reinterpret_cast<const char*>(alpnData), alpnLen);

		co_return R::Ok(std::move(stream));
	}



	ne::Task<ne::io::IoResult<void_t>> TlsStream::Handshake(std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<void_t>;

		auto* nativeSsl = static_cast<SSL*>(ssl);

		while (true)
		{
			const int sslResult = SSL_connect(nativeSsl);
			if (sslResult == 1) break;

			const int sslError = SSL_get_error(nativeSsl, sslResult);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await transport.WaitReadable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await transport.WaitWritable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}

			co_return R::Error(SslError("[TlsStream/Handshake]"));
		}

		const unsigned char* alpnData = nullptr;
		unsigned int alpnLen = 0;
		SSL_get0_alpn_selected(nativeSsl, &alpnData, &alpnLen);
		if (alpnData && alpnLen > 0) negotiatedProtocol.assign(reinterpret_cast<const char*>(alpnData), alpnLen);

		co_return R::Ok();
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Send(const ne::memory::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Send]"));

		const auto dataSpan = _data.Span();
		std::size_t sent = 0;
		auto* nativeSsl = static_cast<SSL*>(ssl);

		while (sent < dataSpan.size())
		{
			const int bytes = SSL_write(nativeSsl, dataSpan.data() + sent, static_cast<int>(dataSpan.size() - sent));
			if (bytes > 0)
			{
				sent += static_cast<std::size_t>(bytes);
				continue;
			}

			const int sslError = SSL_get_error(nativeSsl, bytes);
			if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await transport.WaitWritable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}
			else if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await transport.WaitReadable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}

			co_return R::Error(SslError("[TlsStream/Send]"));
		}

		co_return R::Ok(sent);
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receive(const ne::memory::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Receive]"));

		auto* nativeSsl = static_cast<SSL*>(ssl);
		while (true)
		{
			const int bytes = SSL_read(nativeSsl, _data.ptr, static_cast<int>(_data.length));
			if (bytes > 0) co_return R::Ok(static_cast<std::size_t>(bytes));
			if (bytes == 0) co_return R::Ok(0);

			const int sslError = SSL_get_error(nativeSsl, bytes);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await transport.WaitReadable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await transport.WaitWritable(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));
				continue;
			}

			co_return R::Error(SslError("[TlsStream/Receive]"));
		}
	}

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Shutdown()
	{
		(void_t)Close();
		co_return ne::io::IoResult<void_t>::Ok();
	}

	ne::io::IoResult<void_t> TlsStream::Close()
	{
		using R = ne::io::IoResult<void_t>;
		if (!IsOpen()) return R::Ok();

		SSL_shutdown(static_cast<SSL*>(ssl));
		SSL_free(static_cast<SSL*>(ssl));
		ssl = nullptr;

		SSL_CTX_free(static_cast<SSL_CTX*>(ctx));
		ctx = nullptr;

		(void_t)transport.Close(); // 소켓 소멸

		return R::Ok();
	}
#else
	// TLS 백엔드가 전혀 빌드되지 않은 구성(POSIX + NEBULA_WITH_OPENSSL 미정의)에서 모든 연산을 실패시킨다.
	static ne::io::IoError NoTls(const string_view_t _ctx) { return ne::io::IoError{ ne::io::IoErrorKind::UNSUPPORTED, "TLS not available (define NEBULA_WITH_OPENSSL on POSIX)" }.Context(_ctx); }



	// 이동 생성자는 TlsStream.h 가 세 백엔드 분기 모두에 대해 인라인 정의한다 — 여기서 다시 정의하면
	// 재정의 오류로 이 구성 자체가 컴파일되지 않는다(과거 그 상태였다).
	TlsStream::~TlsStream() = default;
	TlsStream& TlsStream::operator=(TlsStream&&) noexcept = default;



	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Connect(ne::io::Socket&&, ne::io::Context&, string_view_t, const TlsConfig&, std::stop_token, ne::memory::IAllocator*) { co_return ne::io::IoResult<TlsStream>::Error(NoTls("[TlsStream/Connect]")); }

	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Accept(ne::io::Socket&&, ne::io::Context&, const TlsConfig&, std::stop_token, ne::memory::IAllocator*) { co_return ne::io::IoResult<TlsStream>::Error(NoTls("[TlsStream/Accept]")); }

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Handshake(std::stop_token) { co_return ne::io::IoResult<void_t>::Error(NoTls("[TlsStream/Handshake]")); }

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Send(ne::memory::BufferView, std::stop_token) { co_return ne::io::IoResult<std::size_t>::Error(NoTls("[TlsStream/Send]")); }

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receive(ne::memory::BufferView, std::stop_token) { co_return ne::io::IoResult<std::size_t>::Error(NoTls("[TlsStream/Receive]")); }

	// Sendv/Receivev 는 백엔드 무관 공통 구현(TlsStream.cpp)이 담당한다 — Send/Receive 가 UNSUPPORTED 를
	// 돌려주므로 이 구성에서도 자연히 실패로 전파된다.

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Shutdown() { co_return ne::io::IoResult<void_t>::Ok(); }

	ne::io::IoResult<void_t> TlsStream::Close() { return ne::io::IoResult<void_t>::Ok(); }
#endif
}
