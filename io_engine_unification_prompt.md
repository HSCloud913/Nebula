# IoEngine Io 모듈 통합 작업

## 작업 환경
- 저장소: https://github.com/HSCloud913/Nebula (main 브랜치)
- 언어: C++23 / CMake 3.28
- 플랫폼: Linux(epoll / io_uring) / Windows(IOCP)
- 예외 금지(-fno-exceptions), RTTI 금지
- 코드 스타일: 기존 Nebula 스타일 유지
  (PascalCase 메서드, _camelCase 매개변수, BEGIN_NS/END_NS,
   [[nodiscard]], NEBULA_NON_COPYABLE_MOVABLE 등)

---

## 목표

현재 `Network/IoEngine/`에 있는 엔진 관련 코드를 전부 `Io/Engine/`으로
이동하고, 세 구현체(EpollEngine / IoUringEngine / IocpEngine)가 모두
`ne::io::IIoEngine` 인터페이스를 상속받는 단일 계층으로 통합한다.

Network 모듈은 엔진 구현체를 더 이상 소유하지 않고, `ne::io::IIoEngine&`를
주입받아서 사용하는 구조로 전환한다.

```
변경 전:
  Network/IoEngine/IIoEngine.h          ← Network 소유
  Network/IoEngine/Epoll/EpollEngine    ← Network 소유
  Network/IoEngine/Iocp/IocpEngine      ← Network 소유
  Io/Engine/IoUring/IoUringEngine       ← 파일 전용, 별도 인터페이스 없음
  Io/Engine/Iocp/IocpEngine             ← 파일 전용, 별도 인터페이스 없음

변경 후:
  Io/Engine/IIoEngine.h                 ← Io 소유, 소켓+파일 공통 인터페이스
  Io/Engine/Epoll/EpollEngine           ← Io 소유 (Network에서 이동)
  Io/Engine/IoUring/IoUringEngine       ← Io 소유 (IIoEngine 상속 추가)
  Io/Engine/Iocp/IocpEngine             ← Io 소유, 파일+소켓 통합
                                           (Network IocpEngine 흡수)
  Network → ne::io::IIoEngine& 주입받아 사용
```

의존 방향:
```
Network → Io → Base
```

---

## 현재 코드 구조 (수정 전)

```
Network/IoEngine/
  IIoEngine.h          ne::network 네임스페이스
                       Watch / Unwatch / RunOnce / SetTimerWheel
  Epoll/
    EpollEngine.h/.cpp Linux epoll, IIoEngine 상속
  Iocp/
    IocpEngine.h/.cpp  Windows IOCP, IIoEngine 상속

Io/Engine/
  Awaitable.h          FileReadAwaitable / FileWriteAwaitable
  IoUring/
    IoUringEngine.h/.cpp  io_uring, 독자 인터페이스(IIoEngine 미상속)
                           내부 스레드, ctx->handle.resume() 직접 호출
  Iocp/
    IocpEngine.h/.cpp    Windows IOCP 파일 전용, 독자 인터페이스
                          내부 스레드, ctx->handle.resume() 직접 호출
```

### Network에서 IIoEngine을 사용하는 모든 지점

```
Network/IoEngine/IIoEngine.h          → 정의 위치 (이동 대상)
Network/IoEngine/Awaitable.h          → RecvAwaitable / SendAwaitable
                                         (IIoEngine& 참조, 이동 대상)
Network/Stream/PlainStream.h/.cpp     → IIoEngine& 멤버
Network/Stream/Tls/TlsStream.h/.cpp  → IIoEngine& 멤버
Network/Stream/Ssh/SshStream.h/.cpp  → IIoEngine& 멤버
Network/Protocol/Http/Http1/Client.h → IIoEngine& 멤버
Network/Protocol/Http/Http1/Server.h → IIoEngine& 멤버
Network/Protocol/Ftp/Client.h        → IIoEngine& 멤버
Network/Protocol/Sftp/Client.h       → IIoEngine& 멤버
Network/Test/test_timer_engine_integration.cpp → EpollEngine/IocpEngine 직접 사용
```

---

## Step 1 — Io/Engine/IIoEngine.h 신규 작성

### 목적
소켓 이벤트(Watch/Unwatch/RunOnce)와 파일 I/O 완료(파일 엔진 통합)를
모두 처리할 수 있는 통합 인터페이스를 `ne::io` 네임스페이스에 정의한다.

