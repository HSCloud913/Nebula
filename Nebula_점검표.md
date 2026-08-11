# Nebula 라이브러리 점검표 (Io 제외)

> 대상: `Base · Memory · Concurrency · Log · Json · Util · Time · Cryptography · Ipc · Network`
> 목적: ① 구현 수준을 타 라이브러리 기준선과 비교 ② 실제 사용(소비자 관점) 불편 요소를 미세한 것까지 점검
> 사용법: 각 프로젝트마다 A(공통)를 한 바퀴 돌린 뒤 B(프로젝트별)로 내려간다. `[ ]` → `[x]`.
>
> **판정 범례(2026-07-22 코드 대조 결과):** `✅ 확인` = 진단이 사실 · `⚠️ 부분` = 부분적으로만 사실 · `❌ 아님/부재` = 사실 아님이거나 대상이 없음(또는 stale) · `[x]` = 현재 상태 확인 완료 · `[ ]` 유지 = 미점검이거나 아직 실행하지 않은 조치/계획 항목.

---

## 🔄 진행 현황 · 남은 작업 (2026-07-24 갱신)

> 아래 A~D의 개별 진단(✅/⚠️/❌)은 **2026-07-22 최초 감사 시점 기준**이며, 이후 수행한 개선을 여기에 요약한다.
> **빌드 상태:** 의도된 http WIP(`NebulaIpc`·`NebulaNetworkTest`)를 제외한 전 모듈·테스트 통과. 커밋은 미실행(사용자가 직접).

### ✅ 완료

**Phase 1 — 네임스페이스 3계층 (A-0, A-1)**
- `BEGIN_NS`/`END_NS` 매크로 전면 폐기 → 명시적 `namespace X {}` (전 파일, `Type.h` 매크로 정의 제거)
- 공개 API를 `ne::<module>`로: Concurrency(`ThreadPool`→`ne::concurrency`, 모듈 내부 분열 해소), Log→`ne::log`(+ 루트 별칭 `ne::Logger`), Util→`ne::util`, Json→`ne::json` + 역할기반 rename(`Value`/`Object`/`Array`, 파사드→자유함수 `Parse`/`Stringify`, 파일 `Value.{h,cpp}`)
- 내부 격리 `ne::<module>::internal`: `SspiWrapper`·`TlsMessageBuffer`, Json `SkipWhitespace`

**Phase 2 — 빌드·소비 (A-7, A-8)**
- `install()`/`export()` + `NebulaConfig.cmake` + `find_package(Nebula)`(핵심+Io, `Nebula::X` ALIAS, `BUILD/INSTALL_INTERFACE`) — 별도 소비자 스모크 테스트로 실증
- CMake 타깃명 `${PROJECT_NAME}` 통일 + `Time/CMakeLists.txt`의 `NebulaUtil` 링크 오타 수정
- 공개 헤더 42개 self-contained 검증(`ICommand`/`IObserver` 인클루드 보강), README 재작성
- (부수) MSVC 코루틴 `void_t`→`void` 버그·`StringFormat` 인클루드 수정

**Phase 3 — 안전·정합성 (A-3, B-5, B-8, D-2)**
- Json 예외→Result: `As*()` 무예외(assert)화 + `TryAs*()` Result 접근자 · 숫자 오버플로 STRING→INVALID · `TryParse`로 파싱 실패 위치/사유(`ParseError`) 제공
- Crypto 안전: AES/RSA/BigInt 프로덕션 위험 `@warning` · `SecureRandom` 실패 안전화(`Fill`→`bool`, `Next` fail-closed) · `ConstantTimeEquals` · `HMACKey::Verify`(상수시간)
- Io 에러 경계 통일(D-2): 날것 `ne::Result<X, IoError>` → `IoResult<X>` 표기 단일화
- (부수) `Timeout` 콤비네이터 `timerRacer` 수명 버그 수정 → Io 테스트 43/43 통과

**Phase 4 — API 품질 (A-1/A-2, B-1~B-4, B-6)**
- `Result` 모나딕 조합자 `Map`/`AndThen`/`OrElse`/`ValueOr`/`Context` (C++23 deducing this, move-only 지원) [B-1]
- 인터페이스 non-copyable(`ICommand`/`IInvoker`/`IObserver`/`ISubject`) + `IStream`/`Json` `[[nodiscard]]` 보강 [A-2]
- Log sink 재설계: `ISink` + `ConsoleSink`/`FileSink`/`RotatingFileSink` + 멀티sink + 동기 `Flush()` [B-4]
- Concurrency 큐 단일 생산자/소비자 위반 디버그 가드 `SingleRoleGuard` [B-3]
- Base64 디코드 안전화(`Result` 반환·잘못된 입력 거부) + 패딩 옵션 [B-6]
- PoolAllocator 디버그 훅(더블프리·풀 밖/비정렬 포인터 감지) [B-2]

**Phase 5 — Io-Network 확장 (D-3/D-4/D-5 step 2·3·4)**
- `when_any` 콤비네이터: `WhenAny(ctx, vector<Task<T>>)` → 최초 완료 태스크의 `{index, value}` 반환, 진 태스크는 파괴로 취소. 공유 race 프리미티브 `RaceState`/`AwaitDecision`를 `Io/Coroutine/Internal/Race.h`(`ne::io::internal`)로 추출하고 `Timeout` 리팩터 [D-3/D-5 step2]
- 엔진 팩토리 `MakeEngine(EngineType)` + 최상위 `Runtime` 파사드(engine+TimerQueue+Context RAII, `BlockOn(Task<T>)`로 한 줄 동기 실행) [D-4/D-5 step3]
- `AsyncListener`: 서버측 accept 헬퍼(create+bind+listen 팩토리, `Accept()` 코루틴, `LocalPort()`) [D-3/D-5 step2]
- 프로토콜 Client 한 줄 파사드: `http_1::FetchGet`/`FetchPost`(자체 Runtime으로 dns→connect→(tls)→요청/응답을 한 줄에). 기존 `Client`/`Server` 무수정, 신규 `Fetch.{h,cpp}` [D-4/D-5 step4]
- 신규 Io 테스트 7개(WhenAny 2·Runtime 4·AsyncListener 1) 통과, `Fetch`는 `NebulaNetwork` 오브젝트로 컴파일 확인(E2E는 NetworkTest WIP 링크 문제로 보류)

### 🗺️ 남은 작업

