# Nebula

C++23 기반의 모듈러 공용 라이브러리. 저수준 I/O(코루틴 기반 비동기), 자료구조, 동시성, 직렬화, 암호, IPC, 네트워크를 한 저장소에서 제공한다.

> ⚠️ **학습·포트폴리오 목적** 프로젝트다. 특히 `Cryptography`의 자체 구현 알고리즘(AES/RSA/BigInt 등)은 **프로덕션 사용에 적합하지 않다**(검증된 백엔드가 아님). 안전이 중요한 용도에는 libsodium/OpenSSL 등을 사용할 것.

## 설계 원칙

- **C++23**, 예외/RTTI 미사용 지향. 실패는 예외 대신 `ne::Result<T, E>`(값 기반)로 표현한다.
- **네임스페이스 3계층**
  - `ne` — 전 모듈 공유 어휘 타입만 (`Result`·`Error`/`OsError`·`Handle`·`Task`·`Type.h` 별칭)
  - `ne::<module>` — 각 모듈의 공개 API (예: `ne::json::Value`, `ne::concurrency::ThreadPool`)
  - `ne::<module>::internal` — 내부 구현(비공개)
  - 자주 쓰는 대표 타입은 루트 별칭 제공: `ne::Logger` = `ne::log::Logger`

## 모듈

| 모듈 | 네임스페이스 | 개요 | 상태 |
|---|---|---|---|
| Base | `ne` | `Result`/`Error`/`Handle`/`Task`(코루틴)/`Type` 별칭·매크로 | ✅ |
| Memory | `ne::memory` | `PoolAllocator`(lock-free free list), `IAllocator`, pmr 어댑터 | ✅ |
| Concurrency | `ne::concurrency` | `ThreadPool`, `MpscQueue`, `SpscQueue` | ✅ |
| Log | `ne::log` (별칭 `ne::Logger`) | 비동기 파일 로거 | ✅ |
| Json | `ne::json` | `Value`/`Object`/`Array`, 자유함수 `Parse`/`Stringify` | ✅ |
| Util | `ne::util` | `Ascii`, `Base64`, `StringFormat` | ✅ |
| Time | `ne::time` | `TimerQueue`(min-heap, 반복 타이머), `SleepFor`/`Deadline` 코루틴 awaitable | ✅ |
| Io | `ne::io` | 비동기 엔진(IOCP/WsaPoll/epoll/io_uring) + `Context`/`Task`/`IStream`/`Socket`/`File` | ✅ |
| Cryptography | `ne::crypto` | Hash(MD5/SHA1/2/3/CRC32)/HMAC/AES/RSA/BigInt/SecureRandom | 🚧 |
| Ipc | `ne::ipc` | `Pipe`/`SharedMemory`/`Semaphore`/`MessageQueue` | 🚧 |
| Network | `ne::network` | `PlainStream`/`TlsStream`(Schannel/OpenSSL)/`Dns`, HTTP/1.1·HTTP/2 통합 API(`ne::network::http`) | 🚧 |

🚧 = 활발히 개발 중(HTTP 등). `find_package` 설치 대상에는 아직 미포함.

## 요구사항

- **CMake ≥ 3.28**
- **컴파일러:** Windows = MSVC(Visual Studio 2022), Linux = GCC/Clang (C++23 지원)
- 소스는 **BOM 없는 UTF-8**. MSVC에서는 `/utf-8`이 필요하며 빌드 스크립트가 자동 적용한다.
- 의존성은 `FetchContent`로 자동 취득: googletest(테스트).
- (Linux) Io는 `liburing`을 요구한다.

## 빌드 & 테스트

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## 설치 & find_package

```bash
cmake --install build --config Debug --prefix /path/to/install
```

설치 후 소비자 프로젝트에서:

```cmake
find_package(Nebula CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE
        Nebula::Util
        Nebula::Json
        Nebula::Log
        Nebula::Io)
```

> 현재 export 범위: 핵심(Base·Memory·Concurrency·Log·Json·Util·Time) + Io. Network/Ipc는 외부 의존성(OpenSSL/liburing) 정리 후 추가 예정.

## 최소 사용 예제

```cpp
#include "Util/Base64.h"
#include "Json/Json.h"
#include "Log/Logger.h"

int main()
{
    const auto encoded = ne::util::Base64::Encode("hello");   // "aGVsbG8="

    auto value = ne::json::Parse(R"({"key":"value"})");
    if (value.IsObject()) { /* ... */ }
    const auto text = ne::json::Stringify(value);

    ne::Logger logger("app.log");   // ne::log::Logger 의 루트 별칭
    logger.Info("started");
}
```

## 라이선스

포트폴리오/학습용. (별도 명시 전까지 무보증)