### 신규 파일
`Io/Engine/IIoEngine.h`

### 내용

기존 `Network/IoEngine/IIoEngine.h`를 기반으로 작성하되
네임스페이스를 `ne::io`로 변경하고 타입 참조를 조정한다.

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include "Result.h"
#include "Error.h"
#include "Type.h"
#include "TimerWheel.h"

// 소켓 fd 타입 — IoType.h에서 가져오거나 직접 선언
// Network에서 분리하므로 socket_t를 Io/IoType.h에 추가해야 함
#include "IoType.h"

BEGIN_NS(ne::io)
    struct IoEvent
    {
        static constexpr uint32_t Read   = 1u << 0;
        static constexpr uint32_t Write  = 1u << 1;
        static constexpr uint32_t Error  = 1u << 2;
        static constexpr uint32_t HangUp = 1u << 3;
    };

    using IoCallback = std::function<void(socket_t _fd, uint32_t _events)>;

    class IIoEngine
    {
        NEBULA_NON_COPYABLE_MOVABLE(IIoEngine)
    public:
        IIoEngine() = default;
        virtual ~IIoEngine() = default;

    public:
        [[nodiscard]] virtual ne::Result<void, ne::OsError>
            Watch(socket_t _fd, uint32_t _events, IoCallback _cb) = 0;
        [[nodiscard]] virtual ne::Result<void, ne::OsError>
            Unwatch(socket_t _fd) = 0;
        [[nodiscard]] virtual ne::Result<void, ne::OsError>
            RunOnce(int_t _timeoutMs = -1) = 0;
        virtual void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept = 0;
    };
END_NS
```

### Io/IoType.h 수정

기존 `file_t` 외에 `socket_t`를 추가한다.
Network/NetworkType.h의 socket_t 정의와 동일하게 맞춘다.

```cpp
// 추가
#if defined(_WIN32)
    using socket_t = SOCKET;
    inline const auto InvalidSocket = INVALID_SOCKET;
#elif defined(IS_POSIX)
    using socket_t = int;
    inline constexpr socket_t InvalidSocket = -1;
#endif
```

### 완료 기준
- [ ] `Io/Engine/IIoEngine.h` 작성 완료
- [ ] `ne::io::IIoEngine` 인터페이스가 `ne::network::IIoEngine`과 동일한 API 보유
- [ ] `Io/IoType.h`에 socket_t 추가

---

## Step 2 — EpollEngine을 Io/Engine/Epoll/로 이동

### 목적
`Network/IoEngine/Epoll/EpollEngine`을 `Io/Engine/Epoll/`로 이동하고
`ne::io::IIoEngine`을 상속하도록 변경한다.

### 파일 이동
```
Network/IoEngine/Epoll/EpollEngine.h  →  Io/Engine/Epoll/EpollEngine.h
Network/IoEngine/Epoll/EpollEngine.cpp → Io/Engine/Epoll/EpollEngine.cpp
```

### 변경 내용

#### EpollEngine.h

```cpp
#pragma once
#if defined(IS_POSIX)
#include <unordered_map>
#include "Engine/IIoEngine.h"    // ne::io::IIoEngine (변경)
#include "Handle.h"

BEGIN_NS(ne::io)                  // 네임스페이스 변경 ne::network → ne::io
    class EpollEngine final : public IIoEngine
    {
        ...
        // 기존 내용 그대로, 네임스페이스만 변경
    };
END_NS
#endif
```

#### EpollEngine.cpp

- `BEGIN_NS(ne::io)` / `END_NS` 로 변경
- `#include "EpollEngine.h"` 경로 조정
- `ne::network::IoEvent::*` → `ne::io::IoEvent::*` 로 변경
- 내부 구현 로직은 변경 없음

### 완료 기준
- [ ] `Io/Engine/Epoll/EpollEngine.h/.cpp` 작성
- [ ] `ne::io::EpollEngine`이 `ne::io::IIoEngine` 상속
- [ ] `Network/IoEngine/Epoll/` 디렉토리 삭제

---

## Step 3 — IoUringEngine을 IIoEngine 상속으로 개편

### 목적
기존 `Io/Engine/IoUring/IoUringEngine`은 파일 전용 독자 인터페이스였다.
`ne::io::IIoEngine`을 상속하고 소켓 이벤트(Watch/Unwatch/RunOnce)도
처리할 수 있는 통합 엔진으로 확장한다.

