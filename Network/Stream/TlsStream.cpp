//
// Created by hscloud on 25. 6. 29.
//

#include "TlsStream.h"
#include "Coroutine/Awaitable.h"
#include <utility>

#ifdef NEBULA_WITH_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

BEGIN_NS(ne::network)

#ifdef NEBULA_WITH_OPENSSL

static ne::OsError SslError(string_view_t _ctx)
{
	const auto code = static_cast<ne::ulong_t>(ERR_get_error());
	const char* msg = ERR_error_string(code, nullptr);
	auto err = ne::OsError{ code, msg ? msg : "SSL error" };
	err.Context(_ctx);
	return err;
}

TlsStream::TlsStream(Socket&& _socket, IIoEngine& _engine,
                     void* _ctx, void* _ssl) noexcept
	: socket(std::move(_socket))
	, engine(&_engine)
	, ctx(_ctx)
	, ssl(_ssl) {}

TlsStream::TlsStream(TlsStream&& _other) noexcept
	: socket(std::move(_other.socket))
	, engine(_other.engine)
	, ctx(std::exchange(_other.ctx, nullptr))
	, ssl(std::exchange(_other.ssl, nullptr)) {}

TlsStream& TlsStream::operator=(TlsStream&& _other) noexcept
{
	if (this != &_other)
	{
		if (ssl) { SSL_shutdown(static_cast<SSL*>(ssl)); SSL_free(static_cast<SSL*>(ssl)); }
		if (ctx) { SSL_CTX_free(static_cast<SSL_CTX*>(ctx)); }
		socket = std::move(_other.socket);
		engine = _other.engine;
		ctx    = std::exchange(_other.ctx, nullptr);
		ssl    = std::exchange(_other.ssl, nullptr);
	}
	return *this;
}

