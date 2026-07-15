//
// Created by hscloud on 25. 6. 29.
//

#include "Network/Stream/Tls/TlsStream.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

#if defined(_WIN32)
#   include "Network/Stream/Tls/Schannel/SspiWrapper.h"
#   include "Network/Stream/Tls/Schannel/TlsMessageBuffer.h"
#   include "Util/StringFormat.h"
#   include <winsock2.h>
#elif defined(NEBULA_WITH_OPENSSL)
#   include <openssl/err.h>
#   include <openssl/ssl.h>
#   include <sys/socket.h>
#endif



namespace
{
#if defined(_WIN32) || defined(NEBULA_WITH_OPENSSL)
	// Schannel/OpenSSL 둘 다 "동기 send/recv + WANT_READ/WANT_WRITE" 스타일 라이브러리라, PlainStream 의
	// completion 기반 Send/Receive 대신 raw 소켓 핸들 + readiness 대기(PlainStream::WaitReadable/
	// WaitWritable)로 직접 구동한다 — SshStream(libssh2) 과 동일한 통합 패턴.
	ne::Task<ne::io::IoResult<ne::void_t>> SendAll(ne::network::PlainStream& _transport, const ne::byte_t* _data, const std::size_t _length, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<ne::void_t>;
		std::size_t sent = 0;
		while (sent < _length)
		{
			if (auto waited = co_await _transport.WaitWritable(_stopToken); waited.IsError())
				co_return R::Error(std::move(waited.Error()).Context("[TlsStream/SendAll]"));

			const int bytes = ::send(_transport.Handle(), reinterpret_cast<const char*>(_data + sent), static_cast<int>(_length - sent), 0);
			if (bytes <= 0)
				co_return R::Error(ne::io::IoError{ ne::OsError{ ne::LastOsError() } }.Context("[TlsStream/SendAll]"));

			sent += static_cast<std::size_t>(bytes);
		}

		co_return R::Ok();
	}

	ne::Task<ne::io::IoResult<std::size_t>> RecvSome(ne::network::PlainStream& _transport, ne::byte_t* _data, const std::size_t _capacity, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;
		if (auto waited = co_await _transport.WaitReadable(_stopToken); waited.IsError())
			co_return R::Error(std::move(waited.Error()).Context("[TlsStream/RecvSome]"));

		const int bytes = ::recv(_transport.Handle(), reinterpret_cast<char*>(_data), static_cast<int>(_capacity), 0);
		if (bytes < 0)
			co_return R::Error(ne::io::IoError{ ne::OsError{ ne::LastOsError() } }.Context("[TlsStream/RecvSome]"));

		co_return R::Ok(static_cast<std::size_t>(bytes)); // 0 == 상대가 send 방향을 닫음(EOF)
	}
#endif

#if defined(_WIN32)
	// SEC_APPLICATION_PROTOCOLS { DWORD ProtocolListsSize; SEC_APPLICATION_PROTOCOL_LIST ProtocolLists[1]; }
	// 를 SECBUFFER_APPLICATION_PROTOCOLS 입력 버퍼용으로 직렬화. 구조체를 통째로 memcpy 하면 컴파일러
	// 패딩에 따라 레이아웃이 어긋날 수 있어(특히 4바이트 enum + 2바이트 WORD 경계) 필드별로 직접 채운다.
	static std::vector<ne::byte_t> BuildAlpnBuffer(const std::vector<ne::string_t>& _protocols)
	{
		std::vector<ne::byte_t> protocolList;
		for (const auto& protocol : _protocols)
		{
			protocolList.push_back(static_cast<ne::byte_t>(protocol.size()));
			protocolList.insert(protocolList.end(), protocol.begin(), protocol.end());
		}

		const ne::ulong_t protoNegoExt = SecApplicationProtocolNegotiationExt_ALPN;
		const ne::ushort_t protocolListSize = static_cast<ne::ushort_t>(protocolList.size());
		const ne::ulong_t protocolListsSize = sizeof(protoNegoExt) + sizeof(protocolListSize) + protocolList.size();

		std::vector<ne::byte_t> buffer;
		buffer.reserve(sizeof(DWORD) + protocolListsSize);

		auto appendBytes = [&buffer](const void* _ptr, const std::size_t _size)
		{
			const auto* bytes = static_cast<const ne::byte_t*>(_ptr);
			buffer.insert(buffer.end(), bytes, bytes + _size);
		};

		appendBytes(&protocolListsSize, sizeof(protocolListsSize));
		appendBytes(&protoNegoExt, sizeof(protoNegoExt));
		appendBytes(&protocolListSize, sizeof(protocolListSize));
		appendBytes(protocolList.data(), protocolList.size());

		return buffer;
	}
#endif
}



