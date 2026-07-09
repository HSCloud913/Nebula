# Io 평가 · Ipc/Network 설계 · time::Awaitable 활용 브리핑

> 작성: 2026-07-09 · 읽기 전용 분석(코드 변경 없음) · 기준: 당시 Io 프로젝트 현행 상태

범례: ●●● 성숙·검증 · ●●○ 동작하나 갭/미결선 · ●○○ 초기/부분 · ○○○ 미구현

---

## 1. Io 프로젝트 구현 수준 (항목별 평가)

### Level 0 — 엔진 계층
| 항목 | 수준 | 근거 |
|---|---|---|
| `IEngine` 완료기반 인터페이스 | ●●● | Submit/WaitCompletions/Wake/Cancel/Supports + `AsRegisteredBufferProvider` — capability로만 질의, 엔진분기 없음 |
| **IocpEngine**(Win proactor) | ●●● | Read/Write/Recv/Send/Accept(AcceptEx)/Connect(ConnectEx)/SendTo/ReceiveFrom + TransmitFile + RIO. inflight맵+CancelIoEx 직렬화. 테스트됨 |
| **IoUringEngine**(Linux proactor) | ●●○ | 전 opcode + Fixed Buffer + MSG_ZEROCOPY, 지연 ASYNC_CANCEL. (Windows 빌드/CI 밖) |
| **EpollEngine**(Linux reactor) | ●●○ | readiness→synthetic completion, sendfile/MSG_ZEROCOPY. 등록버퍼 없음 |
| **WsaPollEngine**(Win reactor 폴백) | ●●○ | synthetic completion, TransmitFile. RIO 불가(완료모델 전제). 테스트됨 |
| RioProvider / IoUringProvider | ●●○ | RegisterBuffer/SubmitSend·ReceiveRegistered 구현. **단 고수준 Socket에 미결선** |

### Level 1 — IoContext
| 항목 | 수준 | 근거 |
|---|---|---|
| 단일스레드 executor(Run/RunOnce/Post/Stop) | ●●● | Stop/Run 경합·abandoned 수명안전 처리 견고 |
| TimerWheel 연동 | ●●○ | `SetTimerWheel` + EffectiveTimeout + Tick. 단 **수동 주입** 필요, 기본 미장착 |
| **thread-per-core** | ○○○ | `IoContextPool`은 **주석에만 존재**. Post()만 있고 코어 스케줄러 없음 |

### Level 2 — 코루틴/에러
| 항목 | 수준 | 근거 |
|---|---|---|
| `Task<T>`(symmetric transfer) | ●●● | 이동전용, UAF 계약 명시. throw→terminate(설계상) |
| Io `Awaitable`(Request→co_await) | ●●● | stop_token→CancelIoEx, abandoned heap 이관(mid-flight 안전) |
| `IoResult`/`IoError`/`CO_TRY` | ●●● | 값 기반 자동전파 |

### Level 3 — 리소스
| 항목 | 수준 | 근거 |
|---|---|---|
| `File` | ●●○ | Open/Read/Write/Readv/Writev/Close. async open·truncate·metadata 없음, `*_fixed` 공개 없음 |
| `Socket`(TCP+UDP) | ●●○ | Attach/Connect/Accept/Recv/Send/Recvv/Sendv/SendTo/ReceiveFrom. **갭**: 소켓옵션(nodelay/reuse/timeout)·shutdown(half-close)·SendZeroCopy/SendFile 공개 없음, Connect는 숫자IP 전용, RIO 플래그 미생성 |
| `BufferView`/`BufferChain` | ●●○ | iovec/WSABUF 매핑. RegisteredBuffer/BufferPool RAII 없음 |

### Level 3.5 — Zero-copy (가장 큰 갭)
| 항목 | 수준 | 근거 |
|---|---|---|
| 엔진 opcode(ReadFixed/WriteFixed/SendZeroCopy/SendFile) | ●●○ | enum·Request(auxHandle/bufferId)·엔진 처리·capability 광고 **존재** |
| **공개 API 표면** | ○○○ | `Socket::SendZeroCopy/SendFile`·`File::*_fixed`·`RegisteredBuffer`·`BufferPool` **부재** → 엔진은 되는데 상위에서 못 씀 |

