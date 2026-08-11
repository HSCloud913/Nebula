//
// Created by hscloud on 25. 6. 29.
//
// TlsStream 의 Windows(Schannel/SSPI) 백엔드 구현입니다 — POSIX 판은 Internal/OpenSsl/OpenSslStream.cpp.
// CMake 가 플랫폼에 따라 둘 중 하나만 컴파일합니다.

#include "Network/Stream/Tls/TlsStream.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>
#include "Network/Stream/Tls/Internal/Transfer.h"
#include "Network/Stream/Tls/Internal/Schannel/SspiWrapper.h"
#include "Network/Stream/Tls/Internal/Schannel/TlsMessageBuffer.h"
#include "Util/StringFormat.h"
#include "Base/WinsockApi.h"
#include <schannel.h>

using namespace ne::network::internal; // SspiWrapper·TlsMessageBuffer·Transfer 는 내부 격리 네임스페이스

namespace ne::network
{
	namespace
	{
		// SEC_APPLICATION_PROTOCOLS { DWORD ProtocolListsSize; SEC_APPLICATION_PROTOCOL_LIST ProtocolLists[1]; }
		// 를 SECBUFFER_APPLICATION_PROTOCOLS 입력 버퍼용으로 직렬화. 구조체를 통째로 memcpy 하면 컴파일러
		// 패딩에 따라 레이아웃이 어긋날 수 있어(특히 4바이트 enum + 2바이트 WORD 경계) 필드별로 직접 채운다.
		// ALPN 프로토콜 후보 목록을 SECBUFFER_APPLICATION_PROTOCOLS 입력 버퍼 포맷으로 직렬화한다.
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
	}


	// SECURITY_STATUS 를 이 라이브러리의 공통 IoError 로 변환한다.
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
		if (!functionTable) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: secur32.dll load failed" }.Context("[TlsStream/Connect]"));

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
		if (!functionTable) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: secur32.dll load failed" }.Context("[TlsStream/Accept]"));

		std::ifstream pfxFile(_config.certFile, std::ios::binary);
		if (!pfxFile) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: failed to open PFX file" }.Context("[TlsStream/Accept]"));

		std::vector<BYTE> pfxBytes((std::istreambuf_iterator<char>(pfxFile)), std::istreambuf_iterator<char>{});

		CRYPT_DATA_BLOB blob{ static_cast<DWORD>(pfxBytes.size()), pfxBytes.data() };
		HCERTSTORE certStore = ::PFXImportCertStore(&blob, _config.pfxPassword.empty() ? nullptr : ne::util::StringFormat::UTF8toWCS(_config.pfxPassword.c_str()).c_str(), PKCS12_INCLUDE_EXTENDED_PROPERTIES);
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
		if (!functionTable) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "SChannel: secur32.dll load failed" }.Context("[TlsStream/Handshake]"));

		auto whost = ne::util::StringFormat::UTF8toWCS(sniHost.c_str());
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

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Send(const ne::memory::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Send]"));

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

	ne::Task<ne::io::IoResult<std::size_t>> TlsStream::Receive(const ne::memory::BufferView _data, std::stop_token _stopToken)
	{
		using R = ne::io::IoResult<std::size_t>;

		if (!IsOpen()) co_return R::Error(ne::io::IoError{ ne::io::IoErrorKind::OS_FAILURE, "TLS stream closed" }.Context("[TlsStream/Receive]"));

		const auto* functionTable = SspiWrapper::Get();
		auto* nativeCtxHandle = static_cast<CtxtHandle*>(ctxHandle);
		auto* nativeMessageBuffer = static_cast<TlsMessageBuffer*>(messageBuffer);
		// 지난 호출에서 호출자 버퍼가 작아 남겨 둔 평문이 있으면 먼저 그것부터 넘긴다. 이 잔여분을
		// 버리면 레코드 평문(최대 16KiB)이 호출자 버퍼(보통 1~4KiB)를 넘길 때마다 데이터가 소실된다.
		if (nativeMessageBuffer->HasPlaintext()) co_return R::Ok(nativeMessageBuffer->TakePlaintext(_data.ptr, _data.length));

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
						const auto* plain = static_cast<const byte_t*>(buffers[i].pvBuffer);
						const std::size_t plainLength = buffers[i].cbBuffer;

						// 아직 복호화되지 않은 암호문(SECBUFFER_EXTRA)을 먼저 챙긴다. 평문 보관이
						// buffer 를 건드리지는 않지만, 순서를 이렇게 두면 두 잔여분의 출처가 명확하다.
						for (int_t j = 0; j < 4; ++j)
						{
							if (buffers[j].BufferType == SECBUFFER_EXTRA && buffers[j].pvBuffer && buffers[j].cbBuffer > 0)
							{
								const auto* extra = static_cast<const byte_t*>(buffers[j].pvBuffer);
								nativeMessageBuffer->data = span.subspan(static_cast<std::size_t>(extra - span.data()), buffers[j].cbBuffer);
							}
						}

						// 레코드 평문이 호출자 버퍼보다 크면 남는 만큼을 보관해 다음 Receive 에서 넘긴다.
						if (plainLength > _data.length)
						{
							nativeMessageBuffer->StorePlaintext(plain, plainLength);
							co_return R::Ok(nativeMessageBuffer->TakePlaintext(_data.ptr, _data.length));
						}

						std::memcpy(_data.ptr, plain, plainLength);

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

	ne::io::IoResult<void_t> TlsStream::Close()
	{
		using R = ne::io::IoResult<void_t>;

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
}