- **Phase 3 잔여** — Network OpenSSL TLS 호스트네임 검증 격차(`SSL_set1_host`·기본 신뢰저장소 로딩) [B-10] *(http WIP과 조율)*
- **Phase 5 잔여** [D-3/D-5 step4·5] — WebSocket 프레이밍(Upgrade 후 전환) · HTTP/2·SFTP 다중화 계층 *(http WIP 영역과 직결 — 그쪽 안정화 후)* · Client keep-alive/커넥션 풀 · `AsyncListener` 위 동시 처리(연결별 코루틴 분기) spawn 모델
- **Phase 6 — 완성도** — 벤치마크 [A-6] · Sanitizer(ASan/UBSan/TSan)+CI [A-9] · 복붙 사용 예제 [A-8]
- **Crypto 심화(별도)** [B-8] — AES-GCM · RSA OAEP/PSS+상수시간 패딩 · BigInt 상수시간 ModPow · 비밀 버퍼 zeroization
- **경량 잔여 감사** — `noexcept` 전수 [A-2] · is/has 접두사 [A-1] · `string_view` 저장/moved-from 계약 [A-4] · MpscQueue 노드 풀링·블로킹 API [B-3/A-6] · Ipc create-vs-open·orphan 정리·플랫폼차 문서화 [B-9]

---

## A. 전 프로젝트 공통 점검

### A-0. 네임스페이스 설계 ★현재 최우선 개선 영역
> 업계 기준선: **std**(어휘 타입은 `std`에 평평하게 두고 응집된 도메인만 하위로: `chrono`/`ranges`/`pmr`) · **Boost**(라이브러리 = `boost::<lib>` 한 단계 통일, 내부는 `detail`) · **Abseil**(얕은 중첩 + 내부 전용 `*_internal` ns) · **folly/LLVM**(대체로 평평 + `detail`)

**현재 진단 — 중첩 기준이 없어 두 방식이 섞임:**
- [x] 루트 `ne`에 도메인 API가 섞여 있다: `ne::Logger`(Log)·`ne::Json`(Json)·`ne::Base64`(Util)·`ne::ThreadPool`(Concurrency). — **✅ 확인:** 네 타입 모두 `BEGIN_NS(ne)`로 루트 선언(Logger.h:15, Json.h:7, Base64.h:4, ThreadPool.h:14).
- [x] 반면 같은 성격인데 하위로 간 것들: `ne::memory::`·`ne::concurrency::`·`ne::time::`·`ne::network::`·`ne::crypto::`. — **✅ 확인:** 다섯 하위 ns 모두 실재(추가로 `ne::network::dns`/`http`/`http_1` 중첩도 존재).
- [x] **Concurrency 모듈 내부 분열(가장 뚜렷한 증거):** 같은 타깃 안에서 `ThreadPool.h`는 `BEGIN_NS(ne)`, `MpscQueue.h`/`SpscQueue.h`는 `BEGIN_NS(ne::concurrency)` → 소비자가 `ne::ThreadPool`과 `ne::concurrency::MpscQueue`를 한 모듈에서 섞어 써야 함. — **✅ 확인:** ThreadPool.h:14 vs MpscQueue.h:9 / SpscQueue.h:12.
- [x] **내부(비공개) 구현 격리용 `detail`/`internal` 네임스페이스가 아예 없음** — TlsStream 내부인 `SspiWrapper`·`TlsMessageBuffer`가 공개 `ne::network`에 그대로 노출. — **✅ 확인:** `namespace detail/internal` 프로젝트 전체 0건, SspiWrapper.h:14·TlsMessageBuffer.h:13 모두 `ne::network`.
- [x] **`BEGIN_NS`/`END_NS` 매크로 자체가 문제.** ⓐ `END_NS`가 닫는 괄호에 이름을 안 남겨 중첩 시 어느 블록이 닫히는지 추적 어려움(Google이 `}  // namespace foo`를 강제하는 이유), ⓑ IDE·정적분석·clang-format이 매크로 뒤 네임스페이스 블록을 제대로 인식/들여쓰기 못 함, ⓒ `namespace ne::io { }` 같은 C++17 중첩 문법을 매크로로는 자연스럽게 못 씀. — **✅ 확인:** Base/Type.h:49-50 정의, 130개 파일이 사용. 이미 Io/Context/ContextPool.h·Ipc/Pipe.h·MessageQueue.h는 raw `namespace ne::x {}` 사용 → 매크로/명시 방식 혼재.

**목표 규칙(권장 3계층) — 아래를 기준으로 항목 점검:**
- [x] **0계층 `ne` = 어휘 타입만.** `Result`·`Error`/`OsError`·`Handle`·`Type.h` 별칭·매크로 등 전 모듈 공유 기반만 유지. (루트에 어휘 타입을 둔 건 std가 `std::expected`를 `std`에 두는 것과 같은 올바른 판단 — 유지.) — **⚠️ 현재 미준수:** 어휘 타입 외 도메인 API(Logger/Json/Base64/ThreadPool)가 루트에 혼재.
- [x] **1계층 `ne::<module>` = 모든 공개 API를 예외 없이.** `ne::log::Logger`, `ne::json::Json`, `ne::util::Base64`, `ne::concurrency::ThreadPool`, `ne::memory::PoolAllocator` … "공개 타입 = `ne::모듈명::`"이라는 단일 규칙으로 예측 가능하게. — **⚠️ 현재 미준수:** Log/Json/Util 및 ThreadPool이 규칙 밖(루트)에 있음.
- [x] **2계층 `ne::<module>::internal` = 내부 구현.** `SspiWrapper`·`TlsMessageBuffer`·파서 헬퍼 등. 중첩 깊이는 `ne::network::dns` 수준(최대 3단계)까지만. — **⚠️ 미도입:** internal 계층 자체가 없음.
- [ ] 이름이 길어지는 불편은 **문서에서 별칭 제공**으로 흡수 (`namespace nej = ne::json;`, std의 `namespace fs = std::filesystem;` 방식). *(조치 미실행)*
- [ ] **`BEGIN_NS`/`END_NS` 폐기 → 명시적 `namespace` 사용(확정).** C++17 중첩 네임스페이스 문법으로 전환
  ```cpp
  // 기존
  BEGIN_NS(ne::io)
      class Socket { ... };
  END_NS

  // 전환 후
  namespace ne::io
  {
      class Socket { ... };
  }  // namespace ne::io
  ```
  이점: IDE/clang-format/정적분석이 네임스페이스를 정확히 인식, 닫는 지점이 자명, 별도 매크로 헤더 의존 제거. `Type.h`에서 `BEGIN_NS`/`END_NS` 정의를 삭제하고 전 파일 일괄 치환(정규식 `BEGIN_NS\((.+)\)` → `namespace $1 {`, `END_NS` → `}` + 이름 주석 수기 보정). 이때 A-0의 3계층 재배치를 함께 반영하면 파일당 한 번의 수정으로 끝남.