### 횡단 항목
| 항목 | 수준 | 비고 |
|---|---|---|
| 에러 모델 일관성 | ●●○ | File/Socket=IoError, **provider=OsError** (zero-copy 경계 이원화) |
| 엔진 팩토리(create_engine/EnginePreference) | ○○○ | 없음 — 호출자가 구체 엔진 직접 생성 |
| `AsyncListener`(bind/listen/accept 루프) | ○○○ | 없음 |
| Io 레벨 Timer/SleepFor | ○○○ | Time::Awaitable 의존, Io 래퍼 없음 |
| 테스트/문서 | ●●○ / ●●● | Win 엔진·context·awaitable·socket 검증. 일부 통합테스트 깨짐. 인라인 문서 풍부 |

**총평:** *커널 완료 엔진 + 완료모델 통일 + 코루틴/에러/수명안전*은 **프로덕션급에 근접(성숙)**. 반대로 *zero-copy 공개 표면, thread-per-core, 연결관리 헬퍼(Listener·옵션·타임아웃), 엔진 팩토리*가 **얇음**. 한 줄 요약: **"저수준은 탄탄, 상위 편의/zero-copy 표면이 미완".**

---

## 2. Ipc / Network 설계 & 지원 범위

### 공통 전제 (Io 위에 얹기)
```
io::IEngine (엔진 택1)                      ← 팩토리 없으니 당분간 직접 생성
   └ io::Context (스레드 1개 = 컨텍스트 1개)  ← thread-per-core 전까지 이 가정
        ├ io::Socket / io::File (완료 async, IoResult<IoError>)
        └ time::Awaitable (타임아웃/주기) — 3번 참조
```
**선결 1순위: 에러 타입 통일** — Network `IStream`이 아직 `Result<T,OsError>`. `IoResult<T>`(또는 network 전용 에러)로 맞춰야 접합부 변환이 사라짐.

### Network (Stream부터 재구현)
```
io::Socket ─ PlainStream:IStream ─ TlsStream/SshStream(하위 IStream 래핑) ─ HTTP1/2·FTP·SFTP
```
- **PlainStream** = `io::Socket` 얇은 래퍼 (Send/Receive/Sendv → io::Socket, 버퍼는 이미 io::BufferView/Chain로 정렬됨).
- **역할 분담**: DNS 해석/페일오버/소켓옵션은 Network 계층(숫자 endpoint 생성 후 `io::Socket::Connect` 위임). 데이터경로는 io로 내려감.
- **서버**: `io::Socket::Accept`(AcceptEx) 존재 → accept 루프 헬퍼(AsyncListener)만 얹으면 됨.
- **TLS**: `TlsStream`(Schannel/SSPI)이 PlainStream 위에 얹힘 — 기존 구조 재사용.

**지금 지원 가능**: 단일스레드 async TCP 클라/서버(요청-응답·에코), UDP(SendTo/ReceiveFrom), HTTP/1 over Plain, TLS(Schannel).
**소규모 작업 필요**: accept 루프, connect/read 타임아웃, 소켓옵션, graceful shutdown.
**중간 작업 필요**: 진짜 zero-copy 송신(SendFile/RIO/SEND_ZC 공개 API), 등록버퍼 풀링, thread-per-core.
**플랫폼 한계**: **Windows 진짜 recv zero-copy 없음**(RIO는 오버헤드 감소 수준), io_uring 미지원 커널→epoll 폴백.

### Ipc
현재 `Pipe/SharedMemory/Semaphore/MessageQueue`는 **구 Io API(IIoEngine/구 awaitable) 참조로 stale** + mingw `counting_semaphore` 미지원.
- **설계**: Named Pipe(Win)/UDS(POSIX) 핸들을 `io::Context`에 associate → async Read/Write 래퍼 → **IStream 호환 트랜스포트** → Network의 스트림/프로토콜 스택을 **IPC 위에서 재사용**.
- **SharedMemory** → L3.5 등록버퍼의 백킹으로 재활용(zero-copy IPC).
- **선결**: (a) Ipc를 현행 Io API로 마이그레이션, (b) 이식성 있는 세마포어로 교체.