BEGIN_NS(ne::network)

#if defined(_WIN32)
	static ne::io::IoError SchannelError(const SECURITY_STATUS _ss, const string_view_t _ctx) { return ne::io::IoError{ ne::OsError{ static_cast<ne::ulong_t>(_ss), "SChannel error" } }.Context(_ctx); }




	TlsStream::~TlsStream()
	{
		if (ctxHandle)
		{
			if (const auto* functionTable = SspiWrapper::Get()) functionTable->DeleteSecurityContext(static_cast<CtxtHandle*>(ctxHandle));
			delete static_cast<CtxtHandle*>(ctxHandle);
		}
		if (credHandle)
		{
			if (const auto* functionTable = SspiWrapper::Get()) functionTable->FreeCredentialHandle(static_cast<CredHandle*>(credHandle));
			delete static_cast<CredHandle*>(credHandle);
		}

		delete static_cast<TlsMessageBuffer*>(messageBuffer);
	}

	TlsStream& TlsStream::operator=(TlsStream&& _other) noexcept
	{
		if (this != &_other)
		{
			(void_t)Close();
			transport = std::move(_other.transport);
			sniHost = std::move(_other.sniHost);
			alpnCandidates = std::move(_other.alpnCandidates);
			negotiatedProtocol = std::move(_other.negotiatedProtocol);
			allocator = _other.allocator;
			credHandle = std::exchange(_other.credHandle, nullptr);
			ctxHandle = std::exchange(_other.ctxHandle, nullptr);
			messageBuffer = std::exchange(_other.messageBuffer, nullptr);
		}

		return *this;
	}



	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Connect(ne::io::Socket&& _socket, ne::io::Context& _context, const string_view_t _host, const TlsConfig& _config, std::stop_token _stopToken, ne::memory::IAllocator* _allocator)
	{
		using R = ne::io::IoResult<TlsStream>;

		const auto* functionTable = SspiWrapper::Get();
		if (!functionTable)
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: secur32.dll load failed" }.Context("[TlsStream/Connect]"));

		SCHANNEL_CRED credData{};
		credData.dwVersion = SCHANNEL_CRED_VERSION;
		credData.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
		credData.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
		if (_config.verifyPeer) credData.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;
		else credData.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_SERVERNAME_CHECK;

		auto tempCredHandle = std::unique_ptr<CredHandle>(new CredHandle{});
		TimeStamp timeLimit{};
		SECURITY_STATUS ss = functionTable->AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &credData, nullptr, nullptr, tempCredHandle.get(), &timeLimit);
		if (ss != SEC_E_OK) co_return R::Error(SchannelError(ss, "[TlsStream/Connect/AcquireCred]"));

		auto plainStream = PlainStream::Create(std::move(_socket), _context, _allocator);
		if (plainStream.IsError()) co_return R::Error(std::move(plainStream.Error()).Context("[TlsStream/Connect]"));

		TlsStream stream(std::move(plainStream.Value()), tempCredHandle.release(), nullptr, new TlsMessageBuffer(TlsMessageBuffer::Allocate()), _allocator);
		stream.sniHost = string_t(_host);
		stream.alpnCandidates = _config.alpnProtocols;

		if (auto result = co_await stream.Handshake(_stopToken); result.IsError()) co_return R::Error(std::move(result.Error()));

		co_return R::Ok(std::move(stream));
	}

	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Accept(ne::io::Socket&& _socket, ne::io::Context& _context, const TlsConfig& _config, std::stop_token _stopToken, ne::memory::IAllocator* _allocator)
	{
		using R = ne::io::IoResult<TlsStream>;

		const auto* functionTable = SspiWrapper::Get();
		if (!functionTable)
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: secur32.dll load failed" }.Context("[TlsStream/Accept]"));

		std::ifstream pfxFile(_config.certFile, std::ios::binary);
		if (!pfxFile) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: failed to open PFX file" }.Context("[TlsStream/Accept]"));

		std::vector<BYTE> pfxBytes((std::istreambuf_iterator<char>(pfxFile)), std::istreambuf_iterator<char>{});

		CRYPT_DATA_BLOB blob{ static_cast<DWORD>(pfxBytes.size()), pfxBytes.data() };
		HCERTSTORE certStore = ::PFXImportCertStore(&blob, _config.pfxPassword.empty() ? nullptr : ne::StringFormat::UTF8toWCS(_config.pfxPassword.c_str()).c_str(), PKCS12_INCLUDE_EXTENDED_PROPERTIES);
		if (!certStore) co_return R::Error(ne::io::IoError{ ne::OsError{ ::GetLastError() } }.Context("[TlsStream/Accept/PFX]"));

		PCCERT_CONTEXT certCtx = ::CertFindCertificateInStore(certStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_HAS_PRIVATE_KEY, nullptr, nullptr);
		if (!certCtx)
		{
			::CertCloseStore(certStore, 0);
			co_return R::Error(ne::io::IoError{ ne::OsError{ ::GetLastError() } }.Context("[TlsStream/Accept/Cert]"));
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

		if (ss != SEC_E_OK) co_return R::Error(SchannelError(ss, "[TlsStream/Accept/AcquireCred]"));

		auto plainStream = PlainStream::Create(std::move(_socket), _context, _allocator);
		if (plainStream.IsError()) co_return R::Error(std::move(plainStream.Error()).Context("[TlsStream/Accept]"));

		TlsStream stream(std::move(plainStream.Value()), tempCredHandle.release(), nullptr, new TlsMessageBuffer(TlsMessageBuffer::Allocate()), _allocator);
		stream.alpnCandidates = _config.alpnProtocols;

		auto* nativeCred = static_cast<CredHandle*>(stream.credHandle);
		auto* nativeBuffer = static_cast<TlsMessageBuffer*>(stream.messageBuffer);
		auto span = nativeBuffer->GetBuffer();
		auto tempCtxHandle = std::unique_ptr<CtxtHandle>(new CtxtHandle{});
		bool_t isFirstCall = true;
		std::size_t dataInBuffer = 0;

		std::vector<byte_t> alpnBuffer;
		if (!stream.alpnCandidates.empty()) alpnBuffer = BuildAlpnBuffer(stream.alpnCandidates);

		while (true)
		{
			if (dataInBuffer >= span.size())
			{
				nativeBuffer->Resize(span.size() * 2);
				span = nativeBuffer->GetBuffer();
			}

			auto receiveResult = co_await RecvSome(stream.transport, span.data() + dataInBuffer, span.size() - dataInBuffer, _stopToken);
			if (receiveResult.IsError()) co_return R::Error(std::move(receiveResult.Error()));
			if (receiveResult.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "peer closed during handshake" }.Context("[TlsStream/Accept]"));

			dataInBuffer += receiveResult.Value();

			std::array<SecBuffer, 3> inBuffers{};
			ULONG inBufferCount = 0;
			inBuffers[inBufferCount++] = { static_cast<ULONG>(dataInBuffer), SECBUFFER_TOKEN, span.data() };
			inBuffers[inBufferCount++] = { 0, SECBUFFER_EMPTY, nullptr };

			if (isFirstCall && !alpnBuffer.empty()) inBuffers[inBufferCount++] = { static_cast<ULONG>(alpnBuffer.size()), SECBUFFER_APPLICATION_PROTOCOLS, alpnBuffer.data() };

			SecBufferDesc inDesc = { SECBUFFER_VERSION, inBufferCount, inBuffers.data() };

			std::array<SecBuffer, 2> outBuffers{};
			outBuffers[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBuffers[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBuffers.data() };

			ULONG retFlags = 0;
			ss = functionTable->AcceptSecurityContext(nativeCred, isFirstCall ? nullptr : tempCtxHandle.get(), &inDesc, ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM, SECURITY_NATIVE_DREP, isFirstCall ? tempCtxHandle.get() : nullptr, &outDesc, &retFlags, nullptr);
			isFirstCall = false;

			if (ss == SEC_I_COMPLETE_AND_CONTINUE || ss == SEC_I_COMPLETE_NEEDED)
			{
				functionTable->CompleteAuthToken(tempCtxHandle.get(), &outDesc);
				ss = (ss == SEC_I_COMPLETE_AND_CONTINUE) ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
			}

			if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0)
			{
				std::memmove(span.data(), span.data() + (dataInBuffer - inBuffers[1].cbBuffer), inBuffers[1].cbBuffer);
				dataInBuffer = inBuffers[1].cbBuffer;
			}
			else if (ss != SEC_E_INCOMPLETE_MESSAGE) dataInBuffer = 0;

			if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0)
			{
				auto sendResult = co_await SendAll(stream.transport, static_cast<const byte_t*>(outBuffers[0].pvBuffer), outBuffers[0].cbBuffer, _stopToken);
				functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
				if (sendResult.IsError()) co_return R::Error(std::move(sendResult.Error()));
			}

			if (ss == SEC_E_OK) break;
			if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE) continue;

			co_return R::Error(SchannelError(ss, "[TlsStream/Accept]"));
		}

		stream.ctxHandle = tempCtxHandle.release();

		if (!stream.alpnCandidates.empty())
		{
			SecPkgContext_ApplicationProtocol protoInfo{};
			if (functionTable->QueryContextAttributesW(static_cast<CtxtHandle*>(stream.ctxHandle), SECPKG_ATTR_APPLICATION_PROTOCOL, &protoInfo) == SEC_E_OK && protoInfo.ProtoNegoStatus == SecApplicationProtocolNegotiationStatus_Success) stream.negotiatedProtocol.assign(reinterpret_cast<const char*>(protoInfo.ProtocolId), protoInfo.ProtocolIdSize);
		}

		co_return R::Ok(std::move(stream));
	}



	ne::Task<ne::io::IoResult<void_t>> TlsStream::Handshake(std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<void_t>;

		const auto* functionTable = SspiWrapper::Get();
		if (!functionTable)
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: secur32.dll load failed" }.Context("[TlsStream/Handshake]"));

		auto whost = ne::StringFormat::UTF8toWCS(sniHost.c_str());
		const lpwstr_t host = whost.empty() ? nullptr : whost.data();

		auto rawCtx = std::unique_ptr<CtxtHandle>(new CtxtHandle{});
		auto* rawCred = static_cast<CredHandle*>(credHandle);
		auto* rawBuffer = static_cast<TlsMessageBuffer*>(messageBuffer);
		auto span = rawBuffer->GetBuffer();
		bool_t isFirstCall = true;
		std::size_t dataInBuffer = 0;

		std::vector<byte_t> alpnBuffer;
		if (!alpnCandidates.empty()) alpnBuffer = BuildAlpnBuffer(alpnCandidates);

		while (true)
		{
			std::array<SecBuffer, 2> inBuffers{};
			ULONG inBufferCount = 0;
			SecBufferDesc inDesc{};
			PSecBufferDesc pInDesc = nullptr;

			if (!isFirstCall)
			{
				inBuffers[inBufferCount++] = { static_cast<ULONG>(dataInBuffer), SECBUFFER_TOKEN, span.data() };
				inBuffers[inBufferCount++] = { 0, SECBUFFER_EMPTY, nullptr };
				inDesc = { SECBUFFER_VERSION, inBufferCount, inBuffers.data() };
				pInDesc = &inDesc;
			}
			else if (!alpnBuffer.empty())
			{
				inBuffers[inBufferCount++] = { static_cast<ULONG>(alpnBuffer.size()), SECBUFFER_APPLICATION_PROTOCOLS, alpnBuffer.data() };
				inDesc = { SECBUFFER_VERSION, inBufferCount, inBuffers.data() };
				pInDesc = &inDesc;
			}

			std::array<SecBuffer, 2> outBuffers{};
			outBuffers[0] = { 0, SECBUFFER_TOKEN, nullptr };
			outBuffers[1] = { 0, SECBUFFER_ALERT, nullptr };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 2, outBuffers.data() };

			ULONG retFlags = 0;
			SECURITY_STATUS ss = functionTable->InitializeSecurityContextW(rawCred, isFirstCall ? nullptr : rawCtx.get(), host, ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM, 0, 0, pInDesc, 0, isFirstCall ? rawCtx.get() : nullptr, &outDesc, &retFlags, nullptr);

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
			else if (!isFirstCall) dataInBuffer = 0;

			if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0)
			{
				auto sendResult = co_await SendAll(transport, static_cast<const byte_t*>(outBuffers[0].pvBuffer), outBuffers[0].cbBuffer, _stopToken);
				functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
				if (sendResult.IsError()) co_return R::Error(std::move(sendResult.Error()));
			}

			if (ss == SEC_E_OK) break;

			if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_E_INCOMPLETE_MESSAGE)
			{
				if (dataInBuffer >= span.size())
				{
					rawBuffer->Resize(span.size() * 2);
					span = rawBuffer->GetBuffer();
				}

				auto receiveResult = co_await RecvSome(transport, span.data() + dataInBuffer, span.size() - dataInBuffer, _stopToken);
				if (receiveResult.IsError()) co_return R::Error(std::move(receiveResult.Error()));
				if (receiveResult.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "peer closed during handshake" }.Context("[TlsStream/Handshake]"));

				dataInBuffer += receiveResult.Value();
				isFirstCall = false;
				continue;
			}

			co_return R::Error(SchannelError(ss, "[TlsStream/Handshake]"));
		}

		ctxHandle = rawCtx.release();

		if (!alpnCandidates.empty())
		{
			SecPkgContext_ApplicationProtocol protoInfo{};
			if (functionTable->QueryContextAttributesW(static_cast<CtxtHandle*>(ctxHandle), SECPKG_ATTR_APPLICATION_PROTOCOL, &protoInfo) == SEC_E_OK && protoInfo.ProtoNegoStatus == SecApplicationProtocolNegotiationStatus_Success) negotiatedProtocol.assign(reinterpret_cast<const char*>(protoInfo.ProtocolId), protoInfo.ProtocolIdSize);
		}

		co_return R::Ok();
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Send(const ne::io::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen())
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Send]"));

		const auto* functionTable = SspiWrapper::Get();
		auto* nativeCtxHandle = static_cast<CtxtHandle*>(ctxHandle);

		SecPkgContext_StreamSizes sizes{};
		SECURITY_STATUS ss = functionTable->QueryContextAttributesW(nativeCtxHandle, SECPKG_ATTR_STREAM_SIZES, &sizes);
		if (ss != SEC_E_OK) co_return R::Error(SchannelError(ss, "[TlsStream/Send/QueryAttr]"));

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
			if (ss != SEC_E_OK) co_return R::Error(SchannelError(ss, "[TlsStream/Send/Encrypt]"));

			const ULONG encodeSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;

			auto sendResult = co_await SendAll(transport, encodeBuffer.data(), encodeSize, _stopToken);
			if (sendResult.IsError()) co_return R::Error(std::move(sendResult.Error()));

			totalSent += chunk;
		}

		co_return R::Ok(totalSent);
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receive(const ne::io::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen())
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Receive]"));

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
				auto receiveResult = co_await RecvSome(transport, span.data(), span.size(), _stopToken);
				if (receiveResult.IsError()) co_return R::Error(std::move(receiveResult.Error()));
				if (receiveResult.Value() == 0) co_return R::Ok(0);

				dataInBuffer = receiveResult.Value();
			}

			std::array<SecBuffer, 4> buffers{};
			buffers[0] = { static_cast<ULONG>(dataInBuffer), SECBUFFER_DATA, span.data() };
			buffers[1] = { 0, SECBUFFER_EMPTY, nullptr };
			buffers[2] = { 0, SECBUFFER_EMPTY, nullptr };
			buffers[3] = { 0, SECBUFFER_EMPTY, nullptr };
			SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers.data() };

			const SECURITY_STATUS ss = functionTable->DecryptMessage(nativeCtxHandle, &desc, 0, nullptr);
			if (ss == SEC_E_OK)
			{
				for (int_t i = 0; i < 4; ++i)
				{
					if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].pvBuffer)
					{
						const std::size_t plainLength = std::min<std::size_t>(buffers[i].cbBuffer, _data.length);
						std::memcpy(_data.ptr, buffers[i].pvBuffer, plainLength);

						for (int_t j = 0; j < 4; ++j)
						{
							if (buffers[j].BufferType == SECBUFFER_EXTRA && buffers[j].pvBuffer && buffers[j].cbBuffer > 0)
							{
								const auto* p = static_cast<const byte_t*>(buffers[j].pvBuffer);
								nativeMessageBuffer->data = span.subspan(static_cast<std::size_t>(p - span.data()), buffers[j].cbBuffer);
							}
						}

						co_return R::Ok(plainLength);
					}
				}

				// SECBUFFER_DATA 없이 EOK — 컨트롤 메시지(예: 재협상 알림). 다음 레코드를 마저 받는다.
				dataInBuffer = 0;
				continue;
			}

			if (ss == SEC_I_CONTEXT_EXPIRED) co_return R::Ok(0);

			if (ss == SEC_E_INCOMPLETE_MESSAGE)
			{
				if (dataInBuffer >= span.size())
				{
					nativeMessageBuffer->Resize(span.size() * 2);
					span = nativeMessageBuffer->GetBuffer();
				}

				auto receiveResult = co_await RecvSome(transport, span.data() + dataInBuffer, span.size() - dataInBuffer, _stopToken);
				if (receiveResult.IsError()) co_return R::Error(std::move(receiveResult.Error()));
				if (receiveResult.Value() == 0) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "peer closed mid-record" }.Context("[TlsStream/Receive]"));

				dataInBuffer += receiveResult.Value();
				continue;
			}

			co_return R::Error(SchannelError(ss, "[TlsStream/Receive/Decrypt]"));
		}
	}

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Shutdown()
	{
		(void_t)Close();
		co_return ne::io::IoResult<void_t>::Ok();
	}

	ne::Result<void_t, ne::io::IoError> TlsStream::Close()
	{
		using R = ne::Result<void_t, ne::io::IoError>;

		if (!IsOpen()) return R::Ok();

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
			functionTable->InitializeSecurityContextW(rawCred, rawCtx, nullptr, ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM, 0, 0, nullptr, 0, nullptr, &outDesc, &retFlags, nullptr);

			// Close()는 IStream 계약상 동기(Task 아님) — 여기서는 readiness 대기 없이 best-effort 로 알림만 보낸다.
			if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0)
			{
				::send(transport.Handle(), static_cast<const char*>(outBuffers[0].pvBuffer), static_cast<int>(outBuffers[0].cbBuffer), 0);
				functionTable->FreeContextBuffer(outBuffers[0].pvBuffer);
			}

			functionTable->DeleteSecurityContext(rawCtx);
			functionTable->FreeCredentialHandle(rawCred);
		}

		delete static_cast<CtxtHandle*>(ctxHandle);
		delete static_cast<CredHandle*>(credHandle);
		delete static_cast<TlsMessageBuffer*>(messageBuffer);
		ctxHandle = credHandle = messageBuffer = nullptr;

		(void_t)transport.Close(); // 소켓 소멸

		return R::Ok();
	}