- [ ] **breaking change 관리:** Logger·Json·Util·ThreadPool 이동은 기존 소비 코드에 파괴적. 외부 배포(`find_package`) 붙기 전인 지금 규칙을 확정하고, 옮길 거면 한 번에. *(조치 미실행)*

### A-1. API 표면 · 일관성
- [x] 네임스페이스 규칙 — **A-0에서 확정한 규칙**을 전 프로젝트가 예외 없이 따르는가. — **❌ 아니오:** A-0대로 불일치.
- [ ] 타입/함수 명명(PascalCase 메서드, `_` 접두 매개변수)이 전 프로젝트에서 일관된가. *(전수 미점검)*
- [x] `Is*()` / `As*()` / `Ok()` / `Error()` 같은 관용 동사가 프로젝트별로 뜻이 흔들리지 않는가. — **⚠️ 부분:** Ok/Error/IsOk는 일관(Result), 그러나 `As*()`는 Json에서 예외를 던지는 접근자로 의미가 흔들림(B-5).
- [x] 반환 타입 정책이 통일돼 있는가 (`Result<T>` vs 예외 vs `bool` + out-param vs `std::optional`). — **❌ 미통일:** Result / 예외(Json As*, Logger Open) / bool(큐) 혼재.
- [x] 커스텀 typedef(`string_t`, `bool_t`, `void_t` …)가 **공개 API 시그니처**에 노출되는가. — **✅ 확인(노출됨):** `ne::uint16_t`·`bool_t` 등이 공개 시그니처에 광범위(Socket.h:58·PlainStream.h:57·Http1/Client.h:23 등).
- [x] `int16_t`/`uint16_t`를 `Base/Type.h`에서 재정의 → `<cstdint>`와 충돌·혼선 소지. — **⚠️ 부분:** Type.h:106-107서 `short`/`unsigned short` 별칭으로 정의. `ne` 네임스페이스라 하드 충돌은 없으나 정확폭 미보장 + 표준명 섀도잉 위험.
- [ ] bool_t 변수의 일관성을 위해 is, has 같은 적절한 접두사를 붙이는가. (std::atomic이나 std::optional 같은 항목들도 당연히 적용되어야 함) *(전수 미점검)*
- [ ] bool_t 를 반환하는 함수의 일관성을 위해 Is, Has 같은 적절한 접두사를 붙이는가. *(전수 미점검)*

### A-2. const / noexcept / 특수멤버 정확성
- [x] `[[nodiscard]]`가 "무시하면 버그"인 반환값(특히 `Result`, 할당 포인터, `future`)에 빠짐없이 붙었는가. — **⚠️ 부분:** nodiscard가 69개 헤더에서 사용되나 "빠짐없이" 여부(특히 모든 `Result` 반환 함수)는 전수 미검증.
- [ ] `noexcept`가 실제로 예외를 던질 수 없는 함수에만 붙었는가 (내부에서 `std::format`/할당하는 함수에 잘못 붙지 않았는지). *(전수 미점검)*
- [ ] const 오버로드(값 접근자)가 값/참조 반환을 일관되게 하는가. *(미점검)*
- [x] 복사/이동 정책이 매크로(`NEBULA_NON_COPYABLE_MOVABLE` 등)로 **명시적으로 선언**돼 있는가. — **⚠️ 부분:** 매크로 존재(Type.h:74·86)하나 일부 인터페이스(ICommand/IObserver)는 암묵 특수멤버에 의존(비-non-copyable).
- [x] 소유권을 가진 타입(포인터/핸들 보유)이 rule of 5를 지키는가, 아니면 명시적으로 non-copyable인가. — **⚠️ 부분:** `Handle`은 이동전용 RAII 확인(Handle.h:24-86). 전 소유 타입 전수는 미검증.
- [ ] `explicit` 생성자 규칙 — 단일 인자 생성자에서 의도치 않은 암묵 변환이 없는가. *(미점검)*

### A-3. 에러 처리 계약
- [x] `Result`/`Error`를 쓰는 경로와 `assert`로 끝내는 경로의 경계가 문서화돼 있는가. — **⚠️ 부분:** `Value()`/`Error()` 오상태 `assert`(릴리스 UB) 확인(Result.h:52-74). 계약의 공개 문서화는 없음(README 7줄).
- [x] **라이브러리 전체가 "예외 없음"을 표방하는데, 실제로 예외를 던지는 지점이 있는가.** — **✅ 확인:** Json `As*()`가 `std::bad_variant_access`, Logger `Open`이 `fs::filesystem_error` try/catch. 철학 불일치 실재.
- [x] `OsError`의 context chain(`Context()`)이 실패 경로에서 실제로 누적되는가. — **⚠️ 부분:** `Error::Context()` 체이닝 구현 확인(Error.h:36-42), `OsError` 오버로드 존재. 실패 경로별 실제 호출 여부는 표본.
- [x] 부분 실패(예: 일부 바이트만 쓰기/읽기) 표현이 `Result`로 손실 없이 전달되는가. — **✅ 확인:** short read/write가 `IoResult`로 그대로 전달, SendAll 루프 처리(TlsStream.cpp:35-50).

### A-4. 리소스 · 수명 관리
- [x] 모든 OS 핸들/메모리/스레드가 RAII로 감싸져 예외·조기 return 경로에서도 누수가 없는가. — **⚠️ 부분:** 핵심 타입 RAII 확인(Handle·Logger·ThreadPool), 전수 미검증.
- [ ] `string_view_t`를 받는 API가 **호출 이후 저장하지 않음**을 보장/문서화하는가 (dangling view 위험). *(미점검)*
- [x] 소멸자에서 블로킹/조인/드레인하는 타입(`Logger`, `ThreadPool`, `MpscQueue`)이 소멸 순서 의존성을 문서화하는가. — **⚠️ 부분:** Logger는 조인+드레인 문서화·구현 확인(Logger.cpp:39-46). ThreadPool/큐의 소멸 순서 의존성 문서는 미비.
- [ ] 이동 후(moved-from) 객체의 유효 상태가 정의돼 있는가. *(미점검)*

