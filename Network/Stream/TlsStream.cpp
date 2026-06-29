//
// Created by hscloud on 25. 6. 29.
//

#include "TlsStream.h"
#include "Coroutine/Awaitable.h"
#include <utility>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#	include "Schannel/SspiWrapper.h"
#	include "Schannel/TlsMessageBuffer.h"
#	include "StringFormat.h"
#	include <winsock2.h>
#elif defined(NEBULA_WITH_OPENSSL)
#	include <openssl/ssl.h>
#	include <openssl/err.h>
#	include <sys/socket.h>
#endif

BEGIN_NS(ne::network)
#if defined(_WIN32)
	static ne::OsError SchannelError(SECURITY_STATUS _ss, string_view_t _ctx)
	{
		auto err = ne::OsError{ static_cast<ne::ulong_t>(_ss), "SChannel error" };
		err.Context(_ctx);
		return err;
	}



	TlsStream::TlsStream(Socket&& _socket, IIoEngine& _engine,
		void* _cred, void* _ctx, void* _buf) noexcept
		: socket(std::move(_socket))
		, engine(&_engine)
		, credHandle(_cred)
		, ctxHandle(_ctx)
		, msgBuffer(_buf) {}

	TlsStream::TlsStream(TlsStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine)
		, credHandle(std::exchange(_other.credHandle, nullptr))
		, ctxHandle(std::exchange(_other.ctxHandle, nullptr))
		, msgBuffer(std::exchange(_other.msgBuffer, nullptr)) {}

	TlsStream& TlsStream::operator=(TlsStream&& _other) noexcept
	{
		if (this != &_other)
		{
			(void)Close();
			socket = std::move(_other.socket);
			engine = _other.engine;
			credHandle = std::exchange(_other.credHandle, nullptr);
			ctxHandle = std::exchange(_other.ctxHandle, nullptr);
			msgBuffer = std::exchange(_other.msgBuffer, nullptr);
		}

		return *this;
	}

	TlsStream::~TlsStream()
	{
		if (ctxHandle)
		{
			if (auto* fn = SspiWrapper::Get()) fn->DeleteSecurityContext(static_cast<CtxtHandle*>(ctxHandle));
			delete static_cast<CtxtHandle*>(ctxHandle);
		}
		if (credHandle)
		{
			if (auto* fn = SspiWrapper::Get()) fn->FreeCredentialHandle(static_cast<CredHandle*>(credHandle));
			delete static_cast<CredHandle*>(credHandle);
		}
		delete static_cast<TlsMessageBuffer*>(msgBuffer);
	}



	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(Socket&& _socket, IIoEngine& _engine, string_view_t _host, const TlsConfig& _config)
	{
		auto* fn = SspiWrapper::Get();
		if (!fn)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				ne::OsError{ 0, "SChannel: secur32.dll load failed" });

		// ── 자격증명 획득 ──
		SCHANNEL_CRED credData{};
		credData.dwVersion = SCHANNEL_CRED_VERSION;
		credData.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
		credData.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
		if (_config.verifyPeer) credData.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;
		else credData.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_SERVERNAME_CHECK;

		auto rawCred = std::unique_ptr<CredHandle>(new CredHandle{});
		TimeStamp timeLimit{};
		SECURITY_STATUS ss = fn->AcquireCredentialsHandleW(
			nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND,
			nullptr, &credData, nullptr, nullptr, rawCred.get(), &timeLimit);
		if (ss != SEC_E_OK)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Connect/AcquireCred]"));

		auto rawCtx = std::unique_ptr<CtxtHandle>(new CtxtHandle{});
		auto rawBuf = std::unique_ptr<TlsMessageBuffer>(new TlsMessageBuffer(TlsMessageBuffer::Allocate()));

		std::wstring whost = ne::StringFormat::UTF8toWCS(string_t(_host).c_str());
		wchar_t* phost = whost.empty() ? nullptr : whost.data();

		constexpr ULONG kReqFlags =
		ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
		ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

		bool firstCall = true;
		std::size_t dataInBuf = 0;

		// ── 핸드셰이크 루프 ──
		while (true)
		{
			// 입력 버퍼 준비 (첫 호출은 null)
			std::array<SecBuffer, 2> inBufs{};
			SecBufferDesc inDesc{};
			PSecBufferDesc pInDesc = nullptr;

			if (!firstCall)
			{
				inBufs[0] = { static_cast<ULONG>(dataInBuf), SECBUFFER_TOKEN, rawBuf->GetBuffer().data() };
				inBufs[1] = { 0, SECBUFFER_EMPTY, nullptr };
				inDesc = { SECBUFFER_VERSION, 2, inBufs.data() };
				pInDesc = &inDesc;
			}

			// 출력 버퍼 (SSPI가 할당)
			std::array<SecBuffer, 2> outBufs{};
			outBufs[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBufs[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBufs.data() };

			ULONG retFlags = 0;
			ss = fn->InitializeSecurityContextW(
				rawCred.get(),
				firstCall ? nullptr : rawCtx.get(),
				phost,
				kReqFlags, 0, 0,
				pInDesc,
				0,
				firstCall ? rawCtx.get() : nullptr,
				&outDesc,
				&retFlags,
				nullptr);

			if (ss == SEC_I_COMPLETE_AND_CONTINUE || ss == SEC_I_COMPLETE_NEEDED)
			{
				fn->CompleteAuthToken(rawCtx.get(), &outDesc);
				ss = (ss == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
			}

			// SECBUFFER_EXTRA 처리: 남은 데이터를 버퍼 앞으로 이동
			if (!firstCall && inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0)
			{
				auto span = rawBuf->GetBuffer();
				std::memmove(span.data(),
							span.data() + (dataInBuf - inBufs[1].cbBuffer),
							inBufs[1].cbBuffer);
				dataInBuf = inBufs[1].cbBuffer;
			}
			else if (!firstCall)
			{
				dataInBuf = 0;
			}

			// 출력 데이터 전송
			if (outBufs[0].pvBuffer && outBufs[0].cbBuffer > 0)
			{
				if (auto r = co_await SendAwaitable{ _socket.Handle(), _engine }; r.IsError())
				{
					fn->FreeContextBuffer(outBufs[0].pvBuffer);
					co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));
				}
				int n = ::send(_socket.Handle(),
								static_cast<const char*>(outBufs[0].pvBuffer),
								static_cast<int>(outBufs[0].cbBuffer), 0);
				fn->FreeContextBuffer(outBufs[0].pvBuffer);
				if (n < 0)
					co_return ne::Result<TlsStream, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Connect/Send]"));
			}

			if (ss == SEC_E_OK) break;

			if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE)
			{
				// 서버 데이터 수신
				auto span = rawBuf->GetBuffer();
				if (dataInBuf >= span.size())
				{
					rawBuf->Resize(span.size() * 2);
					span = rawBuf->GetBuffer();
				}

				if (auto r = co_await RecvAwaitable{ _socket.Handle(), _engine }; r.IsError()) co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));

				int n = ::recv(_socket.Handle(),
								reinterpret_cast<char*>(span.data() + dataInBuf),
								static_cast<int>(span.size() - dataInBuf), 0);
				if (n <= 0)
					co_return ne::Result<TlsStream, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Connect/Recv]"));

				dataInBuf += static_cast<std::size_t>(n);
				firstCall = false;
				continue;
			}

			co_return ne::Result<TlsStream, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Connect/Handshake]"));
		}

		co_return ne::Result<TlsStream, ne::OsError>::Ok(
			TlsStream{ std::move(_socket), _engine,
						rawCred.release(), rawCtx.release(), rawBuf.release() });
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(const std::span<const byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		auto* fn = SspiWrapper::Get();
		auto* ctx = static_cast<CtxtHandle*>(ctxHandle);

		SecPkgContext_StreamSizes sizes{};
		SECURITY_STATUS ss = fn->QueryContextAttributesW(ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
		if (ss != SEC_E_OK)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Send/QueryAttr]"));

		std::size_t totalSent = 0;
		const std::size_t maxMsg = sizes.cbMaximumMessage;

		while (totalSent < _data.size())
		{
			const std::size_t chunk = std::min(_data.size() - totalSent, maxMsg);
			std::vector<byte_t> encBuf(sizes.cbHeader + chunk + sizes.cbTrailer);

			std::memcpy(encBuf.data() + sizes.cbHeader, _data.data() + totalSent, chunk);

			std::array<SecBuffer, 4> bufs{};
			bufs[0] = { sizes.cbHeader, SECBUFFER_STREAM_HEADER, encBuf.data() };
			bufs[1] = { static_cast<ULONG>(chunk), SECBUFFER_DATA, encBuf.data() + sizes.cbHeader };
			bufs[2] = { sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, encBuf.data() + sizes.cbHeader + chunk };
			bufs[3] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs.data() };

			ss = fn->EncryptMessage(ctx, 0, &desc, 0);
			if (ss != SEC_E_OK)
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					SchannelError(ss, "[TlsStream/Send/Encrypt]"));

			const ULONG encSize = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;

			if (auto r = co_await SendAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));

			int n = ::send(socket.Handle(), reinterpret_cast<const char*>(encBuf.data()), static_cast<int>(encSize), 0);
			if (n < 0)
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Send]"));

			totalSent += chunk;
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(totalSent);
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(std::span<byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		auto* fn = SspiWrapper::Get();
		auto* ctx = static_cast<CtxtHandle*>(ctxHandle);
		auto* mbuf = static_cast<TlsMessageBuffer*>(msgBuffer);
		auto span = mbuf->GetBuffer();

		// 이전 DecryptMessage 에서 남은 extra 데이터를 앞으로 이동
		std::size_t dataInBuf = 0;
		if (!mbuf->data.empty())
		{
			dataInBuf = mbuf->data.size();
			std::memmove(span.data(), mbuf->data.data(), dataInBuf);
			mbuf->data = {};
		}

		while (true)
		{
			// 버퍼가 비어 있으면 소켓에서 수신
			if (dataInBuf == 0)
			{
				if (auto r = co_await RecvAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));

				int n = ::recv(socket.Handle(), reinterpret_cast<char*>(span.data()), static_cast<int>(span.size()), 0);
				if (n <= 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(0); // EOF / clean close
				dataInBuf = static_cast<std::size_t>(n);
			}

			std::array<SecBuffer, 4> bufs{};
			bufs[0] = { static_cast<ULONG>(dataInBuf), SECBUFFER_DATA, span.data() };
			bufs[1] = { 0, SECBUFFER_EMPTY, nullptr };
			bufs[2] = { 0, SECBUFFER_EMPTY, nullptr };
			bufs[3] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs.data() };

			SECURITY_STATUS ss = fn->DecryptMessage(ctx, &desc, 0, nullptr);

			if (ss == SEC_E_OK)
			{
				// 복호화된 평문 + extra 처리
				for (int i = 0; i < 4; ++i)
				{
					if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].pvBuffer)
					{
						const std::size_t plainLen = std::min<std::size_t>(bufs[i].cbBuffer, _data.size());
						std::memcpy(_data.data(), bufs[i].pvBuffer, plainLen);

						for (int j = 0; j < 4; ++j)
						{
							if (bufs[j].BufferType == SECBUFFER_EXTRA && bufs[j].pvBuffer && bufs[j].cbBuffer > 0)
							{
								const auto* p = static_cast<const byte_t*>(bufs[j].pvBuffer);
								mbuf->data = span.subspan(static_cast<std::size_t>(p - span.data()), bufs[j].cbBuffer);
							}
						}
						co_return ne::Result<std::size_t, ne::OsError>::Ok(plainLen);
					}
				}
				// SECBUFFER_DATA 없음 → 재협상 등, 다시 읽기
				dataInBuf = 0;
				continue;
			}

			if (ss == SEC_I_CONTEXT_EXPIRED) co_return ne::Result<std::size_t, ne::OsError>::Ok(0); // TLS close_notify

			if (ss == SEC_E_INCOMPLETE_MESSAGE)
			{
				if (dataInBuf >= span.size())
				{
					mbuf->Resize(span.size() * 2);
					span = mbuf->GetBuffer();
				}

				if (auto r = co_await RecvAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));

				int n = ::recv(socket.Handle(),
								reinterpret_cast<char*>(span.data() + dataInBuf),
								static_cast<int>(span.size() - dataInBuf), 0);
				if (n <= 0)
					co_return ne::Result<std::size_t, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Receive/Recv]"));
				dataInBuf += static_cast<std::size_t>(n);
				continue;
			}

			co_return ne::Result<std::size_t, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Receive/Decrypt]"));
		}
	}

	ne::Result<void, ne::OsError> TlsStream::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

		auto* fn = SspiWrapper::Get();
		auto* cred = static_cast<CredHandle*>(credHandle);
		auto* ctx = static_cast<CtxtHandle*>(ctxHandle);

		// close_notify 전송
		if (fn)
		{
			DWORD dwType = SCHANNEL_SHUTDOWN;
			SecBuffer shut = { sizeof(dwType), SECBUFFER_TOKEN, &dwType };
			SecBufferDesc shutDesc = { SECBUFFER_VERSION, 1, &shut };
			fn->ApplyControlToken(ctx, &shutDesc);

			std::array<SecBuffer, 2> outBufs{};
			outBufs[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBufs[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBufs.data() };

			constexpr ULONG kReqFlags =
			ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
			ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
			ULONG retFlags = 0;

			fn->InitializeSecurityContextW(
				cred, ctx, nullptr, kReqFlags, 0, 0,
				nullptr, 0, nullptr, &outDesc, &retFlags, nullptr);

			if (outBufs[0].pvBuffer && outBufs[0].cbBuffer > 0)
			{
				::send(socket.Handle(),
						static_cast<const char*>(outBufs[0].pvBuffer),
						static_cast<int>(outBufs[0].cbBuffer), 0);
				fn->FreeContextBuffer(outBufs[0].pvBuffer);
			}

			fn->DeleteSecurityContext(ctx);
			fn->FreeCredentialHandle(cred);
		}

		delete static_cast<CtxtHandle*>(ctxHandle);
		delete static_cast<CredHandle*>(credHandle);
		delete static_cast<TlsMessageBuffer*>(msgBuffer);
		ctxHandle = credHandle = msgBuffer = nullptr;

		(void)engine->Unwatch(socket.Handle());
		[[maybe_unused]] auto closing = std::move(socket);
		return ne::Result<void, ne::OsError>::Ok();
	}

