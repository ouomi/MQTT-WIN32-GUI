# 核心与协议测试

Linux 原生构建只生成测试，不需要 Windows SDK 或 OpenSSL：

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

- `win32mqtt_core_modules`：URI 解析与订阅目录。
- `win32mqtt_publish_parser`：直接编译仓库内的 `mqtt.c`，验证 PUBLISH 解析边界、错误长度、各 QoS 的空 payload、二进制 payload、截断报文及连续报文。

- `win32mqtt_initialization`：验证初始化的锁状态、CONNECT 成功与失败时的锁释放、失败后再次连接及传统 `mqtt_init` 调用约定。

协议测试使用 `mqtt_test_pal.h` 替换平台类型和宏，不链接生产网络适配层。意外调用网络时立即终止；PUBLISH 测试也禁止锁操作，初始化测试则用计数桩检测未持锁解锁和意外重复加锁。这些测试不验证 Winsock、TLS、真实线程调度或 Broker 行为。畸形报文与截断报文使用与输入长度一致的堆分配，便于 AddressSanitizer 发现越界读取。

运行 `build-tests-asan/tests/win32mqtt_init_tests --reproduce-unlocked-connect` 可复现修复前的调用顺序。预期退出码为 1，并报告 `FAIL: unlock without ownership`；此负向检查不属于正常 CTest 测试。

Windows 构建默认也生成这三个测试目标。交叉编译时，仅在配置了 `CMAKE_CROSSCOMPILING_EMULATOR` 的情况下注册 CTest 测试，避免在宿主机直接执行 Windows 程序。