### A-5. 스레드 안전성
- [x] 각 공개 타입의 스레드 안전 계약이 헤더에 명시돼 있는가. — **⚠️ 부분:** 큐(Mpsc/Spsc)는 명시, TimerQueue·IStream은 미명시.
- [x] `memory_order` 사용이 정당한가. lock-free 경로는 리뷰 + TSan 필수. — **⚠️ 부분:** PoolAllocator 태그드 CAS 등 구현은 확인, 그러나 TSan/CI 부재로 실증 안 됨.
- [x] false sharing 방지(`alignas(64)`)가 실제 경합 필드에 적용됐고, 반대로 과도하게 남발되지 않았는가. — **✅ 확인:** SpscQueue.h:37-38 read/writePos에 alignas(64), 과남발 징후 없음.
- [x] ABA·lost-wakeup 대비가 주석 주장대로 코드에서 성립하는가. — **✅ 확인:** PoolAllocator 32bit index+32bit tag 패킹 CAS로 ABA 방어(PoolAllocator.h:41-45).
- [ ] 원자 플래그(`isStop`/`isRunning`/`isShutdown`)가 double-checked 패턴에서 올바른 순서로 읽고 쓰이는가. *(표본만, 전수 미점검)*

### A-6. 성능
- [x] 핫패스에서 불필요한 복사/힙할당이 없는가 (`MpscQueue`의 노드당 `new` 등). — **⚠️ 부분:** MpscQueue 노드당 `new/delete` 확인(할당 부담). 그 외 핫패스는 표본.
- [x] 작은 객체에 대한 SBO/풀 재사용 여지가 검토됐는가. — **⚠️ 미검토:** 큐 노드 풀링/재사용 없음.
- [x] **벤치마크가 존재하는가.** — **❌ 부재:** benchmark 타깃/디렉터리 0건.
- [x] 컴파일 시간·헤더 포함 비용 (`windows.h`가 소비자에게 새는지). — **⚠️ 부분:** Io(Ws2_32/Mswsock/ntdll)·Crypto(bcrypt)가 시스템 라이브러리를 PUBLIC 링크(A-7). windows.h 헤더 누출 여부는 표본.

### A-7. 빌드 · 소비(consume) 편의  ★실사용 최대 마찰 지점
- [x] **`install()` / `export()` / package-config가 없음** → 소비자가 `find_package(Nebula)`로 못 붙임. — **✅ 확인(부재):** `install(`/`export(`/`Config.cmake` 0건. `find_package`는 third-party(OpenSSL 등)용뿐.
- [x] **CMake 타깃 이름이 혼용됨** (`${PROJECT_NAME}` vs 하드코딩 `NebulaMemory`/`NebulaUtil`/`NebulaNetwork`). — **✅ 확인:** Memory/Util/Network는 하드코딩, Time/Concurrency/Io/Crypto/Json/Log/Ipc는 `${PROJECT_NAME}`.
- [x] **의심 오타 확인:** `Time/CMakeLists.txt`의 `target_link_libraries(NebulaUtil PUBLIC NebulaBase)` — Time이 아니라 Util 타깃을 링크. — **✅ 확인(실버그):** Time/CMakeLists.txt:23이 `NebulaUtil`을 링크(→ `NebulaTime`/`${PROJECT_NAME}`이어야 함).
- [ ] 각 헤더가 **self-contained**한가. *(개별 컴파일 검증 미실시)*
- [x] `target_include_directories`가 `PUBLIC`으로 리포 루트를 노출하는 방식이 설치 시에도 성립하는가. — **✅ 확인(문제 실재):** 루트 CMakeLists.txt:42 `include_directories(${CMAKE_SOURCE_DIR})` + 각 모듈 raw 경로 PUBLIC, `BUILD_INTERFACE`/`INSTALL_INTERFACE` 0건.
- [x] 링크 의존성이 최소인가 — Ipc가 `NebulaIo`에 의존하는 등. — **⚠️ 부분:** Ipc→NebulaIo 의존은 Pipe/MessageQueue 비동기에 실제 사용(정당). 단 SharedMemory/Semaphore엔 불필요한데 모듈 전체가 의존.
- [x] 플랫폼별 시스템 라이브러리 링크(`Ws2_32`, `bcrypt`, `Secur32`, `pthread`/`rt`)가 `PRIVATE`로 올바르게 캡슐화됐는가. — **⚠️ 부분:** Network/Ipc는 PRIVATE, 그러나 Io(Ws2_32/Mswsock/ntdll)·Crypto(bcrypt)는 의도적 PUBLIC.

### A-8. 문서 · 예제
- [x] README가 실제 내용을 반영하는가. — **✅ 확인(미흡):** README 7줄, 모듈 목록·빌드법·예제 없음.
- [x] 각 공개 타입에 **복붙 가능한 최소 사용 예제**가 있는가. — **❌ 부재.**
- [x] Doxygen 주석의 서술이 현재 구현과 일치하는가. — **⚠️ 부분:** `TimerWheel`→`TimerQueue` 리네이밍으로 이름/구현 괴리는 해소(2026-08-11). `SecureRandom` 주석 "사실상 실패 안 함" 은 남아 있음.
- [x] 스레드 안전/수명/에러 계약이 "코드에만 있고 문서에 없는" 상태가 아닌가. — **⚠️ 확인:** 상당수 계약이 코드/주석에만 존재, 공개 문서 없음.

### A-9. 테스트 · 이식성
- [x] 정상 경로뿐 아니라 **경계/실패 경로** 테스트가 있는가. — **⚠️ 부분:** crypto 벡터·http 통합 테스트 등 존재하나 경계/실패 경로 전반은 표본.
- [x] Sanitizer 통과 여부 (ASan/UBSan, lock-free는 TSan). CI에서 자동화됐는가. — **❌ 부재:** `.github/workflows` 없음, `-fsanitize` 플래그 0건.
- [x] Windows/Linux **양 플랫폼**에서 같은 테스트가 도는가. — **⚠️ 부분:** 코드에 POSIX 분기 존재하나 리눅스 CI 실행 검증 부재.
- [x] 테스트가 구현 세부가 아닌 공개 계약을 검증하는가. — **⚠️ 부분(표본).**

---

## B. 프로젝트별 점검 (타 라이브러리 비교 기준선 포함)

