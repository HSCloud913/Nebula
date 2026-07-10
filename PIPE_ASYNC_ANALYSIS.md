# Pipe 비동기화 가능성 분석 (작업 5 § 3)

## 현재 구현

- **Linux**: Unix Domain Socket (`AF_UNIX SOCK_STREAM`) 기반. fd = `handle` (`int`)
- **Windows**: Named Pipe (`PIPE_TYPE_BYTE | PIPE_WAIT`) 기반. HANDLE without `FILE_FLAG_OVERLAPPED`

## POSIX (Linux) — IIoEngine::Watch 직접 적용 가능

`Pipe::Impl::handle` 은 `AF_UNIX` 소켓의 file descriptor → epoll 감시 완전 지원.

```
IIoEngine::Watch(static_cast<ne::network::socket_t>(handle), IoEvent::Read, cb)
```

- 데이터 도착 시 EPOLLIN 발생 → recv() 호출로 즉시 읽기 (블로킹 없음)
- `MessageQueue`의 POSIX async 구현(`MqWatchAwaitable`)과 동일한 패턴
- **approach (a) 적용 가능, 추가 인프라 불필요**

구현 예상 코드:

```cpp
// 제안 인터페이스 (후속 작업)
[[nodiscard]] ne::Task<ne::Result<std::vector<std::byte>, ne::OsError>>
    ReadAsync(std::size_t _maxBytes, ne::network::IIoEngine& _engine);
[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>>
    WriteAsync(std::span<const std::byte> _data, ne::network::IIoEngine& _engine);
```

## Windows — 재설계 필요

현재 `PIPE_TYPE_BYTE | PIPE_WAIT`이므로 IOCP 연동이 불가능함.

비동기화하려면:

1. `CreateNamedPipeW` 에 `FILE_FLAG_OVERLAPPED` 추가
2. `ConnectNamedPipe`도 OVERLAPPED 방식으로 변경
3. `ReadFile/WriteFile`에 OVERLAPPED 구조체 사용
4. IOCP에 등록 (FileIocpEngine 패턴 참고)

또는 `MessageQueue`처럼 bridge 스레드 approach (b) 사용 (구현 간단, 스레드 비용 있음).

OVERLAPPED 방식(approach a)을 권장하나 기존 `Listen()/Connect()` 인터페이스의 블로킹
특성 변경이 수반되므로 단독 작업으로 분리 권장.

## 결론

| 플랫폼     | 방식           | 난이도 | 비고                           |
|---------|--------------|-----|------------------------------|
| Linux   | approach (a) | 낮음  | AF_UNIX socket fd → epoll 직접 |
| Windows | approach (a) | 중간  | FILE_FLAG_OVERLAPPED 재설계 필요  |
| Windows | approach (b) | 낮음  | bridge 스레드, 스레드 비용 있음        |

이번 작업(작업 5)에서는 namespace 변경만 적용, 실제 비동기화는 후속 작업으로 분리.