파일 완료 시 `ctx->handle.resume()` 직접 호출 문제도 이 단계에서 해결한다.
eventfd를 통해 이벤트 루프(RunOnce) 스레드에서만 resume이 발생하도록 한다.

### 수정 파일
- `Io/Engine/IoUring/IoUringEngine.h`
- `Io/Engine/IoUring/IoUringEngine.cpp`

### 설계

io_uring은 소켓과 파일 양쪽을 처리할 수 있다.
단, 현재 코드베이스의 소켓 처리는 epoll 기반이므로
IoUringEngine은 두 가지 모드를 지원한다.

```
소켓 이벤트 처리: io_uring의 IORING_OP_POLL_ADD 사용
                  또는 내부적으로 epoll_fd를 함께 관리
파일 I/O 처리:   기존 io_uring read/write SQE 사용
```

1차 구현에서는 복잡도를 낮추기 위해
소켓 이벤트를 epoll_fd를 내부에 포함하는 방식으로 처리한다
(epoll을 io_uring SQE로 감싸지 않고, epoll_fd를 직접 보유).

#### IoUringEngine.h 변경

```cpp
#pragma once
#if defined(IS_POSIX)
#include <coroutine>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <liburing.h>
#include "Engine/IIoEngine.h"          // 추가
#include "Queue/MpscQueue.h"
#include "Handle.h"
#include "TimerWheel.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
    struct FileIoCtx { ... };          // 기존 유지

    class IoUringEngine final : public IIoEngine   // IIoEngine 상속 추가
    {
        NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)
    public:
        explicit IoUringEngine(unsigned _queueDepth = 256) noexcept;
        ~IoUringEngine();

    private:
        // 파일 I/O용 io_uring
        io_uring ring{};
        std::thread thread;
        std::atomic<bool> running{ false };
        bool valid{ false };

        // 소켓 이벤트용 epoll (내부 포함)
        using EpollFdHandle = ne::Handle<
            int_t,
            decltype([](const int_t _fd) { ::close(_fd); }),
            -1>;
        EpollFdHandle epollFd;
        std::unordered_map<socket_t, WatchEntry> watches;

        // 파일 I/O 완료를 이벤트 루프 스레드로 전달하는 큐 + eventfd
        ne::concurrency::MpscQueue<std::coroutine_handle<>> completionQueue;
        int completionEventFd{ -1 };

        ne::time::TimerWheel* timerWheel{ nullptr };

    public:
        // IIoEngine 구현 — 소켓 이벤트
        [[nodiscard]] ne::Result<void, ne::OsError>
            Watch(socket_t _fd, uint32_t _events, IoCallback _cb) override;
        [[nodiscard]] ne::Result<void, ne::OsError>
            Unwatch(socket_t _fd) override;
        [[nodiscard]] ne::Result<void, ne::OsError>
            RunOnce(int_t _timeoutMs = -1) override;
        void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override
            { timerWheel = _wheel; }

        // 파일 I/O 제출 (FileReadAwaitable / FileWriteAwaitable 에서 호출)
        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitRead(int _fd, void* _buf, std::size_t _len,
                       std::size_t _offset, FileIoCtx* _ctx) noexcept;
        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitWrite(int _fd, const void* _buf, std::size_t _len,
                        std::size_t _offset, FileIoCtx* _ctx) noexcept;

        [[nodiscard]] bool_t IsValid() const noexcept { return valid; }

    private:
        void ThreadLoop();      // io_uring CQE 처리 전용 스레드
        void DrainCompletions() noexcept;  // RunOnce에서 호출
    };
END_NS
#endif
```

#### IoUringEngine.cpp 핵심 변경 포인트

생성자에서 epoll_fd와 completionEventFd 초기화:
```cpp
epollFd = EpollFdHandle(::epoll_create1(EPOLL_CLOEXEC));
completionEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

// completionEventFd를 epoll에 등록 — io_uring 완료 신호 수신용
epoll_event ev{};
ev.events  = EPOLLIN;
ev.data.fd = completionEventFd;
::epoll_ctl(epollFd.Get(), EPOLL_CTL_ADD, completionEventFd, &ev);
```

Watch / Unwatch:
```cpp
// EpollEngine.cpp의 Watch/Unwatch 로직을 그대로 이식
// epollFd를 사용해 소켓 fd 등록/해제
```