### B-1. Base — `Result` · `Error` · `Type` · `Interface` · `Handle` · `Coroutine`
> 비교 기준선: `std::expected`(C++23), `tl::expected`, `std::error_code`, GSL
- [x] `Result<T,E>`가 `std::expected`와 왜/어떻게 다른지 근거가 있는가. — **⚠️ 부분:** `std::variant` 기반 자체 타입, 모나딕 연산 부재. 표준 대신 쓰는 근거는 문서화 없음.
- [x] `Result`에 `.map()`/`.and_then()`/`.value_or()` 같은 조합자가 없어 호출부가 장황해지지 않는가. — **✅ 확인(없음):** 조합자 부재(Result.h:45-74).
- [x] `Value()`/`Error()` 오상태 접근이 `assert`뿐 — 릴리스 빌드 오용 시 UB. — **✅ 확인:** `assert` 후 `get_if` 널 역참조 UB(Result.h:52-74, void_t 특수화 99-109 동일).
- [x] `Error`의 `Context()`가 `Error&`만 반환 → `Result`와 매끄럽게 이어지는 헬퍼가 있는가. — **⚠️ 부분:** Context()는 `Error&` 체이닝(OsError는 `OsError&` 오버로드). Result와 매끄럽게 잇는 별도 헬퍼는 없음.
- [x] `Handle`이 RAII로 OS 핸들 소유권을 안전히 감싸고 이동 전용인가. — **✅ 확인**(Handle.h:24-86, 복사삭제·이동정의·dtor Close).
- [x] `Interface/`의 순수 가상 인터페이스들이 가상 소멸자를 갖고 non-copyable인가. — **⚠️ 부분:** 가상 소멸자는 모두 있음, non-copyable은 `ISingleton`만(ICommand/IObserver 미선언).

### B-2. Memory — `IAllocator` · `PoolAllocator`
> 비교 기준선: `std::pmr::monotonic_buffer_resource`/`unsynchronized_pool_resource`, boost.pool, mimalloc
- [x] `PoolAllocator`의 정렬(512/4096) 지원이 실제로 요청 정렬을 만족하는가. — **✅ 확인:** base를 aligned 할당 + blockSize를 alignment 배수로 라운드(PoolAllocator.cpp:39-47) → 모든 블록 시작 정렬됨.
- [x] 풀 고갈 시 동작이 정의돼 있는가. `Available()`와 실제 할당 성공이 일치하는가. — **⚠️ 부분:** 고갈 시 nullptr(정의됨, 폴백 없음). 단 `Available()`는 relaxed 근사치라 동시성에서 실제 상태와 일시 불일치.
- [x] `pmr::memory_resource` 어댑터의 `do_is_equal`이 포인터 동일성만 보는데, 제약을 문서화했는가. — **⚠️ 부분:** 포인터 동일성만(IAllocator.h:35), 그로 인한 제약 문서화는 미비.
- [x] 할당 통계/디버그 훅(더블프리·경계침범 감지)이 있는가. — **❌ 부재:** available 카운터뿐, 더블프리/경계 검증 없음.
- [x] over-alignment(요청 정렬 > 풀 설정 정렬) 요청 시 안전하게 실패하는가. — **✅ 확인:** cpp:70 `_align > alignment` → nullptr.
- [x] TSan/스트레스 테스트로 lock-free free list의 ABA 방어가 실증됐는가. — **⚠️ 부분:** 태그드 인덱스 구현은 확인, 그러나 TSan/CI 부재로 실증 안 됨.

### B-3. Concurrency — `ThreadPool` · `MpscQueue` · `SpscQueue`
> 비교 기준선: `moodycamel::ConcurrentQueue`, folly MPMC/ProducerConsumerQueue, TBB
- [x] `ThreadPool::Enqueue`가 shutdown 후 **무효 future**를 반환. — **⚠️ 부분:** 무효 future 반환 확인(ThreadPool.h:48). `future.valid()` 검사 유도 문서/예제는 없음.
- [x] 태스크 내부 예외가 `future`로 안전히 전파되는가. — **✅ 확인:** `packaged_task` 경로로 전파.
- [x] `MpscQueue`가 노드마다 `new/delete`. — **✅ 확인:** Michael-Scott 리스트, 원소당 1회 힙할당(MpscQueue.h:35·44·56·69). 블록/할당자 주입 여지.
- [x] `SpscQueue`가 링버퍼면 용량/빈 상태 반환 규약이 명확한가, false sharing 분리가 됐는가. — **✅ 확인:** full/empty를 bool로 반환, alignas(64) 분리, 용량 2의 거듭제곱 assert.
- [x] 소비자 단일 제약이 헤더에 명시됨 — 위반 시 방어(디버그 assert)가 있는가. — **⚠️ 부분:** 문서 명시 O, 위반 감지용 디버그 assert 없음.
- [x] 블로킹 vs 논블로킹 API 구분이 소비자에게 선택 가능한가. — **❌ 부재:** 논블로킹 bool API만 노출.

### B-4. Log — `Logger`
> 비교 기준선: spdlog, glog, quill
- [x] **sink 추상화**가 있는가. — **❌ 부재:** 단일 `ofstream` 파일 전용.
- [x] 포맷 커스터마이즈(패턴, 필드 순서)가 가능한가. — **❌ 부재:** 하드코딩 포맷(Logger.cpp:123).
- [x] **로그 로테이션**과 디스크 가득참 대응이 있는가. — **❌ 부재:** append 1회 open, 로테이션/디스크풀 처리 없음.
- [x] 레벨 필터가 호출 지점에서 **인자 평가 전에** 걸러지는가. — **⚠️ 부분:** 레벨 체크는 선행하나 API가 완성된 `string_t`를 받아 포매팅 비용은 호출자가 이미 지불(지연 포맷 없음).
- [x] 백프레셔 정책 — 큐 폭주 시 드롭/블록/무한증가 중 무엇인가. — **❌ 무한증가·미문서:** MpscQueue 무제한 `new`, 드롭/블록 없음.
- [x] **크래시 세이프티** — flush 정책/강제 flush API가 있는가. — **⚠️ 부분:** `>= FATAL`만 auto-flush, 강제 flush 공개 API 없음.
- [x] 소멸 시 백엔드 스레드 조인과 잔여 큐 드레인이 보장되는가. — **✅ 확인:** join 후 FlushPending 드레인(Logger.cpp:39-46).

