# 测试 / Tests

这些是开发时运行的组件与回归测试，不会安装到发布包。保留独立可执行文件以隔离不同的平台适配桩；不需要外部测试框架或 Broker。

These development-time component and regression tests are never installed in the application package. Separate executables isolate different platform stubs; no external test framework or broker is required.

## 运行 / Run

从仓库根目录运行，Linux 需要 CMake 3.20+、Ninja 和 C/C++ 编译器：

From the repository root, on Linux with CMake 3.20+, Ninja, and C/C++ compilers:

```sh
cmake --preset native-tests
cmake --build --preset native-tests
ctest --preset native-tests
```

安装 Clang 和 sanitizer 运行库后，可进行内存、泄漏及未定义行为检查：

With Clang and its sanitizer runtimes installed, check memory safety, leaks, and undefined behavior:

```sh
cmake --preset native-tests-asan
cmake --build --preset native-tests-asan
ctest --preset native-tests-asan
```

该测试预设启用 LeakSanitizer，并让 UBSan 遇错失败。在使用 ptrace 的调试器或沙箱内，LeakSanitizer 可能无法运行；应在支持的环境中执行，环境报错不代表测试通过。

The sanitizer test preset enables LeakSanitizer and makes UBSan stop on errors. LeakSanitizer may not run under ptrace-based debuggers or sandboxes; run it in a supported environment rather than treating an environment error as a pass.

OpenSSL 开发库可选：Linux 上可用时运行 13 项测试，否则运行 11 项并明确跳过 BIO/TLS。若要显式验证无 OpenSSL 的构建，使用独立目录：

OpenSSL development libraries are optional: 13 tests run on Linux when available, otherwise 11 run and BIO/TLS are explicitly skipped. To check a build without OpenSSL, use a separate directory:

```sh
cmake --preset native-tests -B build/tests-no-openssl -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
cmake --build build/tests-no-openssl
ctest --test-dir build/tests-no-openssl --output-on-failure
```

## 分组与覆盖 / Groups and coverage

CTest 名称与可执行文件名称一致：`win32mqtt_<name>_tests`。组件测试有 10 秒超时，生产会话测试有 30 秒超时。

CTest and executable names match: `win32mqtt_<name>_tests`. Component tests have a 10-second timeout; the production-session test has 30 seconds.

| `<name>` | 标签 / Label | 覆盖内容 / Coverage |
| --- | --- | --- |
| `core` | `core` | URI、订阅目录、主题和 Unicode 校验 / URI, catalog, topics, and Unicode validation |
| `capacity` | `core` | 命令与报文上限、用户请求队列满恢复 / Command and packet bounds, user-request queue-full recovery |
| `display` | `core` | 有界事件队列、并发消费、日志淘汰和文本截断 / Event bounds, concurrent consumption, log retention and truncation |
| `publish` | `protocol` | PUBLISH 解析边界、畸形输入、空与二进制 payload / Parser bounds, malformed input, empty and binary payloads |
| `init` | `protocol` | CONNECT 初始化和锁约定 / CONNECT initialization and lock ownership |
| `protocol` | `protocol` | 短写、重试、队列回收后迟到确认、重复确认、Packet ID 重用、QoS 2 全 ID 空间与拥塞恢复、确认报文校验 / Partial writes, late and duplicate ACKs after compaction, ID reuse, full QoS 2 ID space, backpressure recovery, ACK validation |
| `disconnect` | `connection` | 有期限的 DISCONNECT 发送器 / Bounded DISCONNECT sender |
| `connect_attempt` | `connection` | 连接阶段期限和取消隔离 / Connection phase deadlines and cancellation isolation |
| `dns` | `connection` | 异步解析包装器、取消后回调与资源释放 / Async DNS wrapper, late callbacks, and cleanup |
| `socket` | `transport` | Winsock 适配器的短读写、WOULDBLOCK、EOF / Winsock adapter partial I/O, WOULDBLOCK, and EOF |
| `session` | `connection` | 生产工作线程、100 个长主题恢复与退订、队列满恢复、可靠结果、迟到发布确认及 200 条 QoS 2 突发后连接保持、替代连接、失活、停止及取消 / Production worker orchestration, late publish ACKs and 200-message QoS 2 burst recovery (Linux) |
| `tls` | `transport` | 真实证书验证与 TLS 握手、WANT_READ/WANT_WRITE、移动写缓冲、普通 BIO 双向推进 / Real verified TLS handshake and retry scheduling |
| `settings` | `core` | Windows 原子替换、不可写目标、容量失败保留旧文件、INI 精确往返 / Windows atomic replacement and persistence failures |
| `bio` | `transport` | OpenSSL BIO 重试和有限缓冲区 / OpenSSL BIO retries and bounded buffers |