RunOnce:
```cpp
ne::Result<void, ne::OsError> IoUringEngine::RunOnce(const int_t _timeoutMs)
{
    // TimerWheel 기반 effective timeout 계산 (EpollEngine과 동일)
    int_t effectiveTimeout = _timeoutMs;
    if (timerWheel) { ... }

    epoll_event events[64];
    const int count = ::epoll_wait(epollFd.Get(), events, 64, effectiveTimeout);
    if (count == -1) { ... EINTR 처리 ... }

    for (int i = 0; i < count; ++i)
    {
        const int fd = events[i].data.fd;

        if (fd == completionEventFd)
        {
            // io_uring 파일 완료 통지 — DrainCompletions로 handle resume
            DrainCompletions();
        }
        else
        {
            // 소켓 이벤트 — 기존 watches 콜백 디스패치
            if (const auto it = watches.find(fd); it != watches.end())
                it->second.callback(fd, FromEpollEvents(events[i].events));
        }
    }

    if (timerWheel) timerWheel->Tick();
    return ne::Result<void, ne::OsError>::Ok();
}
```

ThreadLoop (io_uring 전용 스레드):
```cpp
// io_uring CQE 완료 시 ctx->handle.resume() 대신 큐에 push + eventfd 신호
completionQueue.Enqueue(ctx->handle);
uint64_t val = 1;
(void)::write(completionEventFd, &val, sizeof(val));
```

DrainCompletions:
```cpp
void IoUringEngine::DrainCompletions() noexcept
{
    uint64_t val{};
    (void)::read(completionEventFd, &val, sizeof(val));

    std::coroutine_handle<> handle;
    while (completionQueue.Dequeue(handle))
        if (handle && !handle.done()) handle.resume();
}
```

### 완료 기준
- [ ] `IoUringEngine`이 `ne::io::IIoEngine` 상속
- [ ] Watch/Unwatch/RunOnce 구현 (소켓 이벤트 처리)
- [ ] 파일 I/O 완료 resume이 RunOnce 스레드에서만 발생
- [ ] 기존 Io/Test/test_asyncfile.cpp 통과

---

## Step 4 — IocpEngine 통합 (Windows)

### 목적
현재 Windows에는 엔진이 두 개다.
- `Network/IoEngine/Iocp/IocpEngine` — 소켓 전용
- `Io/Engine/Iocp/IocpEngine(FileIocpEngine)` — 파일 전용

두 개를 하나의 `Io/Engine/Iocp/IocpEngine`으로 통합한다.
소켓 핸들과 파일 핸들 모두 같은 완료 포트에 등록해서 처리한다.

### 파일 처리
```
Network/IoEngine/Iocp/IocpEngine.h/.cpp  삭제 (통합됨)
Io/Engine/Iocp/IocpEngine.h/.cpp         전면 재작성 (통합본)
```

### 설계

```cpp
BEGIN_NS(ne::io)
    // 소켓 완료 컨텍스트 (기존 Network::IocpContext 흡수)
    struct SocketIocpCtx
    {
        OVERLAPPED overlapped{};  // 반드시 첫 멤버
        socket_t fd{};
        uint32_t events{};
        IoCallback callback;
    };

    // 파일 완료 컨텍스트 (기존 FileIocpCtx 유지)
    struct FileIocpCtx
    {
        OVERLAPPED overlapped{};  // 반드시 첫 멤버
        std::coroutine_handle<> handle;
        ne::Result<std::size_t, ne::OsError> result{ ... };
    };

    // 완료 포트 타입 식별 키
    static constexpr ULONG_PTR SocketCompletionKey = 1;
    static constexpr ULONG_PTR FileCompletionKey   = 2;

    class IocpEngine final : public IIoEngine
    {
    public:
        explicit IocpEngine(ulong_t _concurrentThreads = 0) noexcept;
        ~IocpEngine();

    private:
        HANDLE iocpHandle{ INVALID_HANDLE_VALUE };
        std::unordered_map<socket_t, SocketIocpCtx*> socketContexts;
        ne::time::TimerWheel* timerWheel{ nullptr };
        std::thread fileThread;   // 파일 I/O 완료 처리 전용 스레드
        std::atomic<bool> running{ false };

    public:
        // IIoEngine — 소켓 이벤트
        [[nodiscard]] ne::Result<void, ne::OsError>
            Watch(socket_t _fd, uint32_t _events, IoCallback _cb) override;
        [[nodiscard]] ne::Result<void, ne::OsError>
            Unwatch(socket_t _fd) override;
        [[nodiscard]] ne::Result<void, ne::OsError>
            RunOnce(int_t _timeoutMs = -1) override;
        void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override
            { timerWheel = _wheel; }

        // 파일 핸들 등록 (AsyncFile::Create/Open에서 호출)
        [[nodiscard]] ne::Result<void, ne::OsError>
            RegisterFile(HANDLE _fileHandle) noexcept;

        // 파일 I/O 제출
        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitRead(HANDLE _fd, void* _buf, std::size_t _len,
                       std::size_t _offset, FileIocpCtx* _ctx) noexcept;
        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitWrite(HANDLE _fd, const void* _buf, std::size_t _len,
                        std::size_t _offset, FileIocpCtx* _ctx) noexcept;

        [[nodiscard]] bool_t IsValid() const noexcept
            { return iocpHandle != INVALID_HANDLE_VALUE; }
        [[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle; }

    private:
        void FileThreadLoop();  // 파일 완료만 전담 처리
    };
END_NS
```