### B-5. Json — `Json` · `JsonValue`
> 비교 기준선: nlohmann/json, RapidJSON, simdjson, Glaze
- [x] 숫자 5분류가 소비자에게 **실용적 이득**인가, 아니면 마찰인가. — **⚠️ 부분:** 5분류 실재(JsonValue.h:18-23), 접근 편의 함수 부재로 마찰 소지.
- [x] `As*()`의 `std::bad_variant_access` 예외가 "예외 없음" 철학과 충돌. — **✅ 확인(A-3 연동):** Result 반환 접근자 없음.
- [x] `Parse`가 실패 시 `INVALID`만 반환 → **오류 위치/사유**를 못 줌. — **✅ 확인:** 모든 실패 `{}` 반환. (게다가 숫자 오버플로 시 STRING `"out of range"` 반환하는 잠재버그)
- [x] UTF-8·이스케이프·서러게이트 페어·제어문자 처리가 RFC 8259를 만족하는가. — **⚠️ 부분(비준수):** `\uXXXX`는 처리하나 서러게이트 페어 미결합(invalid UTF-8 생성), raw `\t` 허용 등.
- [x] 부동소수 round-trip 정밀도가 보존되는가. — **❌ 미보존:** 자릿수 누적·거듭 `*10/÷10` 방식, `strtod` 미사용.
- [x] 큰 문서에서 깊이 제한이 있는가. — **✅ 확인:** `MaxParseDepth=256`(JsonValue.h:212, cpp:43).
- [x] `Stringify`의 이스케이프/비-ASCII 출력 정책이 일관된가. — **⚠️ 부분:** 내부 일관하나 parse/stringify 비대칭(`\t`, 0x7F 취급 상이), ≥0x80 raw 통과.

### B-6. Util — `Ascii` · `Base64` · `StringFormat`
> 비교 기준선: `std::format`, abseil strings, fmt
- [x] `Base64`가 표준(RFC 4648)과 URL-safe 변형, 패딩 유무를 모두 지원/구분하는가. — **⚠️ 부분:** 표준/URL-safe 있으나 패딩 on/off 독립 옵션 없음(변형에 종속).
- [x] `Base64` 디코드가 잘못된 입력에 대해 안전히 실패(`Result`)하는가. — **❌ 아님:** 검증 없이 `string_t` 반환, 잘못된 문자를 npos→byte 캐스트해 무음 오염.
- [x] `StringFormat`이 `std::format`과 중복이면 존재 이유가 명확한가. — **❌ 중복 아님:** 포매팅이 아니라 Trim/Lower/Upper/인코딩(MBCS/WCS/UTF-8) 변환 유틸. 이름이 오해 소지.
- [x] `Ascii` 유틸이 로캘 비의존인가. — **✅ 확인:** Ascii는 고정 테이블·산술 변환으로 로캘 무관. (단 StringFormat의 Lower/UpperTransform은 `std::tolower` 사용 — 함정 잔존.)

### B-7. Time — `TimerQueue` · `Sleep` · `HttpDate`
> 비교 기준선: Asio timers, folly HHWheelTimer, libuv timers
- [x] `TimerQueue`의 해상도/최대 지연 범위와 tick 비용이 문서화됐는가. — **⚠️ 부분:** 해상도(ms)·복잡도(O(expired·log n)) 문서화, 최대 지연 범위 미문서. (이름/구현 괴리는 2026-08-11 리네이밍으로 해소.)
- [x] 타이머 **취소** API가 있고, 발화 직전 취소의 경쟁 조건이 정의돼 있는가. — **✅ 확인:** `Cancel(id)` 존재, cancel/fire 모두 mutex 하 `live.erase(id)`로 경쟁 결정적.
- [x] 스레드 안전성 — 어느 스레드에서 add/cancel/advance 가능한지 명시. — **⚠️ 부분:** 전 연산 mutex 보호(안전)하나 스레드 계약 문서 없음. 콜백은 락 밖 실행.
- [x] `TimerAwaitable`이 취소·예외 상황에서 코루틴을 안전히 재개/파괴하는가. — **✅ 확인:** `~Awaitable`가 `timerId!=0`이면 타이머 취소(Awaitable.h:33)로 파괴된 프레임 재개 방지.
- [x] 단조시계(`steady_clock`) 사용으로 시스템 시각 변경에 견고한가. — **✅ 확인:** steady_clock 일관 사용.

### B-8. Cryptography — `Hash` · `HMAC` · `AES` · `RSA` · `BigInt` · `SecureRandom`
> 비교 기준선: libsodium, OpenSSL EVP, Botan, BoringSSL
- [x] ⚠️ **자체 구현 암호는 프로덕션 사용 위험**을 README/헤더에 명시하는가. 백엔드(WinCNG/OpenSSL) vs 자체 구현 구분 표기. — **❌ 미명시:** 경고 전무. Hash/HMAC/AES/RSA/BigInt 전부 자체구현, `SecureRandom`만 OS CSPRNG.
- [x] `AES` 기본 모드/패딩/IV 취급이 안전한가. — **⚠️/❌:** GCM(인증모드) 없음, ECB 무경고 노출, IV 재사용 방지·랜덤 IV 헬퍼 없음, PKCS7 언패드 비상수시간(패딩 오라클 소지).
- [x] `RSA` 패딩이 OAEP/PSS인가. — **❌ 위험:** PKCS#1 v1.5만, OAEP/PSS 없음, 서명/검증 API 자체 부재, 패딩 검사 비상수시간(Bleichenbacher), `RSA_512` 제공.
- [x] `BigInt` 모듈러 지수승이 **상수시간**인가. — **❌ 아님:** square-and-multiply 데이터 의존 분기(BigInt.cpp:317-330), 비밀 지수 누출, 주석에 약점 미표기.
- [x] `SecureRandom`의 엔트로피 소스가 OS CSPRNG인가, 실패 시 안전 실패하는가. — **⚠️ 부분:** 소스는 BCryptGenRandom/getrandom(맞음). 그러나 반환값 무시·실패 시 무음 저엔트로피 출력(안전 실패 아님).
- [x] 키/평문 버퍼의 사용 후 **zeroization**이 있는가. — **❌ 부재:** 비밀 소거 없음(AES 키가 평문 `string_t`).
- [x] `HMAC`·비교 연산이 상수시간 비교를 쓰는가. — **❌ 아님:** 상수시간 비교 유틸 없음, HMAC Verify 함수도 없음(비교는 호출자 `==`에 위임).
- [x] 해시 `Wrapper`/`Factory` 추상화가 알고리즘 교체를 매끄럽게 하되 오버헤드가 과하지 않은가. — **✅/⚠️:** 깔끔한 Factory(enum switch)로 교체 용이, 단 호출당 힙할당(HMAC은 Generate마다 2회).
- [x] 테스트가 **공식 테스트 벡터**로 검증되는가. — **⚠️ 부분:** AES(FIPS197/SP800-38A)·HMAC(RFC2202/4231)·Hash KAT 있음, RSA는 벡터 없이 라운드트립만.