### 착수 순서 (Io에 먼저 필요한 것)
1. **에러 타입 통일**(IoError, provider·IStream 포함)
2. **AsyncListener + 소켓옵션 + shutdown**
3. **`when_any`/타임아웃 콤비네이터 + TimerWheel 기본 장착**(또는 `context.SleepFor`)
4. (zero-copy용) **RegisteredBuffer/BufferPool + Socket::SendZeroCopy/SendFile + RIO 플래그 생성**
5. 다코어 확장 시 **thread-per-core(IoContextPool)** — 아니면 1컨텍스트/1스레드로 충분

### 지원 상한 요약
- ✅ 지금: 단일스레드 async TCP/UDP 클라+서버, 파일 async I/O, TLS(Schannel)/plain 스트림, HTTP/1, (Ipc 마이그레이션 후) 로컬 IPC(pipe/UDS)
- ⚠️ 소규모: 서버 accept 루프, connect/read 타임아웃, 소켓옵션, graceful shutdown
- 🔶 중간: zero-copy 송신(SendFile/RIO/SEND_ZC 공개 API), 등록버퍼 풀링, thread-per-core 스케일링
- ❌ 플랫폼 한계: Windows 진짜 recv zero-copy 없음, io_uring 구커널/컨테이너 → epoll 폴백

---

## 3. `time::Awaitable` 활용처

현재 테스트에서만 쓰이고 미결선. 메커니즘(`co_await SleepFor(wheel, 500ms)` → TimerWheel 등록 → IoContext 루프가 만료 시 resume)은 완성돼 있고, **IoContext에 `SetTimerWheel`로 그 wheel을 물리기만** 하면 동작.

| 활용 | 패턴 |
|---|---|
| **connect/read/write 타임아웃** | `when_any(socket.Receive(buf), SleepFor(wheel, 5s))` — 데드라인이 이기면 I/O 취소(Socket op의 stop_token 취소 존재) |
| **재시도 백오프** | 재연결 시도 사이 `co_await SleepFor(wheel, backoff)`(지수 백오프) |
| **하트비트/keepalive** | 주기 SleepFor 루프로 ping(HTTP/2 PING, WS ping, SSH keepalive) |
| **레이트리밋/pacing** | 청크 전송 사이 sleep으로 스로틀 |
| **idle 타임아웃** | N초 유휴 연결 서버측 종료 |
| **주기 작업** | 메트릭 flush, 캐시 evict, 앱레벨 프로토콜 타이머 |
| **절대 데드라인 전파** | `Deadline(wheel, tp)`로 요청 파이프라인 전반 데드라인 |

**실사용 선결 조건**
1. IoContext가 그 TimerWheel을 tick — **메커니즘 이미 있음**(SetTimerWheel + EffectiveTimeout이 다음 만료에 정확히 wakeup).
2. **타임아웃 패턴엔 `when_any`/`Timeout(task, dur)` 콤비네이터가 없음** — 지금은 "sleep만" 가능, "I/O를 타이머와 경주"는 불가 → 이 헬퍼 추가가 핵심.
3. **권장**: `context.SleepFor(dur)` 래퍼로 wheel 핸들 관리를 감추기(호출자 편의 + 결선 강제).
4. 소멸자 취소(감사 수정 #3) 덕에, 경주에서 I/O가 이기면 타이머 awaitable을 안전하게 폐기 가능(UAF 없음).

**결론**: `when_any`(또는 `Timeout`) 콤비네이터 + `IoContext::SleepFor` 노출 두 가지만 얹으면, `time::Awaitable`이 Network/Ipc의 **모든 타임아웃·재시도·하트비트 로직의 backbone**이 됨. 지금은 그 두 결선이 없어 "만들어졌지만 잠들어 있는" 상태.