RunOnce에서 소켓 완료와 파일 완료를 completionKey로 구분:
```cpp
ne::Result<void, ne::OsError> IocpEngine::RunOnce(const int_t _timeoutMs)
{
    DWORD bytes = 0; ULONG_PTR key = 0; OVERLAPPED* ov = nullptr;
    // timeout 계산 (TimerWheel 고려)
    ...
    const BOOL ok = ::GetQueuedCompletionStatus(iocpHandle, &bytes, &key, &ov, timeout);

    if (!ov) { if (timerWheel) timerWheel->Tick(); return Ok(); }

    if (key == SocketCompletionKey)
    {
        // 소켓 이벤트 — 기존 Network::IocpEngine 처리 로직 그대로 이식
        auto* ctx = reinterpret_cast<SocketIocpCtx*>(ov);
        ctx->callback(ctx->fd, ctx->events);
    }
    else if (key == FileCompletionKey)
    {
        // 파일 I/O 완료 — 이벤트 루프 스레드에서 resume
        auto* ctx = reinterpret_cast<FileIocpCtx*>(ov);
        if (!ok) ctx->result = Error(...);
        else     ctx->result = Ok(static_cast<std::size_t>(bytes));
        if (ctx->handle && !ctx->handle.done()) ctx->handle.resume();
    }

    if (timerWheel) timerWheel->Tick();
    return Ok();
}
```

Windows는 IOCP가 단일 완료 포트로 소켓/파일을 모두 처리하므로
Linux의 eventfd 메커니즘이 필요 없다. 파일 완료도 RunOnce의
GetQueuedCompletionStatus에서 처리되어 자연스럽게 이벤트 루프
스레드에서 resume된다.

별도 fileThread는 RunOnce 외부에서 파일 완료를 빠르게 처리해야 하는
경우를 위한 선택적 구성요소다. 1차 구현에서는 fileThread 없이
RunOnce에서 FileCompletionKey 처리만으로 충분하다.

### 완료 기준
- [ ] `Io/Engine/Iocp/IocpEngine`이 `ne::io::IIoEngine` 상속
- [ ] 소켓 이벤트(Watch/Unwatch/RunOnce)와 파일 I/O(SubmitRead/Write) 통합
- [ ] `Network/IoEngine/Iocp/` 디렉토리 삭제
- [ ] Windows 빌드 통과

---

## Step 5 — Io/Engine/Awaitable.h 통합

### 목적
현재 두 곳에 Awaitable이 나뉘어 있다.
- `Network/IoEngine/Awaitable.h` — RecvAwaitable / SendAwaitable (소켓)
- `Io/Engine/Awaitable.h` — FileReadAwaitable / FileWriteAwaitable (파일)

모두 `Io/Engine/Awaitable.h` 하나로 통합한다.

### 수정 파일
`Io/Engine/Awaitable.h` (기존 파일 확장)

### 내용