### B-9. Ipc — `Pipe` · `SharedMemory` · `Semaphore` · `MessageQueue`
> 비교 기준선: boost.interprocess, POSIX/Win32 원시 API
- [x] 이름 있는 객체의 **이름 충돌·권한**(생성 vs 개방) 규약이 명확한가. — **❌ 불명확:** create-vs-open 구분 없음(항상 O_CREAT/CreateXxx), O_EXCL/CREATE_NEW 없음, 기존 객체 무음 attach.
- [x] 프로세스 크래시 후 **orphan 리소스 정리**를 다루는가. — **⚠️ 부분:** Windows는 커널 refcount로 안전. POSIX shm/sem은 소멸자에서만 unlink → 크래시 시 orphan 누수(Pipe/MQ는 bind 전 unlink로 자가치유).
- [x] `SharedMemory` 매핑 크기·정렬·수명이 정의돼 있는가. — **⚠️ 부분:** 크기/페이지정렬 OK, 그러나 소멸마다 unlink(소유권/refcount 개념 없음), open 시 크기 검증 없음.
- [x] Windows/POSIX 의미 차이를 문서화했는가. — **❌ 미문서:** 헤더에 플랫폼 차이(이름 접두 `/`, unlink 시점, Pipe 구현 차이) 설명 없음.
- [x] `MessageQueue`의 최대 메시지 크기/큐 깊이/블로킹 정책이 명시돼 있는가. — **⚠️ 부분:** 최대 64KB 명시, 큐 깊이는 SEQPACKET 전환으로 포기, 타임아웃/논블로킹 옵션 없음.
- [x] Ipc가 `NebulaIo`에 의존하는 이유가 타당한가. — **✅/⚠️:** Pipe/MessageQueue의 비동기(IEngine/Awaitable)에 실제 사용(타당). 단 SharedMemory/Semaphore엔 불필요한데 모듈 전체 의존.

### B-10. Network — `PlainStream` · `TlsStream`(Schannel/OpenSSL) · `Dns`
> 비교 기준선: Asio, cpp-httplib, libcurl
- [x] ⚠️ **TLS 인증서 검증이 기본 ON**인가. — **✅ 확인:** `verifyPeer{true}` 기본(TlsStream.h:21), 양 백엔드 배선.
- [x] **SNI**·호스트네임 검증·신뢰 저장소 지정이 가능한가. — **⚠️ 부분:** SNI 양쪽 OK. **OpenSSL 경로는 `SSL_set1_host`(호스트네임 검증) 및 기본 신뢰저장소 로딩 누락** → 보안 격차.
- [x] Schannel 경로와 OpenSSL 경로가 **동일한 보안 기본값**을 갖는가. — **❌ 편차:** OpenSSL이 호스트네임 검증·기본 신뢰저장소·강암호 하드닝 미비(Schannel은 처리).
- [x] 연결/읽기/쓰기 **타임아웃**과 취소가 API로 제공되는가. — **⚠️ 부분:** 취소는 `stop_token` 전반 지원. 타임아웃은 per-call 인자 없이 `Timeout()` 콤비네이터 래핑으로만.
- [x] `Dns`가 비동기/취소 가능한가, IPv6·다중 레코드·실패 폴백을 다루는가. — **⚠️ 부분:** 비동기·IPv6·다중레코드·연결폴백 OK. **취소 불가**(Resolve에 stop_token 없음, blocking getaddrinfo).
- [x] 부분 읽기/쓰기와 TLS 재협상·close_notify 처리가 올바른가. — **✅ 확인:** SendAll 루프, `SEC_E_INCOMPLETE_MESSAGE`/close_notify/재협상 레코드 처리(TlsStream.cpp:509-530).
- [x] 스트림 계약(수명, 스레드 안전, 재사용)이 문서화됐는가. — **⚠️ 부분:** 비동기/EOF/컴포지션 수명 문서화, IStream 스레드안전 명시 없음(Context 레벨 문서). 1연결=1객체.

---

## D. Io-Network 계층 설계 점검 (비동기 + 코루틴)
> 계층 스택: `Level 0 엔진(IEngine) → Level 1 실행기(Context) → Level 2 코루틴(Task/Awaitable/IoResult) → IStream(확장 seam) → Level 3 프로토콜 → Level 4 파사드`
> 비교 기준선: Asio/Boost.Asio · cppcoro · seastar · libuv

### D-1. 계층 분리 — 이미 잘 된 부분(회귀 방지용 점검)
- [x] 의존 방향이 단방향(하향)으로 유지되는가 — Network는 Io를 알지만 Io는 Network를 모른다. — **✅ 확인:** Io 하위 `#include "Network/` 0건, Dns도 Io 엔진 비의존.
- [x] 엔진 다양성이 `IEngine` 뒤로 완전히 은닉되는가. — **✅ 확인:** 라이브러리 소스에 엔진 분기 없음(구체 엔진명은 테스트 하네스에만).
- [x] reactor를 synthetic completion으로 감싸 proactor로 통일한 계약이 유지되는가. — **✅ 확인**(IEngine.h:22-26).
- [x] **코루틴 수명 안전 계약**이 확장 코드에서도 성립하는가. — **✅ 확인:** `Task` 무조건 `handle.destroy()`(Task.h:76·164) + `Awaitable` `isAbandoned` heap 이관(Awaitable.h:37-43).
- [x] `IStream`이 컴포지션 seam으로 유지되는가. — **✅ 확인:** TlsStream이 `PlainStream transport` 보유(상속 아님), PlainStream은 `final`.

