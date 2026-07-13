//
// Created by hscloud on 26. 7. 12.
//
// TlsStream(Windows=Schannel / POSIX=OpenSSL) loopback 검증. 자체 서명 인증서를 테스트 시작 시
// 프로그램적으로 생성해 임시 파일에 기록한다(정리는 TestCert 소멸자).

#include <gtest/gtest.h>

#if defined(_WIN32) || defined(NEBULA_WITH_OPENSSL)

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Io/Context/Context.h"
#include "Io/Socket/Socket.h"
#include "Network/Stream/Tls/TlsStream.h"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <wincrypt.h>
#   include "Io/Engine/Iocp/IocpEngine.h"
#elif defined(NEBULA_WITH_OPENSSL)
#   include <openssl/evp.h>
#   include <openssl/pem.h>
#   include <openssl/rsa.h>
#   include <openssl/x509.h>
#   include "Io/Engine/Epoll/EpollEngine.h"
#endif

using namespace ne;
using namespace ne::io;
using ne::network::TlsConfig;
using ne::network::TlsStream;

namespace
{
	#if defined(_WIN32)
	using TestEngine = IocpEngine;
	#else
	using TestEngine = EpollEngine;
	#endif

	template <typename T>
	T Drive(Context& _context, ne::Task<T>& _task, const std::chrono::milliseconds _timeout = std::chrono::seconds(10))
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_task.IsReady())
		{
			ADD_FAILURE() << "Drive: task did not complete within timeout";
			std::abort();
		}

		return _task.await_resume();
	}

	// Accept(TLS)/Connect(TLS) 는 서로 데이터를 주고받아야 진행되는 핸드셰이크라 반드시 함께 구동해야 한다.
	template <typename T1, typename T2>
	std::pair<T1, T2> DriveBoth(Context& _context, ne::Task<T1>& _task1, ne::Task<T2>& _task2, const std::chrono::milliseconds _timeout = std::chrono::seconds(10))
	{
		_task1.Resume();
		_task2.Resume();
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while ((!_task1.IsReady() || !_task2.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_task1.IsReady() || !_task2.IsReady())
		{
			ADD_FAILURE() << "DriveBoth: task did not complete within timeout";
			std::abort();
		}

		return { _task1.await_resume(), _task2.await_resume() };
	}

	IoResult<uint16_t> BindEphemeralListener(Socket& _listener)
	{
		using R = IoResult<uint16_t>;
		if (auto r = _listener.Bind("127.0.0.1", 0); r.IsError()) return R::Error(std::move(r.Error()));
		if (auto r = _listener.Listen(); r.IsError()) return R::Error(std::move(r.Error()));

		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);
		if (::getsockname(_listener.Handle(), reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "getsockname failed" });

		return R::Ok(::ntohs(addr.sin_port));
	}

	IoResult<std::pair<Socket, Socket>> ConnectPair(Context& _context, Socket& _listener, const uint16_t _port)
	{
		using R = IoResult<std::pair<Socket, Socket>>;

		auto clientResult = Socket::Create(_context, AF_INET);
		if (clientResult.IsError()) return R::Error(std::move(clientResult.Error()));
		Socket client = std::move(clientResult.Value());

		auto acceptTask = _listener.Accept();
		auto connectTask = client.Connect("127.0.0.1", _port);
		acceptTask.Resume();
		connectTask.Resume();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });
		if (!acceptTask.IsReady() || !connectTask.IsReady()) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "accept/connect did not complete in time" });

		auto acceptResult = acceptTask.await_resume();
		auto connectResult = connectTask.await_resume();
		if (acceptResult.IsError()) return R::Error(std::move(acceptResult.Error()));
		if (connectResult.IsError()) return R::Error(std::move(connectResult.Error()));

		return R::Ok(std::make_pair(std::move(acceptResult.Value()), std::move(client)));
	}



	// ── 자체 서명 인증서(테스트 전용) — 임시 파일에 기록, 소멸자에서 정리 ──

	struct TestCert
	{
		string_t certFile;
		string_t keyFile;     // OpenSSL 전용(PEM 개인키). Windows 는 PFX 하나에 다 들어있어 비어있음.
		string_t pfxPassword; // Windows 전용.

		TestCert() = default;
		TestCert(TestCert&&) = default;
		TestCert& operator=(TestCert&&) = default;
		TestCert(const TestCert&) = delete;
		TestCert& operator=(const TestCert&) = delete;

		~TestCert()
		{
			if (!certFile.empty()) std::remove(certFile.c_str());
			if (!keyFile.empty()) std::remove(keyFile.c_str());
		}
	};

	#if defined(_WIN32)

	// CertCreateSelfSignCertificate + PFXExportCertStoreEx 로 메모리상에서 self-signed PFX 를 만든다.
	IoResult<std::vector<BYTE>> GenerateSelfSignedPfx(const std::wstring& _password)
	{
		using R = IoResult<std::vector<BYTE>>;
		const std::wstring containerName = L"NebulaTlsTestContainer";

		HCRYPTPROV provider = 0;
		::CryptAcquireContextW(&provider, containerName.c_str(), MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_DELETEKEYSET); // 이전 실행 잔여물 제거(실패해도 무방)
		if (!::CryptAcquireContextW(&provider, containerName.c_str(), MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_NEWKEYSET))
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/AcquireContext]"));

		HCRYPTKEY key = 0;
		if (!::CryptGenKey(provider, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &key))
		{
			::CryptReleaseContext(provider, 0);
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/GenKey]"));
		}
		::CryptDestroyKey(key);
		::CryptReleaseContext(provider, 0);

		CRYPT_KEY_PROV_INFO keyProvInfo{};
		keyProvInfo.pwszContainerName = const_cast<wchar_t*>(containerName.c_str());
		keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENHANCED_PROV_W);
		keyProvInfo.dwProvType = PROV_RSA_FULL;
		keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;

		BYTE subjectNameBuffer[256]{};
		DWORD subjectNameSize = sizeof(subjectNameBuffer);
		if (!::CertStrToNameW(X509_ASN_ENCODING, L"CN=nebula-tls-test", CERT_X500_NAME_STR, nullptr, subjectNameBuffer, &subjectNameSize, nullptr))
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/StrToName]"));

		CERT_NAME_BLOB subjectBlob{ subjectNameSize, subjectNameBuffer };

		SYSTEMTIME startTime{};
		::GetSystemTime(&startTime);
		SYSTEMTIME endTime = startTime;
		endTime.wYear += 1;

		PCCERT_CONTEXT certContext = ::CertCreateSelfSignCertificate(0, &subjectBlob, 0, &keyProvInfo, nullptr, &startTime, &endTime, nullptr);
		if (!certContext) return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/SelfSign]"));

		HCERTSTORE memStore = ::CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
		if (!memStore)
		{
			::CertFreeCertificateContext(certContext);
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/OpenStore]"));
		}

		if (!::CertAddCertificateContextToStore(memStore, certContext, CERT_STORE_ADD_ALWAYS, nullptr))
		{
			::CertFreeCertificateContext(certContext);
			::CertCloseStore(memStore, 0);
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/AddToStore]"));
		}
		::CertFreeCertificateContext(certContext);

		CRYPT_DATA_BLOB pfxBlob{};
		const DWORD exportFlags = EXPORT_PRIVATE_KEYS | REPORT_NO_PRIVATE_KEY | REPORT_NOT_ABLE_TO_EXPORT_PRIVATE_KEY;
		if (!::PFXExportCertStoreEx(memStore, &pfxBlob, _password.c_str(), nullptr, exportFlags))
		{
			::CertCloseStore(memStore, 0);
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/ExportSize]"));
		}

		std::vector<BYTE> pfxBytes(pfxBlob.cbData);
		pfxBlob.pbData = pfxBytes.data();
		if (!::PFXExportCertStoreEx(memStore, &pfxBlob, _password.c_str(), nullptr, exportFlags))
		{
			::CertCloseStore(memStore, 0);
			return R::Error(IoError{ ne::OsError{ ::GetLastError() } }.Context("[TestCert/Export]"));
		}

		::CertCloseStore(memStore, 0);
		return R::Ok(std::move(pfxBytes));
	}

	IoResult<TestCert> MakeTestCert()
	{
		using R = IoResult<TestCert>;

		auto pfxResult = GenerateSelfSignedPfx(L"nebula-test");
		if (pfxResult.IsError()) return R::Error(std::move(pfxResult.Error()));

		TestCert cert;
		cert.certFile = "test_tls_selfsigned.pfx";
		cert.pfxPassword = "nebula-test";

		std::ofstream out(cert.certFile, std::ios::binary);
		if (!out) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "failed to write PFX file" });
		out.write(reinterpret_cast<const char*>(pfxResult.Value().data()), static_cast<std::streamsize>(pfxResult.Value().size()));

		return R::Ok(std::move(cert));
	}

	#elif defined(NEBULA_WITH_OPENSSL)

	bool_t GenerateSelfSignedPem(std::string& _outCertPem, std::string& _outKeyPem)
	{
		EVP_PKEY_CTX* keyCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
		if (!keyCtx) return false;

		EVP_PKEY* pkey = nullptr;
		if (EVP_PKEY_keygen_init(keyCtx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(keyCtx, 2048) <= 0 || EVP_PKEY_keygen(keyCtx, &pkey) <= 0)
		{
			EVP_PKEY_CTX_free(keyCtx);
			return false;
		}
		EVP_PKEY_CTX_free(keyCtx);

		X509* x509 = X509_new();
		ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
		X509_gmtime_adj(X509_getm_notBefore(x509), 0);
		X509_gmtime_adj(X509_getm_notAfter(x509), 60 * 60 * 24 * 365);
		X509_set_pubkey(x509, pkey);

		X509_NAME* name = X509_get_subject_name(x509);
		X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("nebula-tls-test"), -1, -1, 0);
		X509_set_issuer_name(x509, name);

		const bool_t signOk = X509_sign(x509, pkey, EVP_sha256()) > 0;

		BIO* certBio = BIO_new(BIO_s_mem());
		PEM_write_bio_X509(certBio, x509);
		char* certData = nullptr;
		const long certLen = BIO_get_mem_data(certBio, &certData);
		_outCertPem.assign(certData, static_cast<std::size_t>(certLen));

		BIO* keyBio = BIO_new(BIO_s_mem());
		PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
		char* keyData = nullptr;
		const long keyLen = BIO_get_mem_data(keyBio, &keyData);
		_outKeyPem.assign(keyData, static_cast<std::size_t>(keyLen));

		BIO_free(certBio);
		BIO_free(keyBio);
		X509_free(x509);
		EVP_PKEY_free(pkey);

		return signOk;
	}

	IoResult<TestCert> MakeTestCert()
	{
		using R = IoResult<TestCert>;

		std::string certPem, keyPem;
		if (!GenerateSelfSignedPem(certPem, keyPem)) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "self-signed cert generation failed" });

		TestCert cert;
		cert.certFile = "test_tls_selfsigned.pem";
		cert.keyFile = "test_tls_selfsigned.key";

		std::ofstream certOut(cert.certFile, std::ios::binary);
		if (!certOut) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "failed to write cert PEM" });
		certOut << certPem;
		certOut.close();

		std::ofstream keyOut(cert.keyFile, std::ios::binary);
		if (!keyOut) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "failed to write key PEM" });
		keyOut << keyPem;
		keyOut.close();

		return R::Ok(std::move(cert));
	}

	#endif

	// 리스너 + accept/connect 소켓 쌍까지 준비된 상태를 반환 — 각 테스트가 TlsConfig 만 다르게 채워 이어붙인다.
	struct SocketSetup
	{
		Socket accepted;
		Socket client;
	};

	IoResult<SocketSetup> MakeConnectedSocketPair(Context& _context)
	{
		using R = IoResult<SocketSetup>;

		auto listenerResult = Socket::Create(_context, AF_INET);
		if (listenerResult.IsError()) return R::Error(std::move(listenerResult.Error()));
		Socket listener = std::move(listenerResult.Value());

		auto portResult = BindEphemeralListener(listener);
		if (portResult.IsError()) return R::Error(std::move(portResult.Error()));

		auto pairResult = ConnectPair(_context, listener, portResult.Value());
		if (pairResult.IsError()) return R::Error(std::move(pairResult.Error()));
		auto [accepted, client] = std::move(pairResult.Value());

		return R::Ok(SocketSetup{ std::move(accepted), std::move(client) });
	}
}

