//
// Created by hscloud on 25. 6. 29.
//

#include "TlsStream.h"
#include "Stream/Awaitable.h"
#include <utility>
#include <cstring>
#include <vector>
#include <fstream>

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



	TlsStream::TlsStream(Socket&& _socket, ne::io::IIoEngine& _engine, void* _credHandle, void* _ctxHandle, void* _messageBuffer, ne::memory::IAllocator* _allocator) noexcept
		: socket(std::move(_socket))
		, engine(&_engine)
		, credHandle(_credHandle)
		, ctxHandle(_ctxHandle)
		, messageBuffer(_messageBuffer)
		, allocator(_allocator) {}

	TlsStream::TlsStream(TlsStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine)
		, sniHost(std::move(_other.sniHost))
		, allocator(std::exchange(_other.allocator, nullptr))
		, credHandle(std::exchange(_other.credHandle, nullptr))
		, ctxHandle(std::exchange(_other.ctxHandle, nullptr))
		, messageBuffer(std::exchange(_other.messageBuffer, nullptr)) {}

	TlsStream& TlsStream::operator=(TlsStream&& _other) noexcept
	{
		if (this != &_other)
		{
			(void)Close();
			socket       = std::move(_other.socket);
			engine       = _other.engine;
			sniHost      = std::move(_other.sniHost);
			allocator    = std::exchange(_other.allocator, nullptr);
			credHandle   = std::exchange(_other.credHandle, nullptr);
			ctxHandle    = std::exchange(_other.ctxHandle, nullptr);
			messageBuffer = std::exchange(_other.messageBuffer, nullptr);
		}

		return *this;
	}

	TlsStream::~TlsStream()
	{
		if (ctxHandle)
		{
			if (const auto* functionTable = SspiWrapper::Get())
			{
				functionTable->DeleteSecurityContext(static_cast<CtxtHandle*>(ctxHandle));
			}
			delete static_cast<CtxtHandle*>(ctxHandle);
		}
		if (credHandle)
		{
			if (const auto* functionTable = SspiWrapper::Get())
			{
				functionTable->FreeCredentialHandle(static_cast<CredHandle*>(credHandle));
			}
			delete static_cast<CredHandle*>(credHandle);
		}

		delete static_cast<TlsMessageBuffer*>(messageBuffer);
	}



	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(Socket&& _socket, ne::io::IIoEngine& _engine, string_view_t _host, const TlsConfig& _config, ne::memory::IAllocator* _allocator)
	{
		const auto* functionTable = SspiWrapper::Get();
		if (!functionTable)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				ne::OsError{ 0, "SChannel: secur32.dll load failed" });

		// ── 자격증명 획득 ──
		SCHANNEL_CRED credData{};
		credData.dwVersion = SCHANNEL_CRED_VERSION;
		credData.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
		credData.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
		if (_config.verifyPeer) credData.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;
		else credData.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_SERVERNAME_CHECK;

		auto tempCredHandle = std::unique_ptr<CredHandle>(new CredHandle{});
		TimeStamp timeLimit{};

		SECURITY_STATUS ss = functionTable->AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &credData, nullptr, nullptr, tempCredHandle.get(), &timeLimit);
		if (ss != SEC_E_OK)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Connect/AcquireCred]"));

		auto tempMessageBuffer = std::unique_ptr<TlsMessageBuffer>(new TlsMessageBuffer(TlsMessageBuffer::Allocate()));

		TlsStream stream(std::move(_socket), _engine, tempCredHandle.release(), nullptr, tempMessageBuffer.release(), _allocator);
		stream.sniHost = string_t(_host);

		if (auto result = co_await stream.Handshake(); result.IsError())
			co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(result.Error()));

		co_return ne::Result<TlsStream, ne::OsError>::Ok(std::move(stream));
	}

	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Accept(Socket&& _socket, ne::io::IIoEngine& _engine, const TlsConfig& _config, ne::memory::IAllocator* _allocator)
	{
		const auto* functionTable = SspiWrapper::Get();
		if (!functionTable)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				ne::OsError{ 0, "SChannel: secur32.dll load failed" });

		std::ifstream pfxFile(_config.certFile, std::ios::binary);
		if (!pfxFile)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				ne::OsError{ 0, "SChannel: failed to open PFX file" });

		std::vector<BYTE> pfxBytes((std::istreambuf_iterator<char>(pfxFile)), std::istreambuf_iterator<char>{});

		CRYPT_DATA_BLOB blob{ static_cast<DWORD>(pfxBytes.size()), pfxBytes.data() };
		HCERTSTORE certStore = ::PFXImportCertStore(&blob, _config.pfxPassword.empty() ? nullptr : StringFormat::UTF8toWCS(_config.pfxPassword.c_str()).c_str(), PKCS12_INCLUDE_EXTENDED_PROPERTIES);
		if (!certStore)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				ne::OsError{ ::GetLastError(), "SChannel: PFXImportCertStore failed" });

		PCCERT_CONTEXT certCtx = ::CertFindCertificateInStore(certStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_HAS_PRIVATE_KEY, nullptr, nullptr);
		if (!certCtx)
		{
			::CertCloseStore(certStore, 0);

			co_return ne::Result<TlsStream, ne::OsError>::Error(
				ne::OsError{ ::GetLastError(), "SChannel: no cert with private key in PFX" });
		}

		SCHANNEL_CRED credData{};
		credData.dwVersion = SCHANNEL_CRED_VERSION;
		credData.cCreds = 1;
		credData.paCred = &certCtx;
		credData.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_3_SERVER;
		credData.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER;

		auto tempCredHandle = std::unique_ptr<CredHandle>(new CredHandle{});

		TimeStamp timeLimit{};
		SECURITY_STATUS ss = functionTable->AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_INBOUND, nullptr, &credData, nullptr, nullptr, tempCredHandle.get(), &timeLimit);

		::CertFreeCertificateContext(certCtx);
		::CertCloseStore(certStore, 0);

		if (ss != SEC_E_OK)
			co_return ne::Result<TlsStream, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Accept/AcquireCred]"));

		auto tempMessageBuffer = std::unique_ptr<TlsMessageBuffer>(new TlsMessageBuffer(TlsMessageBuffer::Allocate()));
		TlsStream stream(std::move(_socket), _engine, tempCredHandle.release(), nullptr, tempMessageBuffer.release(), _allocator);

		auto tempCtxHandle = std::unique_ptr<CtxtHandle>(new CtxtHandle{});
		auto* nativeCredHandle = static_cast<CredHandle*>(stream.credHandle);
		auto* nativeMessageBuffer = static_cast<TlsMessageBuffer*>(stream.messageBuffer);
		auto span = nativeMessageBuffer->GetBuffer();
		bool_t isFirstCall = true;
		std::size_t dataInBuffer = 0;

		while (true)
		{
			if (dataInBuffer >= span.size())
			{
				nativeMessageBuffer->Resize(span.size() * 2);
				span = nativeMessageBuffer->GetBuffer();
			}

			if (auto result = co_await ne::io::RecvAwaitable{ stream.socket.Handle(), _engine }; result.IsError())
				co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(result.Error()));

			const int received = ::recv(stream.socket.Handle(), reinterpret_cast<char*>(span.data() + dataInBuffer), static_cast<int>(span.size() - dataInBuffer), 0);
			if (received <= 0)
				co_return ne::Result<TlsStream, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Accept/Recv]"));

			dataInBuffer += static_cast<std::size_t>(received);

			std::array<SecBuffer, 2> inBuffers{};
			inBuffers[0] = { static_cast<ULONG>(dataInBuffer), SECBUFFER_TOKEN, span.data() };
			inBuffers[1] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc inDesc = { SECBUFFER_VERSION, 2, inBuffers.data() };

			std::array<SecBuffer, 2> outBuffers{};
			outBuffers[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBuffers[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBuffers.data() };

			ULONG retFlags = 0;
			ss = functionTable->AcceptSecurityContext(
				nativeCredHandle,
				isFirstCall ? nullptr : tempCtxHandle.get(),
				&inDesc,
				ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
				SECURITY_NATIVE_DREP,
				isFirstCall ? tempCtxHandle.get() : nullptr,
				&outDesc,
				&retFlags,
				nullptr);
			isFirstCall = false;

			if (ss == SEC_I_COMPLETE_AND_CONTINUE || ss == SEC_I_COMPLETE_NEEDED)
			{
				functionTable->CompleteAuthToken(tempCtxHandle.get(), &outDesc);
				ss = (ss == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
			}

			// 잔여 데이터 보존(EXTRA) 또는 폐기
			if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0)
			{
				std::memmove(span.data(), span.data() + (dataInBuffer - inBuffers[1].cbBuffer), inBuffers[1].cbBuffer);
				dataInBuffer = inBuffers[1].cbBuffer;
			}
			else if (ss != SEC_E_INCOMPLETE_MESSAGE) dataInBuffer = 0;

			// 서버 토큰 전송
			if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0)
			{
				if (auto r = co_await ne::io::SendAwaitable{ stream.socket.Handle(), _engine }; r.IsError())
				{
					functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
					co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(r.Error()));
				}
				const int sent = ::send(stream.socket.Handle(), static_cast<const char*>(outBuffers[0].pvBuffer), static_cast<int>(outBuffers[0].cbBuffer), 0);
				functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
				if (sent < 0)
					co_return ne::Result<TlsStream, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Accept/Send]"));
			}

			if (ss == SEC_E_OK) break;
			if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) continue;

			co_return ne::Result<TlsStream, ne::OsError>::Error(SchannelError(ss, "[TlsStream/Accept]"));
		}

		stream.ctxHandle = tempCtxHandle.release();

		co_return ne::Result<TlsStream, ne::OsError>::Ok(std::move(stream));
	}


	ne::Task<ne::Result<void, ne::OsError>> TlsStream::Handshake()
	{
		const auto* functionTable = SspiWrapper::Get();
		if (!functionTable) co_return ne::Result<void, ne::OsError>::Error(ne::OsError{ 0, "SChannel: secur32.dll load failed" });

		auto whost = ne::StringFormat::UTF8toWCS(sniHost.c_str());
		const lpwstr_t host = whost.empty() ? nullptr : whost.data();

		auto rawCtx = std::unique_ptr<CtxtHandle>(new CtxtHandle{});
		auto* rawCred = static_cast<CredHandle*>(credHandle);
		auto* rawBuffer = static_cast<TlsMessageBuffer*>(messageBuffer);
		auto span = rawBuffer->GetBuffer();
		bool_t isFirstCall = true;
		std::size_t dataInBuffer = 0;

		while (true)
		{
			std::array<SecBuffer, 2> inBuffers{};
			SecBufferDesc inDesc{};
			PSecBufferDesc pInDesc = nullptr;

			if (!isFirstCall)
			{
				inBuffers[0] = { static_cast<ULONG>(dataInBuffer), SECBUFFER_TOKEN, span.data() };
				inBuffers[1] = { 0, SECBUFFER_EMPTY, nullptr };
				inDesc = { SECBUFFER_VERSION, 2, inBuffers.data() };
				pInDesc = &inDesc;
			}

			std::array<SecBuffer, 2> outBuffers{};
			outBuffers[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBuffers[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBuffers.data() };

			ULONG retFlags = 0;
			SECURITY_STATUS ss = functionTable->InitializeSecurityContextW(
				rawCred,
				isFirstCall ? nullptr : rawCtx.get(),
				host,
				ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
				0,
				0,
				pInDesc,
				0,
				isFirstCall ? rawCtx.get() : nullptr,
				&outDesc,
				&retFlags,
				nullptr);

			if (ss == SEC_I_COMPLETE_AND_CONTINUE || ss == SEC_I_COMPLETE_NEEDED)
			{
				functionTable->CompleteAuthToken(rawCtx.get(), &outDesc);
				ss = (ss == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
			}

			if (!isFirstCall && inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0)
			{
				std::memmove(span.data(), span.data() + (dataInBuffer - inBuffers[1].cbBuffer), inBuffers[1].cbBuffer);
				dataInBuffer = inBuffers[1].cbBuffer;
			}
			else if (!isFirstCall)
			{
				dataInBuffer = 0;
			}

			if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0)
			{
				if (auto result = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; result.IsError())
				{
					functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
					co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
				}

				const int bytes = ::send(socket.Handle(), static_cast<const char*>(outBuffers[0].pvBuffer), static_cast<int>(outBuffers[0].cbBuffer), 0);
				functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
				if (bytes < 0)
					co_return ne::Result<void, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Handshake/Send]"));
			}

			if (ss == SEC_E_OK) break;

			if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE)
			{
				if (dataInBuffer >= span.size())
				{
					rawBuffer->Resize(span.size() * 2);
					span = rawBuffer->GetBuffer();
				}

				if (auto result = co_await ne::io::RecvAwaitable{ socket.Handle(), *engine }; result.IsError()) co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));

				const int bytes = ::recv(socket.Handle(), reinterpret_cast<char*>(span.data() + dataInBuffer), static_cast<int>(span.size() - dataInBuffer), 0);
				if (bytes <= 0)
					co_return ne::Result<void, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Handshake/Recv]"));

				dataInBuffer += static_cast<std::size_t>(bytes);
				isFirstCall = false;
				continue;
			}

			co_return ne::Result<void, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Handshake]"));
		}

		ctxHandle = rawCtx.release();

		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		const auto* functionTable = SspiWrapper::Get();
		auto* nativeCtxHandle = static_cast<CtxtHandle*>(ctxHandle);

		SecPkgContext_StreamSizes sizes{};
		SECURITY_STATUS ss = functionTable->QueryContextAttributesW(nativeCtxHandle, SECPKG_ATTR_STREAM_SIZES, &sizes);
		if (ss != SEC_E_OK)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Send/QueryAttr]"));

		const auto dataSpan = _data.Span();
		std::size_t totalSent = 0;
		const std::size_t maxMsg = sizes.cbMaximumMessage;

		while (totalSent < dataSpan.size())
		{
			const std::size_t chunk = std::min(dataSpan.size() - totalSent, maxMsg);
			std::vector<byte_t> encodeBuffer(sizes.cbHeader + chunk + sizes.cbTrailer);

			std::memcpy(encodeBuffer.data() + sizes.cbHeader, dataSpan.data() + totalSent, chunk);

			std::array<SecBuffer, 4> buffers{};
			buffers[0] = { sizes.cbHeader, SECBUFFER_STREAM_HEADER, encodeBuffer.data() };
			buffers[1] = { static_cast<ULONG>(chunk), SECBUFFER_DATA, encodeBuffer.data() + sizes.cbHeader };
			buffers[2] = { sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, encodeBuffer.data() + sizes.cbHeader + chunk };
			buffers[3] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers.data() };

			ss = functionTable->EncryptMessage(nativeCtxHandle, 0, &desc, 0);
			if (ss != SEC_E_OK)
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					SchannelError(ss, "[TlsStream/Send/Encrypt]"));

			const ULONG encodeSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;

			if (auto result = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; result.IsError())
				co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));

			const int bytes = ::send(socket.Handle(), reinterpret_cast<const char*>(encodeBuffer.data()), static_cast<int>(encodeSize), 0);
			if (bytes < 0)
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Send]"));

			totalSent += chunk;
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(totalSent);
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Sendv(const BufferChain& _chain)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });
		if (!allocator) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "no allocator for TlsStream::Sendv" });

		const auto flat = _chain.Flatten(*allocator);
		if (!flat.IsValid())
			co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "BufferChain::Flatten failed" });

		auto result = co_await Send(flat);
		flat.owner->Release();

		co_return result;
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		const auto* functionTable = SspiWrapper::Get();
		auto* nativeCtxHandle = static_cast<CtxtHandle*>(ctxHandle);
		auto* nativeMessageBuffer = static_cast<TlsMessageBuffer*>(messageBuffer);
		auto span = nativeMessageBuffer->GetBuffer();
		std::size_t dataInBuffer = 0;

		if (!nativeMessageBuffer->data.empty())
		{
			dataInBuffer = nativeMessageBuffer->data.size();
			std::memmove(span.data(), nativeMessageBuffer->data.data(), dataInBuffer);
			nativeMessageBuffer->data = {};
		}

		while (true)
		{
			if (dataInBuffer == 0)
			{
				if (auto result = co_await ne::io::RecvAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));

				const int bytes = ::recv(socket.Handle(), reinterpret_cast<char*>(span.data()), static_cast<int>(span.size()), 0);
				if (bytes <= 0)
					co_return ne::Result<std::size_t, ne::OsError>::Ok(0);

				dataInBuffer = static_cast<std::size_t>(bytes);
			}

			std::array<SecBuffer, 4> buffers{};
			buffers[0] = { static_cast<ULONG>(dataInBuffer), SECBUFFER_DATA, span.data() };
			buffers[1] = { 0, SECBUFFER_EMPTY, nullptr };
			buffers[2] = { 0, SECBUFFER_EMPTY, nullptr };
			buffers[3] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers.data() };

			SECURITY_STATUS ss = functionTable->DecryptMessage(nativeCtxHandle, &desc, 0, nullptr);
			if (ss == SEC_E_OK)
			{
				for (int i = 0; i < 4; ++i)
				{
					if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].pvBuffer)
					{
						const std::size_t plainLength = std::min<std::size_t>(buffers[i].cbBuffer, _data.length);
						std::memcpy(_data.ptr, buffers[i].pvBuffer, plainLength);

						for (int j = 0; j < 4; ++j)
						{
							if (buffers[j].BufferType == SECBUFFER_EXTRA && buffers[j].pvBuffer && buffers[j].cbBuffer > 0)
							{
								const auto* p = static_cast<const byte_t*>(buffers[j].pvBuffer);
								nativeMessageBuffer->data = span.subspan(static_cast<std::size_t>(p - span.data()), buffers[j].cbBuffer);
							}
						}

						co_return ne::Result<std::size_t, ne::OsError>::Ok(plainLength);
					}
				}

				dataInBuffer = 0;
				continue;
			}

			if (ss == SEC_I_CONTEXT_EXPIRED)
				co_return ne::Result<std::size_t, ne::OsError>::Ok(0);

			if (ss == SEC_E_INCOMPLETE_MESSAGE)
			{
				if (dataInBuffer >= span.size())
				{
					nativeMessageBuffer->Resize(span.size() * 2);
					span = nativeMessageBuffer->GetBuffer();
				}

				if (auto result = co_await ne::io::RecvAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));

				const int bytes = ::recv(socket.Handle(), reinterpret_cast<char*>(span.data() + dataInBuffer), static_cast<int>(span.size() - dataInBuffer), 0);
				if (bytes <= 0)
					co_return ne::Result<std::size_t, ne::OsError>::Error(
						ne::OsError{ ne::LastOsError() }.Context("[TlsStream/Receive/Recv]"));

				dataInBuffer += static_cast<std::size_t>(bytes);
				continue;
			}

			co_return ne::Result<std::size_t, ne::OsError>::Error(
				SchannelError(ss, "[TlsStream/Receive/Decrypt]"));
		}
	}

	ne::Task<ne::Result<void, ne::OsError>> TlsStream::Shutdown()
	{
		(void)Close();
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> TlsStream::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

		const auto* functionTable = SspiWrapper::Get();
		auto* rawCred = static_cast<CredHandle*>(credHandle);
		auto* rawCtx = static_cast<CtxtHandle*>(ctxHandle);

		if (functionTable)
		{
			DWORD type = SCHANNEL_SHUTDOWN;
			SecBuffer shutdown = { sizeof(type), SECBUFFER_TOKEN, &type };
			SecBufferDesc shutdownDesc = { SECBUFFER_VERSION, 1, &shutdown };
			functionTable->ApplyControlToken(rawCtx, &shutdownDesc);

			std::array<SecBuffer, 2> outBuffers{};
			outBuffers[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBuffers[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBuffers.data() };

			ULONG retFlags = 0;
			functionTable->InitializeSecurityContextW(
				rawCred,
				rawCtx,
				nullptr,
				ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
				0,
				0,
				nullptr,
				0, nullptr,
				&outDesc,
				&retFlags,
				nullptr);

			if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0)
			{
				::send(socket.Handle(), static_cast<const char*>(outBuffers[0].pvBuffer), static_cast<int>(outBuffers[0].cbBuffer), 0);
				functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
			}

			functionTable->DeleteSecurityContext(rawCtx);
			functionTable->FreeCredentialHandle(rawCred);
		}

		delete static_cast<CtxtHandle*>(ctxHandle);
		delete static_cast<CredHandle*>(credHandle);
		delete static_cast<TlsMessageBuffer*>(messageBuffer);
		ctxHandle = credHandle = messageBuffer = nullptr;

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



	TlsStream::TlsStream(Socket&& _socket, ne::io::IIoEngine& _engine, void* _ctx, void* _ssl, ne::memory::IAllocator* _allocator) noexcept
		: socket(std::move(_socket))
		, engine(&_engine)
		, allocator(_allocator)
		, ctx(_ctx)
		, ssl(_ssl) {}

	TlsStream::TlsStream(TlsStream&& _other) noexcept
		: socket(std::move(_other.socket))
		, engine(_other.engine)
		, sniHost(std::move(_other.sniHost))
		, allocator(std::exchange(_other.allocator, nullptr))
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

			if (ctx)
			{
				SSL_CTX_free(static_cast<SSL_CTX*>(ctx));
			}

			socket    = std::move(_other.socket);
			engine    = _other.engine;
			sniHost   = std::move(_other.sniHost);
			allocator = std::exchange(_other.allocator, nullptr);
			ctx       = std::exchange(_other.ctx, nullptr);
			ssl       = std::exchange(_other.ssl, nullptr);
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

		if (ctx)
		{
			SSL_CTX_free(static_cast<SSL_CTX*>(ctx));
		}
	}



	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(Socket&& _socket, ne::io::IIoEngine& _engine, string_view_t _host, const TlsConfig& _config, ne::memory::IAllocator* _allocator)
	{
		SSL_CTX* sslCtx = SSL_CTX_new(TLS_client_method());
		if (!sslCtx)
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect]"));

		SSL_CTX_set_verify(sslCtx, _config.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);

		if (!_config.caFile.empty() && SSL_CTX_load_verify_locations(sslCtx, _config.caFile.c_str(), nullptr) != 1)
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

		SSL* tempSsl = SSL_new(sslCtx);
		if (!tempSsl)
		{
			SSL_CTX_free(sslCtx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Connect/SSL]"));
		}

		SSL_set_fd(tempSsl, static_cast<int>(_socket.Handle()));
		if (!_host.empty()) SSL_set_tlsext_host_name(tempSsl, _host.data());

		TlsStream stream(std::move(_socket), _engine, sslCtx, tempSsl, _allocator);
		stream.sniHost = string_t(_host);

		auto result = co_await stream.Handshake();
		if (result.IsError())
			co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(result.Error()));

		co_return ne::Result<TlsStream, ne::OsError>::Ok(std::move(stream));
	}

	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Accept(Socket&& _socket, ne::io::IIoEngine& _engine, const TlsConfig& _config, ne::memory::IAllocator* _allocator)
	{
		SSL_CTX* sslCtx = SSL_CTX_new(TLS_server_method());
		if (!sslCtx)
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Accept]"));

		SSL_CTX_set_verify(sslCtx, _config.verifyPeer ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT : SSL_VERIFY_NONE, nullptr);
		if (!_config.caFile.empty() && SSL_CTX_load_verify_locations(sslCtx, _config.caFile.c_str(), nullptr) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Accept/CA]"));
		}
		if (SSL_CTX_use_certificate_file(sslCtx, _config.certFile.c_str(), SSL_FILETYPE_PEM) != 1 || SSL_CTX_use_PrivateKey_file(sslCtx, _config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Accept/Cert]"));
		}
	
		SSL* tempSsl = SSL_new(sslCtx);
		if (!tempSsl)
		{
			SSL_CTX_free(sslCtx);
			co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Accept/SSL]"));
		}
		SSL_set_fd(tempSsl, static_cast<int>(_socket.Handle()));

		TlsStream stream(std::move(_socket), _engine, sslCtx, tempSsl, _allocator);
		while (true)
		{
			const int sslResult = SSL_accept(tempSsl);
			if (sslResult == 1) break;

			const int sslError = SSL_get_error(tempSsl, sslResult);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await ne::io::RecvAwaitable{ stream.socket.Handle(), _engine }; result.IsError())
					co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await ne::io::SendAwaitable{ stream.socket.Handle(), _engine }; result.IsError())
					co_return ne::Result<TlsStream, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<TlsStream, ne::OsError>::Error(SslError("[TlsStream/Accept/Handshake]"));
			}
		}

		co_return ne::Result<TlsStream, ne::OsError>::Ok(std::move(stream));
	}



	ne::Task<ne::Result<void, ne::OsError>> TlsStream::Handshake()
	{
		auto* nativeSsl = static_cast<SSL*>(ssl);
		while (true)
		{
			const int sslResult = SSL_connect(nativeSsl);
			if (sslResult == 1) break;

			const int sslError = SSL_get_error(nativeSsl, sslResult);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await ne::io::RecvAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<void, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<void, ne::OsError>::Error(SslError("[TlsStream/Handshake]"));
			}
		}

		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

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
				if (auto result = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await ne::io::RecvAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(SslError("[TlsStream/Send]"));
			}
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(sent);
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Sendv(const BufferChain& _chain)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });
		if (!allocator) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "no allocator for TlsStream::Sendv" });

		const auto flat = _chain.Flatten(*allocator);
		if (!flat.IsValid())
			co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "BufferChain::Flatten failed" });

		auto result = co_await Send(flat);
		flat.owner->Release();

		co_return result;
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(BufferView _data)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "TLS stream closed" });

		auto* nativeSsl = static_cast<SSL*>(ssl);
		while (true)
		{
			const int bytes = SSL_read(nativeSsl, _data.ptr, static_cast<int>(_data.length));
			if (bytes > 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
			if (bytes == 0) co_return ne::Result<std::size_t, ne::OsError>::Ok(0);

			const int sslError = SSL_get_error(nativeSsl, bytes);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await ne::io::RecvAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await ne::io::SendAwaitable{ socket.Handle(), *engine }; result.IsError())
					co_return ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(SslError("[TlsStream/Receive]"));
			}
		}
	}

	ne::Task<ne::Result<void, ne::OsError>> TlsStream::Shutdown()
	{
		(void)Close();
		co_return ne::Result<void, ne::OsError>::Ok();
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



	TlsStream::TlsStream(Socket&&, ne::io::IIoEngine&, void*, void*, ne::memory::IAllocator*) noexcept {}
	TlsStream::TlsStream(TlsStream&&) noexcept = default;
	TlsStream& TlsStream::operator=(TlsStream&&) noexcept = default;
	TlsStream::~TlsStream() = default;



	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Connect(Socket&&, ne::io::IIoEngine&, string_view_t, const TlsConfig&, ne::memory::IAllocator*)
	{
		co_return ne::Result<TlsStream, ne::OsError>::Error(NoTls("[TlsStream/Connect]"));
	}

	ne::Task<ne::Result<TlsStream, ne::OsError>> TlsStream::Accept(Socket&&, ne::io::IIoEngine&, const TlsConfig&, ne::memory::IAllocator*)
	{
		co_return ne::Result<TlsStream, ne::OsError>::Error(NoTls("[TlsStream/Accept]"));
	}



	ne::Task<ne::Result<void, ne::OsError>> TlsStream::Handshake()
	{
		co_return ne::Result<void, ne::OsError>::Error(NoTls("[TlsStream/Handshake]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Send(BufferView)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoTls("[TlsStream/Send]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Receive(BufferView)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoTls("[TlsStream/Receive]"));
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> TlsStream::Sendv(const BufferChain&)
	{
		co_return ne::Result<std::size_t, ne::OsError>::Error(NoTls("[TlsStream/Sendv]"));
	}

	ne::Task<ne::Result<void, ne::OsError>> TlsStream::Shutdown()
	{
		co_return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> TlsStream::Close()
	{
		return ne::Result<void, ne::OsError>::Ok();
	}

#endif

END_NS