```cpp
#pragma once
#include <coroutine>
#include <optional>
#include <span>
#include "Engine/IIoEngine.h"
#include "IoType.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)

    // ── 소켓 이벤트 Awaitable (Network에서 이동) ────────────────
    class RecvAwaitable
    {
    public:
        RecvAwaitable(socket_t _fd, IIoEngine& _engine) noexcept;
        ...
        // 기존 Network/IoEngine/Awaitable.h의 RecvAwaitable 내용 이식
        // ne::network → ne::io 네임스페이스만 변경
    };

    class SendAwaitable
    {
        // 동일하게 이식
    };

    // ── 파일 I/O Awaitable (기존 내용 유지) ──────────────────────
#if defined(IS_POSIX)
    class FileReadAwaitable  { ... };   // IoUringEngine& → IIoEngine& 로 변경 불가
                                        // (SubmitRead는 IIoEngine에 없음)
                                        // IoUringEngine& 그대로 유지
    class FileWriteAwaitable { ... };
#elif defined(_WIN32)
    class FileReadAwaitable  { ... };   // IocpEngine& 그대로 유지
    class FileWriteAwaitable { ... };
#endif

END_NS
```

주의: FileReadAwaitable / FileWriteAwaitable은 `SubmitRead/SubmitWrite`를
호출하는데, 이 메서드는 `IIoEngine` 인터페이스에 없다 (플랫폼별 메서드).
따라서 FileReadAwaitable은 여전히 구체 타입(IoUringEngine / IocpEngine)을
참조해야 한다. 억지로 인터페이스화하지 않는다.

`Network/IoEngine/Awaitable.h` 파일은 삭제한다.

### 완료 기준
- [ ] `Io/Engine/Awaitable.h`에 RecvAwaitable / SendAwaitable 포함
- [ ] `Network/IoEngine/Awaitable.h` 삭제
- [ ] 네임스페이스가 `ne::io`로 통일됨

---

## Step 6 — Network 모듈에서 IoEngine 제거 및 참조 교체

### 목적
Network 모듈에서 IIoEngine 정의를 제거하고
`Io/Engine/IIoEngine.h`의 `ne::io::IIoEngine`을 참조하도록 전환한다.

### 삭제 파일
```
Network/IoEngine/IIoEngine.h         삭제
Network/IoEngine/Awaitable.h         삭제 (Step 5에서 처리)
Network/IoEngine/Epoll/EpollEngine.h 삭제 (Step 2에서 이동)
Network/IoEngine/Epoll/EpollEngine.cpp 삭제
Network/IoEngine/Iocp/IocpEngine.h   삭제 (Step 4에서 흡수)
Network/IoEngine/Iocp/IocpEngine.cpp 삭제
Network/IoEngine/                    디렉토리 삭제
```

### 수정 파일 — include 경로 및 네임스페이스 변경

아래 파일 전체에서 다음 치환을 적용한다:

```
#include "IoEngine/IIoEngine.h"      → #include "Engine/IIoEngine.h"
#include "IoEngine/Awaitable.h"      → #include "Engine/Awaitable.h"
ne::network::IIoEngine               → ne::io::IIoEngine
ne::network::IoEvent                 → ne::io::IoEvent
ne::network::RecvAwaitable           → ne::io::RecvAwaitable
ne::network::SendAwaitable           → ne::io::SendAwaitable
```

대상 파일:
- `Network/Stream/PlainStream.h/.cpp`
- `Network/Stream/Tls/TlsStream.h/.cpp`
- `Network/Stream/Ssh/SshStream.h/.cpp`
- `Network/Protocol/Http/Http1/Client.h/.cpp`
- `Network/Protocol/Http/Http1/Server.h/.cpp`
- `Network/Protocol/Ftp/Client.h/.cpp`
- `Network/Protocol/Sftp/Client.h/.cpp`
- `Network/Test/test_timer_engine_integration.cpp`
  → `EpollEngine.h` include 경로 변경, `ne::network::EpollEngine` → `ne::io::EpollEngine`

### CMakeLists 수정

#### Network/CMakeLists.txt

```cmake
# 삭제
#   IoEngine/Epoll/EpollEngine.cpp
#   IoEngine/Iocp/IocpEngine.cpp

# target_include_directories에 Io 모듈 경로 추가
target_include_directories(NebulaNetwork PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Base
    ${CMAKE_SOURCE_DIR}/Io     # 추가: ne::io::IIoEngine 참조용
)
# NebulaIo 링크는 이미 있음 — 유지
```

#### Io/CMakeLists.txt

```cmake
# 추가
if(UNIX)
    list(APPEND SOURCES Engine/Epoll/EpollEngine.cpp)
    list(APPEND SOURCES Engine/IoUring/IoUringEngine.cpp)
endif()
if(WIN32)
    list(APPEND SOURCES Engine/Iocp/IocpEngine.cpp)
endif()

# Concurrency 의존 추가 (MpscQueue)
target_link_libraries(NebulaIo PUBLIC
    NebulaBase
    NebulaConcurrency   # 추가
    NebulaTime          # 추가 (TimerWheel)
)
```