TlsStream::~TlsStream()
{
	if (ssl) { SSL_shutdown(static_cast<SSL*>(ssl)); SSL_free(static_cast<SSL*>(ssl)); }
	if (ctx) { SSL_CTX_free(static_cast<SSL_CTX*>(ctx)); }
}

ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(
	Socket&&         _socket,
	IIoEngine&       _engine,
	string_view_t    _host,
	const TlsConfig& _config)
{
	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx)
		co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect]"));

	SSL_CTX_set_verify(ctx, _config.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);

	if (!_config.caFile.empty() &&
	    SSL_CTX_load_verify_locations(ctx, _config.caFile.c_str(), nullptr) != 1)
	{
		SSL_CTX_free(ctx);
		co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/CA]"));
	}
	if (!_config.certFile.empty())
	{
		if (SSL_CTX_use_certificate_file(ctx, _config.certFile.c_str(), SSL_FILETYPE_PEM) != 1 ||
		    SSL_CTX_use_PrivateKey_file(ctx, _config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1)
		{
			SSL_CTX_free(ctx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/Cert]"));
		}
	}

	SSL* ssl = SSL_new(ctx);
	if (!ssl) { SSL_CTX_free(ctx); co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/SSL]")); }

	SSL_set_fd(ssl, static_cast<int>(_socket.Handle()));
	if (!_host.empty())
		SSL_set_tlsext_host_name(ssl, _host.data());

	// Non-blocking handshake
	while (true)
	{
		const int ret = SSL_connect(ssl);
		if (ret == 1) break;

		const int err = SSL_get_error(ssl, ret);
		if (err == SSL_ERROR_WANT_READ)
		{
			if (auto r = co_await RecvAwaitable{ _socket.Handle(), _engine }; r.IsError())
			{
				SSL_free(ssl); SSL_CTX_free(ctx);
				co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));
			}
		}
		else if (err == SSL_ERROR_WANT_WRITE)
		{
			if (auto r = co_await SendAwaitable{ _socket.Handle(), _engine }; r.IsError())
			{
				SSL_free(ssl); SSL_CTX_free(ctx);
				co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));
			}
		}
		else
		{
			SSL_free(ssl); SSL_CTX_free(ctx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/Handshake]"));
		}
	}

	co_return ne::Result<TlsStream, ne::OsError>::Ok(
		TlsStream{ std::move(_socket), _engine, ctx, ssl }
	);
}

ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(std::span<const byte_t> _data)
{
	if (!IsOpen())
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

	auto* s = static_cast<SSL*>(ssl);
	std::size_t sent = 0;

	while (sent < _data.size())
	{
		const int n = SSL_write(s,
			_data.data() + sent,
			static_cast<int>(_data.size() - sent));

		if (n > 0) { sent += static_cast<std::size_t>(n); continue; }

		const int err = SSL_get_error(s, n);
		if (err == SSL_ERROR_WANT_WRITE)
		{
			if (auto r = co_await SendAwaitable{ socket.Handle(), *engine }; r.IsError())
				co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
		}
		else if (err == SSL_ERROR_WANT_READ)
		{
			if (auto r = co_await RecvAwaitable{ socket.Handle(), *engine }; r.IsError())
				co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
		}
		else
		{
			co_return ne::Result<std::size_t, ne::OsError>::Error(SslError("[TlsStream/Send]"));
		}
	}
	co_return ne::Result<std::size_t, ne::OsError>::Ok(sent);
}

ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(std::span<byte_t> _data)
{
	if (!IsOpen())
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

	auto* s = static_cast<SSL*>(ssl);
	while (true)
	{
		const int n = SSL_read(s, _data.data(), static_cast<int>(_data.size()));
		if (n > 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(n));
		if (n == 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(0); // clean shutdown

		const int err = SSL_get_error(s, n);
		if (err == SSL_ERROR_WANT_READ)
		{
			if (auto r = co_await RecvAwaitable{ socket.Handle(), *engine }; r.IsError())
				co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
		}
		else if (err == SSL_ERROR_WANT_WRITE)
		{
			if (auto r = co_await SendAwaitable{ socket.Handle(), *engine }; r.IsError())
				co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
		}
		else
		{
			co_return ne::Result<std::size_t, ne::OsError>::Error(SslError("[TlsStream/Receive]"));
		}
	}
}

ne::Result<void, ne::OsError> TlsStream::Close()
{
	if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();
	SSL_shutdown(static_cast<SSL*>(ssl));
	SSL_free(static_cast<SSL*>(ssl));   ssl = nullptr;
	SSL_CTX_free(static_cast<SSL_CTX*>(ctx)); ctx = nullptr;
	[[maybe_unused]] auto closing = std::move(socket);
	return ne::Result<void, ne::OsError>::Ok();
}

#else // NEBULA_WITH_OPENSSL not defined

static ne::OsError NoOpenSsl(string_view_t _ctx)
{
	auto err = ne::OsError{ 0, "built without OpenSSL (define NEBULA_WITH_OPENSSL)" };
	err.Context(_ctx);
	return err;
}

TlsStream::TlsStream(Socket&&, IIoEngine&, void*, void*) noexcept {}
TlsStream::TlsStream(TlsStream&&) noexcept = default;
TlsStream& TlsStream::operator=(TlsStream&&) noexcept = default;
TlsStream::~TlsStream() = default;

ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(
	Socket&&, IIoEngine&, string_view_t, const TlsConfig&)
{
	co_return ne::Result<TlsStream, ne::OsError>::Error(NoOpenSsl("[TlsStream/Connect]"));
}
ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(std::span<const byte_t>)
{
	co_return ne::Result<std::size_t, ne::OsError>::Error(NoOpenSsl("[TlsStream/Send]"));
}
ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(std::span<byte_t>)
{
	co_return ne::Result<std::size_t, ne::OsError>::Error(NoOpenSsl("[TlsStream/Receive]"));
}
ne::Result<void, ne::OsError> TlsStream::Close()
{
	return ne::Result<void, ne::OsError>::Ok();
}

#endif // NEBULA_WITH_OPENSSL

END_NS