#elif defined(NEBULA_WITH_OPENSSL)
	static ne::io::IoError SslError(const string_view_t _ctx)
	{
		const auto code = static_cast<ne::ulong_t>(ERR_get_error());
		const char* message = ERR_error_string(code, nullptr);

		return ne::io::IoError{ ne::OsError{ code, message ? message : "SSL error" } }.Context(_ctx);
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

		if (!_config.caFile.empty() && SSL_CTX_load_verify_locations(sslCtx, _config.caFile.c_str(), nullptr) != 1)
		{
			SSL_CTX_free(sslCtx);
			co_return R::Error(SslError("[TlsStream/Connect/CA]"));
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
		if (!_host.empty()) SSL_set_tlsext_host_name(tempSsl, string_t(_host).c_str());

		TlsStream stream(std::move(plainTransport), sslCtx, tempSsl, _allocator);
		stream.sniHost = string_t(_host);
		stream.alpnCandidates = _config.alpnProtocols;

		if (auto result = co_await stream.Handshake(_stopToken); result.IsError())
			co_return R::Error(std::move(result.Error()));

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
				if (auto result = co_await stream.transport.WaitReadable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await stream.transport.WaitWritable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
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
				if (auto result = co_await transport.WaitReadable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await transport.WaitWritable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}

			co_return R::Error(SslError("[TlsStream/Handshake]"));
		}

		const unsigned char* alpnData = nullptr;
		unsigned int alpnLen = 0;
		SSL_get0_alpn_selected(nativeSsl, &alpnData, &alpnLen);
		if (alpnData && alpnLen > 0) negotiatedProtocol.assign(reinterpret_cast<const char*>(alpnData), alpnLen);

		co_return R::Ok();
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Send(const ne::io::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen())
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Send]"));

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
				if (auto result = co_await transport.WaitWritable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await transport.WaitReadable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}

			co_return R::Error(SslError("[TlsStream/Send]"));
		}

		co_return R::Ok(sent);
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receive(const ne::io::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen())
			co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Receive]"));

		auto* nativeSsl = static_cast<SSL*>(ssl);
		while (true)
		{
			const int bytes = SSL_read(nativeSsl, _data.ptr, static_cast<int>(_data.length));
			if (bytes > 0) co_return R::Ok(static_cast<std::size_t>(bytes));
			if (bytes == 0) co_return R::Ok(0);

			const int sslError = SSL_get_error(nativeSsl, bytes);
			if (sslError == SSL_ERROR_WANT_READ)
			{
				if (auto result = co_await transport.WaitReadable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}
			else if (sslError == SSL_ERROR_WANT_WRITE)
			{
				if (auto result = co_await transport.WaitWritable(_stopToken); result.IsError())
					co_return R::Error(std::move(result.Error()));
			}

			co_return R::Error(SslError("[TlsStream/Receive]"));
		}
	}

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Shutdown()
	{
		(void_t)Close();
		co_return ne::io::IoResult<void_t>::Ok();
	}

	ne::Result<void_t, ne::io::IoError> TlsStream::Close()
	{
		using R = ne::Result<void_t, ne::io::IoError>;
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
	static ne::io::IoError NoTls(const string_view_t _ctx) { return ne::io::IoError{ ne::io::IoErrorKind::UNSUPPORTED, "TLS not available (define NEBULA_WITH_OPENSSL on POSIX)" }.Context(_ctx); }



	TlsStream::~TlsStream() = default;
	TlsStream::TlsStream(TlsStream&&) noexcept = default;
	TlsStream& TlsStream::operator=(TlsStream&&) noexcept = default;



	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Connect(ne::io::Socket&&, ne::io::Context&, string_view_t, const TlsConfig&, std::stop_token, ne::memory::IAllocator*) { co_return ne::io::IoResult<TlsStream>::Error(NoTls("[TlsStream/Connect]")); }

	ne::Task<ne::io::IoResult<TlsStream>> TlsStream::Accept(ne::io::Socket&&, ne::io::Context&, const TlsConfig&, std::stop_token, ne::memory::IAllocator*) { co_return ne::io::IoResult<TlsStream>::Error(NoTls("[TlsStream/Accept]")); }

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Handshake(std::stop_token) { co_return ne::io::IoResult<void_t>::Error(NoTls("[TlsStream/Handshake]")); }

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Send(ne::io::BufferView, std::stop_token) { co_return ne::io::IoResult<std::size_t>::Error(NoTls("[TlsStream/Send]")); }

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receive(ne::io::BufferView, std::stop_token) { co_return ne::io::IoResult<std::size_t>::Error(NoTls("[TlsStream/Receive]")); }

	ne::Task<ne::io::IoResult<void_t>> TlsStream::Shutdown() { co_return ne::io::IoResult<void_t>::Ok(); }

	ne::Result<void_t, ne::io::IoError> TlsStream::Close() { return ne::Result<void_t, ne::io::IoError>::Ok(); }
#endif



	// ─── Sendv/Receivev — 백엔드 무관 공통 구현. TLS 레코드는 세그먼트별로 암복호화해야 하므로
	// (BufferChain::Flatten 은 존재하지 않음) 세그먼트 순서대로 Send()/Receive() 를 반복 호출한다. ───
	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Sendv(const ne::io::BufferChain& _chain, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		std::size_t total = 0;
		for (const auto& segment : _chain.Segments())
		{
			auto result = co_await Send(segment, _stopToken);
			if (result.IsError())
				co_return R::Error(std::move(result.Error()).Context("[TlsStream/Sendv]"));

			total += result.Value();
			if (result.Value() < segment.length) break; // 상대 종료 등으로 짧게 끝남 — 더 진행하지 않음
		}

		co_return R::Ok(total);
	}

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receivev(const ne::io::BufferChain& _chain, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		std::size_t total = 0;
		for (const auto& segment : _chain.Segments())
		{
			auto result = co_await Receive(segment, _stopToken);
			if (result.IsError())
				co_return R::Error(std::move(result.Error()).Context("[TlsStream/Receivev]"));

			total += result.Value();
			if (result.Value() < segment.length) break; // EOF 또는 짧은 읽기 — 세그먼트 경계에서 멈춤
		}

		co_return R::Ok(total);
	}

END_NS
