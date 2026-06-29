//
// NebulaProtocol DLL 통합 테스트 Runner
//

#include <iostream>
#include <thread>
#include <atomic>
#include <future>
#include <string>
#include <span>
#include <array>
#include <chrono>
#include <format>

#include "Socket/TcpSocket.h"
#include "Socket/TlsSocket.h"
#include "Socket/UdpSocket.h"
#include "Ipc/Pipe.h"
#include "Ipc/SharedMemory.h"
#include "Ipc/MessageQueue.h"
#include "Ipc/Semaphore.h"
#include "Http/Client/Request.h"
#include "Http/Server/Server.h"
#include "Ftp/FtpClient.h"
#include "Sftp/SftpClient.h"

using namespace ne::protocol;
using namespace ne::protocol::Http;
using namespace ne::protocol::Ipc;
using namespace std::chrono_literals;

namespace Ftp    = ne::protocol::Ftp;
namespace Sftp   = ne::protocol::Sftp;
namespace HttpSrv = ne::protocol::Http::Server;

// ─── 출력 헬퍼 ────────────────────────────────────────────────────────────────

namespace
{
    int gPassed = 0;
    int gFailed = 0;
    int gSkipped = 0;

    void Pass(const std::string& _label)
    {
        std::cout << "  [PASS] " << _label << '\n';
        ++gPassed;
    }

    void Fail(const std::string& _label, const std::string& _reason = "")
    {
        if (_reason.empty()) std::cout << "  [FAIL] " << _label << '\n';
        else                 std::cout << "  [FAIL] " << _label << " \xe2\x80\x94 " << _reason << '\n';
        ++gFailed;
    }

    void Skip(const std::string& _label, const std::string& _reason = "")
    {
        if (_reason.empty()) std::cout << "  [SKIP] " << _label << '\n';
        else                 std::cout << "  [SKIP] " << _label << " (" << _reason << ")\n";
        ++gSkipped;
    }

    std::span<const std::byte> AsBytes(const std::string& _s)
    {
        return { reinterpret_cast<const std::byte*>(_s.data()), _s.size() };
    }

    template <typename Socket>
    int GetBoundPort(const Socket& _socket)
    {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        ::getsockname(_socket.GetHandle(), reinterpret_cast<sockaddr*>(&addr), &len);
        return static_cast<int>(::ntohs(addr.sin_port));
    }
}

// =============================================================================
//  TCP Socket
// =============================================================================

void TestTcpEchoSync()
{
    std::cout << "\n[TcpSocket] 동기 에코\n";

    auto listener = TcpSocket("127.0.0.1", 0);
    listener.Bind();
    listener.Listen();
    const int port = GetBoundPort(listener);

    std::thread server([&listener]
    {
        auto session = TcpSocket(listener.Accept());
        std::array<std::byte, 256> buf{};
        if (const auto n = session.Read(buf); n > 0)
            session.Write(std::span<const std::byte>(buf).first(static_cast<std::size_t>(n)));
    });

    try
    {
        auto client = TcpSocket("127.0.0.1", port);
        client.Connect();

        const auto msg = std::string("Hello Nebula!");
        client.Write(AsBytes(msg));

        std::array<std::byte, 256> buf{};
        const auto n = client.Read(buf);
        const auto echo = std::string(reinterpret_cast<const char*>(buf.data()), n);

        server.join();

        if (echo == msg) Pass("에코 일치");
        else             Fail("에코 불일치", std::format("got '{}'", echo));
    }
    catch (const std::exception& e) { server.join(); Fail("예외", e.what()); }
}

void TestTcpIocpEcho()
{
    std::cout << "\n[TcpSocket] IOCP 에코\n";

    auto listener = TcpSocket("127.0.0.1", 0);
    listener.Bind();
    listener.Listen();
    const int port = GetBoundPort(listener);

    std::string serverGot;

    std::thread server([&listener, &serverGot]
    {
        auto session = TcpSocket(listener.Accept());
        session.RegisterReadHandler([&session, &serverGot](std::vector<std::byte> data)
        {
            serverGot.assign(reinterpret_cast<const char*>(data.data()), data.size());
            session.Write(std::span<const std::byte>(data));
        });
        session.Iocp();
    });

    std::string clientGot;
    try
    {
        {
            auto client = TcpSocket("127.0.0.1", port);
            client.Connect();

            const auto msg = std::string("IOCP Echo");
            client.Write(AsBytes(msg));

            std::array<std::byte, 256> buf{};
            const auto n = client.Read(buf);
            if (n > 0) clientGot.assign(reinterpret_cast<const char*>(buf.data()), n);
        }

        server.join();

        if (serverGot == "IOCP Echo" && clientGot == "IOCP Echo")
            Pass("서버/클라이언트 에코 일치");
        else
            Fail("에코 불일치", std::format("server='{}' client='{}'", serverGot, clientGot));
    }
    catch (const std::exception& e) { server.join(); Fail("예외", e.what()); }
}