TEST(TlsStreamTest, ConnectAcceptSendReceiveRoundTrip)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto certResult = MakeTestCert();
	ASSERT_TRUE(certResult.IsOk()) << certResult.Error().What();
	TestCert cert = std::move(certResult.Value());

	auto setupResult = MakeConnectedSocketPair(context);
	ASSERT_TRUE(setupResult.IsOk()) << setupResult.Error().What();
	auto setup = std::move(setupResult.Value());

	TlsConfig serverConfig;
	serverConfig.certFile = cert.certFile;
	serverConfig.keyFile = cert.keyFile;
	serverConfig.pfxPassword = cert.pfxPassword;

	TlsConfig clientConfig;
	clientConfig.verifyPeer = false; // 자체서명 — CA 검증 생략

	auto acceptTask = TlsStream::Accept(std::move(setup.accepted), context, serverConfig);
	auto connectTask = TlsStream::Connect(std::move(setup.client), context, "localhost", clientConfig);
	auto [acceptResult, connectResult] = DriveBoth(context, acceptTask, connectTask);

	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	TlsStream server = std::move(acceptResult.Value());
	TlsStream tlsClient = std::move(connectResult.Value());

	const char payload[] = "hello-tls-roundtrip";
	const std::size_t length = sizeof(payload) - 1;

	auto sendTask = tlsClient.Send(BufferView{ reinterpret_cast<byte_t*>(const_cast<char*>(payload)), length });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), length);

	byte_t buffer[64]{};
	auto receiveTask = server.Receive(BufferView{ buffer, length });
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
}