#elif defined(NEBULA_WITH_OPENSSL)
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
			if (ssl)
			{
				SSL_shutdown(static_cast<SSL*>(ssl));
				SSL_free(static_cast<SSL*>(ssl));
			}
			if (ctx) { SSL_CTX_free(static_cast<SSL_CTX*>(ctx)); }
			socket = std::move(_other.socket);
			engine = _other.engine;
			ctx = std::exchange(_other.ctx, nullptr);
			ssl = std::exchange(_other.ssl, nullptr);
		}
		return *this;
	}

	TlsStream::~TlsStream()
	{
		if (ssl)
		{
			SSL_shutdown(static_cast<SSL*>(ssl));
			SSL_free(static_cast<SSL*>(ssl));
		}
		if (ctx) { SSL_CTX_free(static_cast<SSL_CTX*>(ctx)); }
	}



	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(Socket&& _socket, IIoEngine& _engine, string_view_t _host, const TlsConfig& _config)
	{
		SSL_CTX* sslCtx = SSL_CTX_new(TLS_client_method());
		if (!sslCtx) co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect]"));

		SSL_CTX_set_verify(sslCtx, _config.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);

		if (!_config.caFile.empty() &&
			SSL_CTX_load_verify_locations(sslCtx, _config.caFile.c_str(), nullptr) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/CA]"));
		}
		if (!_config.certFile.empty())
		{
			if (SSL_CTX_use_certificate_file(sslCtx, _config.certFile.c_str(), SSL_FILETYPE_PEM) != 1 ||
				SSL_CTX_use_PrivateKey_file(sslCtx, _config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1)
			{
				SSL_CTX_free(sslCtx);
				co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/Cert]"));
			}
		}

		SSL* s = SSL_new(sslCtx);
		if (!s)
		{
			SSL_CTX_free(sslCtx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/SSL]"));
		}

		SSL_set_fd(s, static_cast<int>(_socket.Handle()));
		if (!_host.empty()) SSL_set_tlsext_host_name(s, _host.data());

		while (true)
		{
			const int ret = SSL_connect(s);
			if (ret == 1) break;
			const int err = SSL_get_error(s, ret);
			if (err == SSL_ERROR_WANT_READ)
			{
				if (auto r = co_await RecvAwaitable{ _socket.Handle(), _engine }; r.IsError())
				{
					SSL_free(s);
					SSL_CTX_free(sslCtx);
					co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));
				}
			}
			else if (err == SSL_ERROR_WANT_WRITE)
			{
				if (auto r = co_await SendAwaitable{ _socket.Handle(), _engine }; r.IsError())
				{
					SSL_free(s);
					SSL_CTX_free(sslCtx);
					co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));
				}
			}
			else
			{
				SSL_free(s);
				SSL_CTX_free(sslCtx);
				co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/Handshake]"));
			}
		}

		co_return ne::Result<TlsStream, ne::OsError>::Ok(TlsStream{ std::move(_socket), _engine, sslCtx, s });
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(std::span<const byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		auto* s = static_cast<SSL*>(ssl);
		std::size_t sent = 0;

		while (sent < _data.size())
		{
			const int bytes = SSL_write(s, _data.data() + sent, static_cast<int>(_data.size() - sent));
			if (bytes > 0)
			{
				sent += static_cast<std::size_t>(bytes);
				continue;
			}

			const int err = SSL_get_error(s, bytes);
			if (err == SSL_ERROR_WANT_WRITE)
			{
				if (auto r = co_await SendAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
			}
			else if (err == SSL_ERROR_WANT_READ)
			{
				if (auto r = co_await RecvAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
			}
			else co_return ne::Result<std::size_t, ne::OsError>::Error(SslError("[TlsStream/Send]"));
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(sent);
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(std::span<byte_t> _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		auto* s = static_cast<SSL*>(ssl);
		while (true)
		{
			const int bytes = SSL_read(s, _data.data(), static_cast<int>(_data.size()));
			if (bytes > 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
			if (bytes == 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(0);

			const int err = SSL_get_error(s, n);
			if (err == SSL_ERROR_WANT_READ)
			{
				if (auto r = co_await RecvAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
			}
			else if (err == SSL_ERROR_WANT_WRITE)
			{
				if (auto r = co_await SendAwaitable{ socket.Handle(), *engine }; r.IsError()) co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
			}
			else co_return ne::Result<std::size_t, ne::OsError>::Error(SslError("[TlsStream/Receive]"));
		}
	}

	ne::Result<void, ne::OsError> TlsStream::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

		SSL_shutdown(static_cast<SSL*>(ssl));
		SSL_free(static_cast<SSL*>(ssl));
		ssl = nullptr;
		SSL_CTX_free(static_cast<SSL_CTX*>(ctx));
		ctx = nullptr;
		(void)engine->Unwatch(socket.Handle());
		[[maybe_unused]] auto closing = std::move(socket);

		return ne::Result<void, ne::OsError>::Ok();
	}

#else
	static ne::OsError NoTls(string_view_t _ctx)
	{
		auto err = ne::OsError{ 0, "TLS not available (define NEBULA_WITH_OPENSSL on POSIX)" };
		err.Context(_ctx);
		return err;
	}



	TlsStream::TlsStream(Socket&&, IIoEngine&, void*, void*) noexcept {}
	TlsStream::TlsStream(TlsStream&&) noexcept = default;
	TlsStream& TlsStream::operator=(TlsStream&&) noexcept = default;
	TlsStream::~TlsStream() = default;



	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(Socket&&, IIoEngine&, string_view_t, const TlsConfig&)
	{
		co_return ne::Result<TlsStream, ne::OsError>::Error(NoTls("[TlsStream/Connect]"));
	}



	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(std::span<const byte_t>)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoTls("[TlsStream/Send]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(std::span<byte_t>)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoTls("[TlsStream/Receive]"));
	}

	ne::Result<void, ne::OsError> TlsStream::Close()
	{
		return ne::Result<void, ne::OsError>::Ok();
	}

#endif

END_NS
