# WIN32-MQTT

一个小巧的 Windows MQTT 调试工具，使用原生 Win32 控件。支持连接 Broker、管理订阅、发布消息和查看消息日志。

A small Windows MQTT debugging tool built with native Win32 controls. Connect to a broker, manage subscriptions, publish messages, and inspect the message log.

[简体中文](#简体中文) · [English](#english)

## 简体中文

### 使用

需要 **Windows 8 或更新版本**。打开发布目录中的 `WIN32-MQTT.exe`，无需安装。

1. 输入 Broker 地址，例如 `mqtt://localhost:1883`；TLS 版也支持 `mqtts://broker.example:8883`。
2. 设置客户端 ID；如果需要遗嘱消息，在连接前配置。
3. 点击连接，添加并勾选需要订阅的主题过滤器，例如 `sensors/#`。
4. 在发布区独立输入主题，例如 `sensors/temperature`，选择 QoS 并发送。

发布支持 QoS 0/1/2 和空消息，无需先订阅。界面支持中文和英文。连接地址、客户端 ID、订阅和部分界面设置保存在 EXE 旁的 `WIN32-MQTT.ini`。

TLS 版使用 OpenSSL 验证证书链和服务器身份，最低 TLS 1.2。请将 EXE、OpenSSL DLL 和 `ca-bundle.pem` 保持在同一目录，并保留许可证文件。非 TLS 版仅支持 `mqtt://`。

### 从 Linux 构建

需要 **CMake 3.20+、Ninja** 和所选架构的 **MinGW-w64 C/C++ 编译器及 windres**。只构建 64 位时，不需要安装 32 位工具链。

最简单的 64 位非 TLS 构建不依赖 OpenSSL：

```sh
cmake --preset mingw64-notls-release
cmake --build --preset mingw64-notls-release
cmake --install build/mingw64-notls-release --strip
```

完整发布目录为 `dist/mingw64-notls-release/`。`--strip` 缩小安装后的 EXE，构建目录仍保留未去符号的文件。

TLS 构建需要 vcpkg 提供对应的 Windows OpenSSL。将 vcpkg 安装在自己选择的位置，并设置 `VCPKG_ROOT`：

```sh
# 如果尚未安装 vcpkg，先执行以下三步。
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
export VCPKG_ROOT="$HOME/vcpkg"
"$VCPKG_ROOT/bootstrap-vcpkg.sh"

"$VCPKG_ROOT/vcpkg" install openssl:x64-mingw-dynamic
cmake --preset mingw64-tls-release
cmake --build --preset mingw64-tls-release
cmake --install build/mingw64-tls-release --strip
```

如果已有 vcpkg，只需将 `VCPKG_ROOT` 指向实际路径，并安装对应包。以后打开新终端时也需要设置该变量。vcpkg 及其依赖可能有高于本项目的工具版本要求。

| 发布预设 | 架构 | 支持的地址 | 所需 OpenSSL 包 |
| --- | --- | --- | --- |
| `mingw64-notls-release` | x86_64 | `mqtt://` | 无 |
| `mingw64-tls-release` | x86_64 | `mqtt://`、`mqtts://` | `openssl:x64-mingw-dynamic` |
| `mingw32-notls-release` | x86 | `mqtt://` | 无 |
| `mingw32-tls-release` | x86 | `mqtt://`、`mqtts://` | `openssl:x86-mingw-dynamic` |

其他预设使用相同的配置、编译和安装命令，替换预设名称即可。所有发布预设默认关闭测试；工具链名称、Windows 本机构建和自定义 OpenSSL 配置见[开发说明](docs/DEVELOPMENT.md)。

### 运行测试

Linux 原生测试不需要 Windows SDK 或 Broker。需要 C/C++ 编译器；检测到本机 OpenSSL 开发库时会增加 BIO 测试。

```sh
cmake --preset native-tests
cmake --build --preset native-tests
ctest --preset native-tests
```

安装 Clang 及其 sanitizer 运行库后，可将上述三条命令中的预设换成 `native-tests-asan`，运行 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer 检查。覆盖范围和按组运行方法见[测试说明](tests/README.md)。

### 输出目录

```text
build/<preset>/                 编译缓存与中间文件
build/<release-preset>/bin/     开发运行用 EXE；TLS 预设同时复制 CA 和 DLL
build/<test-preset>/tests/      测试程序
dist/<release-preset>/         可直接打包分发的完整目录
```

`dist/` 由安装步骤生成，包含 EXE、许可证，以及 TLS 版需要的 CA 和 DLL；测试程序不会进入发布包。多配置生成器会在开发输出目录下增加 `Debug/` 或 `Release/` 子目录。

### 当前范围与限制

- 使用 MQTT 3.1.1，当前连接界面不提供用户名/密码配置。
- 发送报文最多 **4096 编码字节**，接收报文最多 **8192 编码字节**，均包含协议开销。
- 发布主题不能带 `+` 或 `#`；订阅过滤器可以使用合法的通配符。
- 发布区输入 UTF-8 文本；非 UTF-8 接收内容显示为占位符，尚无十六进制视图。
- 订阅勾选表示期望状态，Broker 的订阅/退订确认和订阅拒绝显示在日志中。
- 发布“已排队”表示本地排队成功，不代表对端已经收到或业务已经处理。
- 日志有保留上限，高流量下可能丢弃显示事件并提示丢弃数量；日志不保存到磁盘。

### 许可证与项目背景

项目使用 [MIT 许可证](LICENSE)。内置 MQTT-C 和 OpenSSL 的许可证位于 [resources/licenses](resources/licenses)。CA 文件来自 curl 的 Mozilla 证书提取，更新时请保留文件头与归属信息。

本项目在 AI 辅助下开发，目标是保持界面直接、依赖少、便于阅读和维护。

## English

### Usage

Requires **Windows 8 or later**. Open `WIN32-MQTT.exe` from the distribution directory; no installer is needed.

1. Enter a broker URI, such as `mqtt://localhost:1883`. TLS builds also accept `mqtts://broker.example:8883`.
2. Set the client ID and, if needed, configure the Last Will before connecting.
3. Connect, add subscription filters such as `sensors/#`, and tick their checkboxes.
4. Enter a separate publish topic such as `sensors/temperature`, choose a QoS, and send.

Publishing supports QoS 0/1/2 and empty messages without requiring a subscription. The UI supports Chinese and English. The server URI, client ID, subscriptions, and some display settings are saved in `WIN32-MQTT.ini` beside the EXE.

TLS builds use OpenSSL to verify the certificate chain and server identity, with TLS 1.2 as the minimum. Keep the EXE, OpenSSL DLLs, and `ca-bundle.pem` together, along with the license files. TCP-only builds support `mqtt://` only.

### Build from Linux

Install **CMake 3.20+, Ninja**, and the **MinGW-w64 C/C++ compilers and windres** for your chosen architecture. A 64-bit build does not require the 32-bit toolchain.

The simplest 64-bit TCP-only build needs no OpenSSL:

```sh
cmake --preset mingw64-notls-release
cmake --build --preset mingw64-notls-release
cmake --install build/mingw64-notls-release --strip
```

The complete package is in `dist/mingw64-notls-release/`. `--strip` reduces the installed EXE size while leaving the unstripped build output available.

TLS presets use vcpkg's Windows OpenSSL packages. Install vcpkg wherever you prefer and set `VCPKG_ROOT`:

```sh
# Run these three steps if vcpkg is not already installed.
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
export VCPKG_ROOT="$HOME/vcpkg"
"$VCPKG_ROOT/bootstrap-vcpkg.sh"

"$VCPKG_ROOT/vcpkg" install openssl:x64-mingw-dynamic
cmake --preset mingw64-tls-release
cmake --build --preset mingw64-tls-release
cmake --install build/mingw64-tls-release --strip
```

For an existing installation, point `VCPKG_ROOT` to its actual path and install the matching package. Set the variable again in new terminals. vcpkg and its dependencies may require newer tools than the project itself.

| Release preset | Architecture | URI support | OpenSSL package |
| --- | --- | --- | --- |
| `mingw64-notls-release` | x86_64 | `mqtt://` | None |
| `mingw64-tls-release` | x86_64 | `mqtt://`, `mqtts://` | `openssl:x64-mingw-dynamic` |
| `mingw32-notls-release` | x86 | `mqtt://` | None |
| `mingw32-tls-release` | x86 | `mqtt://`, `mqtts://` | `openssl:x86-mingw-dynamic` |

Use the same configure, build, and install commands with the desired preset name. Release presets disable tests. See the [development guide](docs/DEVELOPMENT.md) for toolchain names, native Windows builds, and custom OpenSSL configurations.

### Run tests

Native Linux tests require C/C++ compilers, but no Windows SDK or broker. BIO tests are included when the host OpenSSL development libraries are available.

```sh
cmake --preset native-tests
cmake --build --preset native-tests
ctest --preset native-tests
```

With Clang and its sanitizer runtimes installed, replace the preset in all three commands with `native-tests-asan` to run AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer checks. See the [test guide](tests/README.md) for coverage and test groups.

### Output directories

```text
build/<preset>/                 Build cache and intermediate files
build/<release-preset>/bin/     Development EXE; TLS presets also copy the CA and DLLs
build/<test-preset>/tests/      Test executables
dist/<release-preset>/         Complete directory ready to distribute
```

The install step creates `dist/`, containing the EXE, licenses, and the CA and DLLs needed by TLS builds. Tests are never installed. Multi-config generators add a `Debug/` or `Release/` subdirectory to development output paths.

### Current scope and limits

- MQTT 3.1.1; the connection UI currently has no username/password settings.
- Outgoing packets are limited to **4096 encoded bytes**, incoming packets to **8192 encoded bytes**, including protocol overhead.
- Publish topics cannot contain `+` or `#`; subscription filters support valid wildcards.
- Publishing accepts UTF-8 text. Non-UTF-8 received data appears as a placeholder; there is no hex view yet.
- Subscription checkboxes represent desired state. Broker subscription/unsubscription acknowledgements and subscription rejection appear in the log.
- “Publish queued” means accepted into the local queue, not received by the peer or processed by an application.
- Logs have bounded retention. Heavy traffic may drop display events with a reported count; logs are not saved to disk.

### License and background

The project is [MIT licensed](LICENSE). Bundled MQTT-C and OpenSSL license texts are in [resources/licenses](resources/licenses). The CA bundle comes from curl's Mozilla certificate extraction; retain its header and attribution when updating it.

This project was developed with AI assistance, with a focus on a direct UI, few dependencies, and readable, maintainable code.