### 완료 기준
- [ ] Network 모듈 내 `Network/IoEngine/` 디렉토리 완전 삭제
- [ ] Network 전체 빌드 통과 (ne::io::IIoEngine 참조)
- [ ] 기존 테스트 전체 회귀 없음

---

## Step 7 — 테스트 정비 및 통합 검증

### Network/Test/test_timer_engine_integration.cpp 수정
- `ne::network::EpollEngine` → `ne::io::EpollEngine`
- `ne::network::IocpEngine` → `ne::io::IocpEngine`
- include 경로 수정

### 신규 통합 테스트 작성
`Io/Test/test_engine_integration.cpp`

검증 시나리오:
1. `EpollEngine` (또는 `IoUringEngine`)으로 소켓 이벤트와 파일 I/O를 동시에 처리
2. `co_await AsyncFile::Read()` 완료 후 같은 코루틴에서 `co_await RecvAwaitable`을
   호출해도 race condition이 없음을 `thread_id` 비교로 검증
3. 모든 `handle.resume()`이 `RunOnce()` 호출 스레드에서 발생함을 확인

### 완료 기준
- [ ] 모든 기존 테스트 통과
- [ ] 통합 테스트에서 resume 스레드 ID가 이벤트 루프 스레드와 일치
- [ ] Linux: EpollEngine / IoUringEngine 양쪽 검증
- [ ] Windows: IocpEngine 검증

---

## 최종 폴더 구조 (작업 완료 후)

```
Io/
├── CMakeLists.txt
├── IoType.h               socket_t / file_t 통합
├── Engine/
│   ├── IIoEngine.h        ne::io::IIoEngine (통합 인터페이스)
│   ├── Awaitable.h        RecvAwaitable / SendAwaitable /
│   │                      FileReadAwaitable / FileWriteAwaitable
│   ├── Epoll/
│   │   ├── EpollEngine.h
│   │   └── EpollEngine.cpp
│   ├── IoUring/
│   │   ├── IoUringEngine.h  (IIoEngine 상속, 소켓+파일)
│   │   └── IoUringEngine.cpp
│   └── Iocp/
│       ├── IocpEngine.h     (IIoEngine 상속, 소켓+파일 통합)
│       └── IocpEngine.cpp
├── File/
│   ├── IIoFile.h
│   ├── AsyncFile.h/.cpp
│   └── MemoryMapFile.h/.cpp
└── Test/
    ├── test_asyncfile.cpp
    ├── test_mmap.cpp
    └── test_engine_integration.cpp    (신규)

Network/
├── CMakeLists.txt         IoEngine 소스 항목 제거, Io 경로 추가
├── NetworkType.h
├── Buffer/
├── Socket/
├── Stream/                ne::io::IIoEngine& 참조
├── Protocol/              ne::io::IIoEngine& 참조
└── Test/
    └── test_timer_engine_integration.cpp  (경로 수정)
```

---

## 주의사항

1. Step 순서 준수. 각 Step은 빌드 성공 상태로 커밋한다.
   Step 2(EpollEngine 이동) → Step 6(Network 참조 교체)을 같이 묶어서
   커밋하면 중간에 빌드가 깨진다. 반드시 순서대로 진행한다.

2. `ne::io::IoType.h`의 `socket_t` 추가 시 `Network/NetworkType.h`의
   정의와 타입이 일치하는지 반드시 확인한다
   (Windows: SOCKET, POSIX: int).

3. `FileReadAwaitable`은 `IIoEngine` 인터페이스가 아닌 구체 엔진 타입을
   참조한다 (`SubmitRead`가 인터페이스에 없으므로). 이것은 의도된 설계다.
   억지로 인터페이스에 넣지 않는다.

4. Io/CMakeLists.txt에 NebulaConcurrency, NebulaTime 의존이 추가된다.
   루트 CMakeLists.txt의 add_subdirectory 순서가
   `Concurrency → Time → Io` 이어야 한다. 현재 순서를 확인한다.

5. Windows에서 소켓 fd와 파일 HANDLE을 같은 IOCP에 등록할 때
   completionKey(SocketCompletionKey / FileCompletionKey)로 구분한다.
   completionKey 값이 충돌하지 않도록 헤더에 명시적 상수로 선언한다.