TEST(TlsStreamTest, ShutdownSignalsCloseNotify)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto certResult = MakeTestCert();
	ASSERT_TRUE(certResult.IsOk()) << certResult.Error().What();
	TestCert cert = std::move(certResult.Value());

	auto setupResult = MakeConnectedSocketPair(context);
	ASSERT_TRUE(setupResult.IsOk()) << setupResult.Error().What();
	auto setup = std::move(setupResult.Value());

	TlsConfig serverConfig;
	serverConfig.certFile = cert.certFile;
	serverConfig.keyFile = cert.keyFile;
	serverConfig.pfxPassword = cert.pfxPassword;

	TlsConfig clientConfig;
	clientConfig.verifyPeer = false;

	auto acceptTask = TlsStream::Accept(std::move(setup.accepted), context, serverConfig);
	auto connectTask = TlsStream::Connect(std::move(setup.client), context, "localhost", clientConfig);
	auto [acceptResult, connectResult] = DriveBoth(context, acceptTask, connectTask);
	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	TlsStream server = std::move(acceptResult.Value());
	TlsStream tlsClient = std::move(connectResult.Value());

	auto shutdownTask = tlsClient.Shutdown();
	auto shutdownResult = Drive(context, shutdownTask);
	ASSERT_TRUE(shutdownResult.IsOk()) << shutdownResult.Error().What();

	byte_t buffer[16]{};
	auto eofTask = server.Receive(BufferView{ buffer, sizeof(buffer) });
	auto eofResult = Drive(context, eofTask);
	ASSERT_TRUE(eofResult.IsOk()) << eofResult.Error().What();
	EXPECT_EQ(eofResult.Value(), 0u);
}

