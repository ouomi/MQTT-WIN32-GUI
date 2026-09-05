# 核心与协议测试

Linux 原生构建只生成测试，不需要 Windows SDK。OpenSSL 可选；检测到后额外构建 BIO 传输测试：

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

使用 Clang 检查越界访问及未定义行为：

```sh
cmake -S . -B build-tests-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build-tests-asan
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tests-asan --output-on-failure
```

- `win32mqtt_core_modules`：URI 解析、订阅目录以及发布主题/订阅过滤器校验，覆盖通配符、UTF-8/UTF-16、空字符和编码长度边界。
- `win32mqtt_publish_parser`：直接编译仓库内的 `mqtt.c`，验证 PUBLISH 解析边界、错误长度、各 QoS 的空 payload、二进制 payload、截断报文及连续报文。
- `win32mqtt_initialization`：验证初始化的锁状态、CONNECT 成功与失败时的锁释放、失败后再次连接及传统 `mqtt_init` 调用约定。
- `win32mqtt_backpressure_tests`：通过模拟时钟和可控读写，验证零进展、部分发送、超时重发顺序、重发期间收到 ACK、队列整理、重连偏移清零及 EOF 前交付所有完整报文；另外验证单轮最多处理 32 个报文，剩余缓冲数据在后续调用继续处理；验证没有订阅时 QoS 0/1/2 空 payload 的发送及报文解码；验证 SUBACK 拒绝不使连接失败、乱序确认关联、拒绝后继续收消息和再次订阅、完成后不重发、可选回调及畸形/未知 SUBACK 校验；验证 UNSUBACK 乱序关联原主题与 Packet ID、拆包、重复确认不重复通知、确认后不重发、在途 PUBLISH、可选回调及初始化/重初始化、零 ID、长度、保留位和未知确认。
- `win32mqtt_disconnect_tests`：直接测试会话使用的 `MqttDisconnect`，验证 DISCONNECT 的实际字节、短写、零进展、续完正在发送的报文、跳过未发送队列、队列已满、不等待 QoS 确认、1 秒期限、重复请求不延长期限和传输失败。使用可控时钟，无需实际等待。
- `win32mqtt_connect_attempt_tests`：验证 DNS/TCP/TLS/CONNACK 的期限边界、阶段切换、310 秒后仍判定超时、提前取消、取消优先于超时和不同连接请求的取消隔离。
- `win32mqtt_dns_tests`：直接测试生产异步 DNS 包装器，以 `mqtt_dns_test_api.hpp` 注入 Windows API 结果，验证立即成功/失败、异步回调、取消后延迟回调、启动返回前回调、销毁与回调竞争、不支持取消 API 和 Winsock 启动失败。检查解析参数在取消后仍有效，地址和 Winsock 引用恰好释放一次。
- `win32mqtt_capacity_tests`：验证命令数量与字节上限、拒绝后 FIFO 不变、断开预留位置、出队归还容量和整数溢出边界；将报文大小判断与实际 MQTT 编码比较；填满真实 MQTT-C 发送队列后验证发布/订阅/退订被拒绝但连接可继续发送，释放容量后可重试，真实网络错误不会被清除。
- `win32mqtt_display_tests`：直接测试事件队列、窗口桥接和日志模型，覆盖条数/字节上限、状态优先与合并、溢出计数、分批取出、关闭后迟到回调、1 万条事件的并发生产/消费，以及日志淘汰、清空、截断、UTF-16 边界和空字符显示。不执行 Windows 定时器和 EDIT 控件绘制。
- `win32mqtt_socket_tests`：直接编译生产 `mqtt_pal.c` 的 Winsock 分支，以函数桩提供短读写、WOULDBLOCK、EOF 和错误；检查单次调用及时返回、零长度不访问传输层以及长度转换上限。
- `win32mqtt_bio_tests`（需要 OpenSSL）：直接编译生产 BIO 分支，用 OpenSSL 自定义 BIO 检查 WANT_READ/WANT_WRITE（包括返回零的重试）、短读写、EOF 和错误。另用真实 BIO pair 的有限缓冲区验证写满、读出后续写及关闭行为。

协议测试使用 `mqtt_test_pal.h` 替换平台类型、锁和时钟；PUBLISH/初始化测试禁止网络调用，背压和断开测试提供可控传输。断开与连接阶段测试不执行 Windows 会话线程和窗口销毁流程。DNS 测试中的竞争由真实测试线程驱动，但不调用 Windows DNS 服务。适配层测试使用 `mqtt_transport_pal.h`，Winsock 调用被替换为桩，BIO 调用使用真实 OpenSSL 库。这些测试不验证 Windows 实际网络拥塞、DNS 服务、TLS 握手、会话线程调度或 Broker 行为。畸形报文与截断报文使用与输入长度一致的堆分配，便于 AddressSanitizer 发现越界读取。

运行 `build-tests-asan/tests/win32mqtt_init_tests --reproduce-unlocked-connect` 可复现修复前的调用顺序。预期退出码为 1，并报告 `FAIL: unlock without ownership`；此负向检查不属于正常 CTest 测试。

Windows 构建默认生成十个测试目标，TLS 构建增加 BIO 测试。交叉编译时，仅在配置了 `CMAKE_CROSSCOMPILING_EMULATOR` 的情况下注册 CTest 测试，避免在宿主机直接执行 Windows 程序。