void TestTcpIocpMultiMessage()
{
    std::cout << "\n[TcpSocket] IOCP 다중 메시지\n";

    auto listener = TcpSocket("127.0.0.1", 0);
    listener.Bind();
    listener.Listen();
    const int port = GetBoundPort(listener);

    std::atomic<int> count{0};

    std::thread server([&listener, &count]
    {
        auto session = TcpSocket(listener.Accept());
        session.RegisterReadHandler([&count](std::vector<std::byte>)
        {
            count.fetch_add(1, std::memory_order_relaxed);
        });
        session.Iocp();
    });

    constexpr int N = 5;
    try
    {
        {
            auto client = TcpSocket("127.0.0.1", port);
            client.Connect();
            for (int i = 0; i < N; ++i)
            {
                const auto msg = std::format("msg-{}", i);
                client.Write(AsBytes(msg));
                std::this_thread::sleep_for(10ms);
            }
        }

        server.join();

        if (count.load() >= 1)
            Pass(std::format("{} 번 수신 (전송 {}회)", count.load(), N));
        else
            Fail("메시지 미수신");
    }
    catch (const std::exception& e) { server.join(); Fail("예외", e.what()); }
}

void TestTcpIocpExceptionHandler()
{
    std::cout << "\n[TcpSocket] IOCP 에러 핸들러\n";
    try
    {
        auto listener = TcpSocket("127.0.0.1", 0);
        listener.Bind();
        listener.Listen();
        const int port = GetBoundPort(listener);

        std::thread server([&listener]
        {
            auto session = TcpSocket(listener.Accept());
            session.RegisterReadHandler([](std::vector<std::byte>) {});
            session.RegisterExceptionHandler([](const std::string&) {});
            session.Iocp();
        });

        { auto client = TcpSocket("127.0.0.1", port); client.Connect(); }

        server.join();
        Pass("에러 핸들러 등록 후 정상 종료");
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  UDP Socket
// =============================================================================

void TestUdpEcho()
{
    std::cout << "\n[UdpSocket] 에코 (ReadFrom/WriteTo)\n";

    auto server = UdpSocket("127.0.0.1", 0);
    server.Bind();
    const int port = GetBoundPort(server);

    std::string serverGot;
    std::thread serverThread([&server, &serverGot]
    {
        try
        {
            std::array<std::byte, 256> buf{};
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            const auto n = server.ReadFrom(buf, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if (n > 0)
            {
                serverGot.assign(reinterpret_cast<const char*>(buf.data()), n);
                server.WriteTo(std::span<const std::byte>(buf).first(static_cast<std::size_t>(n)),
                               reinterpret_cast<sockaddr*>(&clientAddr), addrLen);
            }
        }
        catch (const std::exception&) {}
    });

    try
    {
        auto client = UdpSocket("127.0.0.1", port);
        client.Connect();
        client.Write(AsBytes("UDP Echo"));

        std::array<std::byte, 256> buf{};
        const auto n = client.Read(buf);
        const auto echo = std::string(reinterpret_cast<const char*>(buf.data()), n);

        serverThread.join();

        if (echo == "UDP Echo" && serverGot == "UDP Echo")
            Pass("에코 일치");
        else
            Fail("에코 불일치", std::format("server='{}' client='{}'", serverGot, echo));
    }
    catch (const std::exception& e) { serverThread.join(); Fail("예외", e.what()); }
}

// =============================================================================
//  IPC — Pipe
// =============================================================================

void TestPipeEcho()
{
    std::cout << "\n[Pipe] 에코 (Listen/Connect/Read/Write)\n";
    try
    {
        std::string serverGot;
        std::thread serverThread([&serverGot]
        {
            try
            {
                auto pipe = Pipe("nebula_runner_pipe");
                pipe.Listen();
                std::array<std::byte, 256> buf{};
                const auto n = pipe.Read(buf);
                if (n > 0)
                {
                    serverGot.assign(reinterpret_cast<const char*>(buf.data()), n);
                    pipe.Write(std::span<const std::byte>(buf).first(static_cast<std::size_t>(n)));
                }
            }
            catch (const std::exception&) {}
        });

        std::this_thread::sleep_for(50ms);

        std::string clientGot;
        {
            auto pipe = Pipe("nebula_runner_pipe");
            pipe.Connect();
            pipe.Write(AsBytes("Pipe Echo"));
            std::array<std::byte, 256> buf{};
            const auto n = pipe.Read(buf);
            if (n > 0) clientGot.assign(reinterpret_cast<const char*>(buf.data()), n);
        }

        serverThread.join();

        if (serverGot == "Pipe Echo" && clientGot == "Pipe Echo")
            Pass("에코 일치");
        else
            Fail("에코 불일치", std::format("server='{}' client='{}'", serverGot, clientGot));
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  IPC — SharedMemory
// =============================================================================

void TestSharedMemory()
{
    std::cout << "\n[SharedMemory] 읽기/쓰기\n";
    try
    {
        const auto payload = std::string("Hello, SharedMemory!");

        auto writer = SharedMemory("nebula_runner_shm", 256);
        const auto wv = writer.GetView();
        std::ranges::copy(
            std::span(reinterpret_cast<const std::byte*>(payload.data()), payload.size()),
            wv.begin()
        );

        auto reader = SharedMemory("nebula_runner_shm", 256);
        const auto rv = reader.GetView();
        const auto result = std::string(reinterpret_cast<const char*>(rv.data()), payload.size());

        if (result == payload) Pass("내용 일치");
        else                   Fail("내용 불일치", result);
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  IPC — MessageQueue
// =============================================================================

void TestMessageQueue()
{
    std::cout << "\n[MessageQueue] 송수신 (Listen/Connect/Send/Receive)\n";
    try
    {
        std::string serverGot;

        std::thread serverThread([&serverGot]
        {
            try
            {
                auto mq = MessageQueue("nebula_runner_mq");
                mq.Listen();
                const auto msg = mq.Receive();
                serverGot.assign(reinterpret_cast<const char*>(msg.data()), msg.size());
            }
            catch (const std::exception&) {}
        });

        std::this_thread::sleep_for(50ms);

        {
            auto mq = MessageQueue("nebula_runner_mq");
            mq.Connect();
            mq.Send(AsBytes("MQ Hello"));
        }

        serverThread.join();

        if (serverGot == "MQ Hello") Pass("메시지 수신 일치");
        else                          Fail("수신 내용 불일치", serverGot);
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  IPC — Semaphore
// =============================================================================

void TestSemaphore()
{
    std::cout << "\n[Semaphore] Acquire/TryAcquire/Release\n";
    try
    {
        auto sem = Semaphore("nebula_runner_sem", 1);

        if (!sem.TryAcquire())             { Fail("TryAcquire 실패 (count=1)"); return; }
        if (sem.TryAcquire())              { Fail("TryAcquire 성공 (count=0, 불가)"); sem.Release(); return; }
        sem.Release();
        if (!sem.TryAcquire())             { Fail("Release 후 TryAcquire 실패"); return; }
        sem.Release();

        // 멀티스레드: Acquire 블로킹 확인
        sem.Acquire();
        std::atomic<bool> acquired{false};
        std::thread t([&sem, &acquired]
        {
            sem.Acquire();
            acquired.store(true);
        });
        std::this_thread::sleep_for(30ms);
        if (acquired.load()) { Fail("Acquire가 블로킹하지 않음"); sem.Release(); t.join(); return; }
        sem.Release();
        t.join();
        if (!acquired.load()) { Fail("Release 후 Acquire가 재개되지 않음"); return; }

        Pass("Acquire/TryAcquire/Release 정상");
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  HTTP Server (로컬)
// =============================================================================

void TestHttpServer()
{
    std::cout << "\n[HttpServer] 로컬 GET/POST 라우팅 (127.0.0.1:19999)\n";
    try
    {
        // Listen()은 _isNonblocking 파라미터와 무관하게 항상 블로킹하므로
        // shared_ptr로 수명을 thread와 공유한다.
        auto server = std::make_shared<HttpSrv::Server>("127.0.0.1", 19999);
        server->SetTimeout(3s)
               .SetThreadPoolSize(1)
               .Route(Method::GET, "/ping", [](HttpSrv::Response&& res)
               {
                   res.SetStatusCode(StatusCode::OK);
                   res.SetBody("pong");
               })
               .Route(Method::POST, "/echo", [](HttpSrv::Response&& res)
               {
                   res.SetStatusCode(StatusCode::OK);
                   res.SetBody("echo-ok");
               });

        std::thread([server] { server->Listen(false); }).detach();
        std::this_thread::sleep_for(100ms);  // 서버 준비 대기

        auto getResp = Client::Get("http://127.0.0.1:19999/ping").Send();
        const auto getBody = getResp.GetBodyString();

        auto postResp = Client::Post("http://127.0.0.1:19999/echo")
            .SetBody("hello")
            .Send();
        const auto postBody = postResp.GetBodyString();

        if (getBody.find("pong") != std::string::npos && postBody.find("echo-ok") != std::string::npos)
            Pass(std::format("GET='{}' POST='{}'", getBody, postBody));
        else
            Fail("응답 불일치", std::format("GET='{}' POST='{}'", getBody, postBody));
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  TLS Socket
// =============================================================================

void TestTlsConnect()
{
    std::cout << "\n[TlsSocket] TLS 핸드셰이크 (google.com:443)\n";
    try
    {
        auto tls = TlsSocket("google.com", 443);
        tls.Connect();

        if (tls.IsConnected()) Pass("TLS 핸드셰이크 성공");
        else                   Fail("IsConnected() false");
    }
    catch (const std::exception& e) { Fail("예외 (인터넷 연결 확인)", e.what()); }
}

void TestTlsIocpRead()
{
    std::cout << "\n[TlsSocket] IOCP 수신 (httpbin.org:443, Connection: close)\n";

    std::string received;
    std::atomic<bool> done{false};
    std::exception_ptr ep;

    std::thread worker([&]
    {
        try
        {
            auto tls = TlsSocket("httpbin.org", 443);
            tls.Connect();
            tls.RegisterReadHandler([&received](std::vector<std::byte> data)
            {
                received.append(reinterpret_cast<const char*>(data.data()), data.size());
            });

            const auto req = std::string(
                "GET /get HTTP/1.1\r\n"
                "Host: httpbin.org\r\n"
                "Connection: close\r\n"
                "\r\n"
            );
            tls.Write(AsBytes(req));
            tls.Iocp();
        }
        catch (...) { ep = std::current_exception(); }
        done.store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(200ms);

    worker.detach();

    if (!done.load()) { Fail("타임아웃 (15s)"); return; }
    if (ep)
    {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { Fail("예외", e.what()); return; }
    }
    if (!received.empty() && received.find("HTTP/1") != std::string::npos)
        Pass(std::format("HTTP 응답 수신 ({} bytes)", received.size()));
    else if (received.empty())
        Fail("응답 없음 (readHandler 미호출)");
    else
        Fail("HTTP/1 없음", received.substr(0, std::min<std::size_t>(80, received.size())));
}

// =============================================================================
//  HTTP Client
// =============================================================================

void TestHttpGet()
{
    std::cout << "\n[HttpClient] GET http://httpbin.org/get\n";
    try
    {
        auto response = Client::Get("http://httpbin.org/get").Send();
        const auto body = response.GetBodyString();

        if (!body.empty()) Pass(std::format("응답 수신 ({} bytes)", body.size()));
        else               Fail("빈 응답");
    }
    catch (const std::exception& e) { Fail("예외 (인터넷 연결 확인)", e.what()); }
}

void TestHttpPost()
{
    std::cout << "\n[HttpClient] POST http://httpbin.org/post\n";
    try
    {
        auto response = Client::Post("http://httpbin.org/post")
            .AddHeader({ .name = "Content-Type", .value = "application/json" })
            .SetBody(R"({"test": true})")
            .Send();

        const auto body = response.GetBodyString();
        if (!body.empty()) Pass(std::format("응답 수신 ({} bytes)", body.size()));
        else               Fail("빈 응답");
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

void TestHttpPut()
{
    std::cout << "\n[HttpClient] PUT http://httpbin.org/put\n";
    try
    {
        auto response = Client::Put("http://httpbin.org/put")
            .AddHeader({ .name = "Content-Type", .value = "application/json" })
            .SetBody(R"({"key": "value"})")
            .Send();

        const auto body = response.GetBodyString();
        if (!body.empty()) Pass(std::format("응답 수신 ({} bytes)", body.size()));
        else               Fail("빈 응답");
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

void TestHttpDelete()
{
    std::cout << "\n[HttpClient] DELETE http://httpbin.org/delete\n";
    try
    {
        auto response = Client::Delete("http://httpbin.org/delete").Send();

        const auto body = response.GetBodyString();
        if (!body.empty()) Pass(std::format("응답 수신 ({} bytes)", body.size()));
        else               Fail("빈 응답");
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

void TestHttpGetAsync()
{
    std::cout << "\n[HttpClient] GET 비동기 (httpbin.org)\n";
    try
    {
        auto future = Client::Get("http://httpbin.org/get").SendAsync();

        if (future.wait_for(10s) != std::future_status::ready)
        {
            Fail("타임아웃 (10s)");
            return;
        }

        const auto response = future.get();
        const auto body = response.GetBodyString();
        if (!body.empty()) Pass(std::format("비동기 응답 수신 ({} bytes)", body.size()));
        else               Fail("빈 응답");
    }
    catch (const std::exception& e) { Fail("예외", e.what()); }
}

// =============================================================================
//  FTP Client (인터넷 + 공개 서버 필요)
// =============================================================================

void TestFtpConnect()
{
    std::cout << "\n[FtpClient] 연결/로그인/디렉토리 조회 (test.rebex.net:21)\n";
    try
    {
        auto ftp = Ftp::FtpClient("test.rebex.net", 21);
        ftp.Connect();
        ftp.Login("demo", "password");
        const auto pwd = ftp.Pwd();
        const auto entries = ftp.List();
        ftp.Quit();

        Pass(std::format("PWD={}, 항목 {}개", pwd, entries.size()));
    }
    catch (const std::exception& e) { Skip("서버 연결 불가", e.what()); }
}

// =============================================================================
//  SFTP Client (인터넷 + 공개 서버 필요)
// =============================================================================

void TestSftpConnect()
{
    std::cout << "\n[SftpClient] 연결/인증/디렉토리 조회 (test.rebex.net:22)\n";
    try
    {
        auto sftp = Sftp::Client("test.rebex.net", 22);
        sftp.Connect();
        sftp.AuthPassword("demo", "password");
        const auto entries = sftp.List("/");
        sftp.Disconnect();

        Pass(std::format("항목 {}개", entries.size()));
    }
    catch (const std::exception& e) { Skip("서버 연결 불가", e.what()); }
}

// =============================================================================
//  main
// =============================================================================

int main()
{
    ::SetConsoleOutputCP(CP_UTF8);

    std::cout << "=============================================\n";
    std::cout << "   Nebula Protocol DLL Runner\n";
    std::cout << "=============================================\n";

    // ── 로컬 루프백 (인터넷 불필요) ──────────────────────────────
    std::cout << "\n--- TCP Socket ---\n";
    TestTcpEchoSync();
    TestTcpIocpEcho();
    TestTcpIocpMultiMessage();
    TestTcpIocpExceptionHandler();

    std::cout << "\n--- UDP Socket ---\n";
    TestUdpEcho();

    std::cout << "\n--- IPC ---\n";
    TestPipeEcho();
    TestSharedMemory();
    TestMessageQueue();
    TestSemaphore();

    std::cout << "\n--- HTTP Server (로컬) ---\n";
    TestHttpServer();

    // ── 네트워크 (인터넷 필요) ───────────────────────────────────
    std::cout << "\n--- TLS Socket ---\n";
    TestTlsConnect();
    TestTlsIocpRead();

    std::cout << "\n--- HTTP Client ---\n";
    TestHttpGet();
    TestHttpPost();
    TestHttpPut();
    TestHttpDelete();
    TestHttpGetAsync();

    std::cout << "\n--- FTP / SFTP (선택, 공개 서버 필요) ---\n";
    TestFtpConnect();
    TestSftpConnect();

    std::cout << "\n=============================================\n";
    std::cout << std::format("  결과: PASS {} / FAIL {} / SKIP {}\n",
                             gPassed, gFailed, gSkipped);
    std::cout << "=============================================\n";
    return gFailed > 0 ? 1 : 0;
}