TEST(TlsStreamTest, RejectsUntrustedCertWhenVerifyPeerTrue)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto certResult = MakeTestCert();
	ASSERT_TRUE(certResult.IsOk()) << certResult.Error().What();
	TestCert cert = std::move(certResult.Value());

	auto setupResult = MakeConnectedSocketPair(context);
	ASSERT_TRUE(setupResult.IsOk()) << setupResult.Error().What();
	auto setup = std::move(setupResult.Value());

	TlsConfig serverConfig;
	serverConfig.certFile = cert.certFile;
	serverConfig.keyFile = cert.keyFile;
	serverConfig.pfxPassword = cert.pfxPassword;

	TlsConfig clientConfig; // verifyPeer 기본값 true, caFile 없음 — 자체서명은 신뢰 루트에 없어 실패해야 함

	auto acceptTask = TlsStream::Accept(std::move(setup.accepted), context, serverConfig);
	auto connectTask = TlsStream::Connect(std::move(setup.client), context, "localhost", clientConfig);
	auto [acceptResult, connectResult] = DriveBoth(context, acceptTask, connectTask);

	EXPECT_TRUE(connectResult.IsError());
}

TEST(TlsStreamTest, AlpnNegotiatesPreferredProtocol)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto certResult = MakeTestCert();
	ASSERT_TRUE(certResult.IsOk()) << certResult.Error().What();
	TestCert cert = std::move(certResult.Value());

	auto setupResult = MakeConnectedSocketPair(context);
	ASSERT_TRUE(setupResult.IsOk()) << setupResult.Error().What();
	auto setup = std::move(setupResult.Value());

	TlsConfig serverConfig;
	serverConfig.certFile = cert.certFile;
	serverConfig.keyFile = cert.keyFile;
	serverConfig.pfxPassword = cert.pfxPassword;
	serverConfig.alpnProtocols = { "http/1.1" }; // 서버가 지원하는 것(우선순위 목록, RFC 7301 은 서버 우선순위를 따름)

	TlsConfig clientConfig;
	clientConfig.verifyPeer = false;
	clientConfig.alpnProtocols = { "h2", "http/1.1" }; // 클라이언트 제안 — h2 를 먼저 제안하지만 서버가 http/1.1 만 지원

	auto acceptTask = TlsStream::Accept(std::move(setup.accepted), context, serverConfig);
	auto connectTask = TlsStream::Connect(std::move(setup.client), context, "localhost", clientConfig);
	auto [acceptResult, connectResult] = DriveBoth(context, acceptTask, connectTask);

	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	TlsStream server = std::move(acceptResult.Value());
	TlsStream tlsClient = std::move(connectResult.Value());

	EXPECT_EQ(server.NegotiatedProtocol(), "http/1.1");
	EXPECT_EQ(tlsClient.NegotiatedProtocol(), "http/1.1");
}

#endif // defined(_WIN32) || defined(NEBULA_WITH_OPENSSL)