### D-2. 계층 관점의 미결(개선 대상)
- [x] **에러 경계 이원화 해소** — 대부분 `IoResult<T>`인데 `Close()`는 `Result<void_t, IoError>`, zero-copy provider는 `OsError`. — **✅ 확인(문제 실재):** 3원화(async IoResult / 동기 Result<void_t,IoError> for Close / OS 경계 OsError). *해소 조치는 미실행.*
- [x] **thread-per-core 부재** — `IoContextPool`이 주석만 존재. — **❌ stale:** `ContextPool`이 실제 thread-per-core executor로 구현됨(ContextPool.h:40-46, cpp:22-89). 진단이 낡음.
- [x] **엔진 팩토리 부재** — 호출자가 구체 엔진을 직접 생성. — **✅ 해소:** 명명 팩토리 `io::MakeEngine(EngineType)` + 최상위 `io::Runtime` 파사드 추가(Phase 5). `ContextPool`도 동일 선택 로직 사용.

### D-3. 프로토콜 확장 용이성 (WS / HTTP(S) / FTP(S) / SFTP)
- [x] **HTTP/1·HTTPS:** `IStream` 위 파서/직렬화 + `TlsConfig` ALPN(`h2`/`http/1.1`). — **✅ 성립:** ALPN 후보 존재(양 백엔드), HTTP/1 Parser·Client·Server가 이미 구현됨(신규 `Network/Protocol/`).
- [x] **WebSocket:** Upgrade 후 프레이밍 전환 + ping/pong용 `when_any`/`Timeout`. — **⚠️ 부분(개선):** `Timeout`·`WhenAny` 콤비네이터 모두 존재(Timeout.h·WhenAny.h). WS 프레이밍은 여전히 미구현.
- [x] **FTP(S):** control+data 이중 연결 오케스트레이션. — **❌ 부재.**
- [x] **SFTP:** SSH 단일 전송 위 **다중 채널 다중화** 계층. — **❌ 부재.**
- [x] **HTTP/2:** 스트림 다중화 계층. — **❌ 부재**(ALPN 토대만 존재).
- [x] **서버측 공통:** `AsyncListener`(accept 루프) 헬퍼. — **✅ 개선:** `io::AsyncListener` 추가(create+bind+listen 팩토리 + `Accept()` 코루틴 + `LocalPort()`). 단 연결별 동시 처리(spawn)는 호출자 몫 — `http_1::Server::Serve`는 순차 처리.

### D-4. 최종 사용자 한 줄 호출 (ergonomics)
- [x] **Runtime(Level 4) 도입** — engine+context+timer+thread를 RAII 하나로. — **✅ 완료:** `io::Runtime`(engine+TimerQueue+Context RAII, `BlockOn(Task<T>)`로 한 줄 동기 실행) 추가. 다중코어는 `ContextPool` 유지.
- [x] **엔진 팩토리** — 플랫폼 best 엔진 자동 선택. — **✅ 완료:** 명명 팩토리 `io::MakeEngine(EngineType)` 추가(`ContextPool`/`Runtime` 공용 진입점).
- [x] **프로토콜 클라이언트 파사드** — dns→connect→tls→프로토콜을 한 줄로. — **✅ 개선:** `http_1::FetchGet`/`FetchPost`가 자체 Runtime으로 한 줄 호출 완성(HTTPS는 TlsStream ALPN 라우팅). keep-alive 없음(1요청=1연결)은 잔여.
- [x] 위 세 조각이 **순수 additive**인가. — **✅ 가능:** 계층이 단방향으로 깨끗(D-1).
- [x] 에러 전파가 한 줄 호출에서도 값 기반(`IoResult`)으로 이어지는가. — **✅/⚠️:** IoResult/HttpResult 값 기반. 단 `Close` 등 경계 통일(D-2)은 미조치.

### D-5. Io-Network 착수 순서(권장)  *(실행 로드맵)*
- [x] 1. 에러 경계 통일(IoResult로, provider·`Close()` 포함) — **완료(Phase 3 D-2)**
- [x] 2. `AsyncListener` + `when_any`/`Timeout` 콤비네이터 + `Context::SleepFor` 노출 — **완료:** `WhenAny`(+`Internal/Race.h`)·`AsyncListener` 추가, `Timeout`·`SleepFor`는 기존.
- [x] 3. `Runtime` + 엔진 팩토리 (한 줄 호출의 전제) — **완료:** `MakeEngine(EngineType)` + `Runtime`(`BlockOn`).
- [~] 4. 프로토콜 Client(HTTP/1·WS부터), 그 위 HTTPS(ALPN) — **부분:** HTTP/1 Client/Server + `FetchGet`/`FetchPost` 한 줄 파사드 완료, HTTPS는 Client가 TlsStream(ALPN)로 라우팅. **WS 프레이밍 미구현.**
- [ ] 5. 다중화 계층(HTTP/2·SFTP) — 별도 작업으로 분리
- [x] 6. (스케일 필요 시) thread-per-core(IoContextPool) · zero-copy 공개 표면  *(참고: thread-per-core `ContextPool`은 이미 존재)*

---

## C. 마무리 우선순위(권장 처리 순서)  *(실행 로드맵 — 미착수)*
- [ ] **1순위(실사용 즉시 마찰):**
  - [ ] **네임스페이스 3계층 확정(A-0)** — ⓐ `ne` = 어휘 타입만 ⓑ 모든 공개 API를 `ne::<module>`로 ⓒ 내부는 `ne::<module>::detail`. 먼저 `ne::concurrency`로 `ThreadPool` 흡수(모듈 내 분열 해소)부터 착수.
  - [ ] **`BEGIN_NS`/`END_NS` 폐기 → 명시적 `namespace ne::x { } // namespace ne::x`** — 3계층 재배치와 같은 커밋에서 파일당 한 번에 처리.
  - [ ] `install/export` + `find_package(Nebula)` 지원
  - [ ] CMake 타깃 이름 통일 · `Time/CMakeLists.txt`의 `NebulaUtil` 링크 오타 수정
  - [ ] README 보강(모듈 목록·요구 표준·빌드법·최소 예제)
- [ ] **2순위(안전·정합성):** Json 예외 vs `Result` 철학 정리 · Crypto 안전 기본값/경고 · Network TLS 검증 기본값(OpenSSL 호스트네임 검증 격차) · **Io 에러 경계 통일(D-2)**
- [ ] **3순위(Io-Network 확장 토대, D-5):** 에러 통일 → `AsyncListener`+`when_any` → `Runtime`+엔진 팩토리 → 프로토콜 Client(HTTP/1·WS) → 다중화(HTTP/2·SFTP)
- [ ] **4순위(완성도):** 벤치마크 · Sanitizer CI · 헤더 self-contained 검증 · 사용 예제