```sh
# 列出测试 / List tests
ctest --preset native-tests -N
# 只运行协议组 / Run the protocol group
ctest --preset native-tests -L protocol
# 只运行 DNS / Run DNS only
ctest --preset native-tests -R '^win32mqtt_dns_tests$'
```

原 `mqtt_backpressure_tests.c` 已更名为 `mqtt_protocol_tests.c`，与其协议队列和订阅确认覆盖范围一致；原有用例保留。新增用例应验证可观察行为和故障恢复，避免仅重复实现步骤。

The former `mqtt_backpressure_tests.c` is now `mqtt_protocol_tests.c`, reflecting its protocol-queue and subscription-acknowledgement coverage. Existing cases are retained. New tests should verify observable behavior and failure recovery rather than merely repeating implementation steps.

## 验证边界 / Validation boundaries

测试直接编译生产协议与辅助组件。`mqtt_test_pal.h` 替换平台类型、锁和时钟；`mqtt_transport_pal.h` 替换 Winsock 调用；`mqtt_dns_test_api.hpp` 注入 Windows DNS API 结果。BIO 测试使用真实 OpenSSL BIO，但不执行 TLS 握手。DNS 和显示测试中包含真实测试线程。

Tests compile the production protocol and helper components. `mqtt_test_pal.h` replaces platform types, locks, and clocks; `mqtt_transport_pal.h` replaces Winsock calls; `mqtt_dns_test_api.hpp` injects Windows DNS API results. BIO tests use real OpenSSL BIOs without a TLS handshake. DNS and display tests include real test threads.

Linux 原生套件执行生产会话线程编排和真实 TLS 握手，但默认不执行 Windows GUI、真实 Windows DNS 服务、实际 Broker 或遗嘱集成流程。Windows/Broker 入口见下方，交叉编译成功不等于运行验证。

The Linux suite now executes production session orchestration and real TLS handshakes. Windows GUI, Windows DNS and broker/Last Will integration require the separate Windows runner below. A cross-build is not execution evidence.

发布预设默认 `BUILD_TESTING=OFF`。可在独立交叉编译目录中显式启用测试以验证 Windows 编译；仅在配置 `CMAKE_CROSSCOMPILING_EMULATOR` 时才注册可执行的交叉编译测试，编译成功不等于运行通过。Windows 测试运行时需保证其运行时 DLL 可被加载。

Release presets default to `BUILD_TESTING=OFF`. Enable it in a separate cross-build directory to verify Windows compilation. Cross-compiled tests are registered only with `CMAKE_CROSSCOMPILING_EMULATOR`; a successful build is not a test run. Windows test execution also requires its runtime DLLs to be available.

初始化测试保留负向验证入口：`build/native-tests-asan/tests/win32mqtt_init_tests --reproduce-unlocked-connect`。预期退出码为 1，并报告 `FAIL: unlock without ownership`；它不属于正常 CTest 套件。

Initialization tests retain a negative check: `build/native-tests-asan/tests/win32mqtt_init_tests --reproduce-unlocked-connect`. It must exit with code 1 and report `FAIL: unlock without ownership`; it is not part of the normal CTest suite.


## Windows / Broker 集成

在 Windows 的独立构建目录启用 `BUILD_TESTING=ON`、`WIN32MQTT_ENABLE_TLS=ON` 和
`WIN32MQTT_BROKER_TESTS=ON`，并配置已安装的 OpenSSL。构建后先运行 CTest，
其中 `settings` 会直接调用 Windows 文件 API 验证保存失败保留原文件。

`win32mqtt_live_tests.exe` 使用默认生产 DNS/socket/TLS 实现，不使用注入后端。
准备好 Mosquitto 和 OpenSSL 命令行工具，并确保测试程序依赖的 DLL 可加载后运行：

```powershell
powershell -ExecutionPolicy Bypass -File tests/run-windows-broker.ps1 `
  -Executable build/windows/tests/Debug/win32mqtt_live_tests.exe `
  -Mosquitto 'C:/Program Files/mosquitto/mosquitto.exe' `
  -OpenSSL 'C:/Program Files/OpenSSL-Win64/bin/openssl.exe'
```

路径按实际构建目录和安装目录调整。脚本在本机 18883/18884 端口启动独立 Broker，
生成临时 localhost 证书，分别验证 TCP、TLS 的 QoS 0/1/2 二进制消息收发，
以及客户端异常退出后的 Last Will；再验证主机名不匹配时 TLS 连接失败。
脚本在退出时停止自己启动的 Broker，并恢复测试程序目录原有的 CA 文件。
该入口不属于默认无 Broker 的 CTest 套件，也不覆盖 GUI 操作和真实外网压力。

本次 Linux 工作区未运行上述 Windows 场景；只交叉编译了应用、Windows 配置测试和
真实 Broker 测试入口。请保留这一验证边界，直到 Windows 运行结果可用。
