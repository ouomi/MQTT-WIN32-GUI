# 开发说明 / Development guide

日常使用、发布构建和测试入口见 [README](../README.md)。本文记录构建约定和实现边界。

See the [README](../README.md) for usage, release builds, and tests. This guide covers build conventions and implementation boundaries.

## 工程结构 / Project structure

| 路径 / Path | 职责 / Purpose |
| --- | --- |
| `CMakeLists.txt` | 构建选项、依赖和应用目标 / Build options, dependencies, and the app target |
| `CMakePresets.json` | 共享的发布、测试入口 / Shared release and test presets |
| `cmake/toolchains/` | Linux → Windows 的 x86/x86_64 MinGW 工具链 / MinGW cross-toolchains |
| `cmake/RuntimeFiles.cmake` | 开发运行文件复制、便携包安装 / Runtime staging and portable installation |
| `src/win32/` | Win32 窗口、面板、配置和事件桥接 / Windows, panels, settings, and event bridge |
| `src/mqtt/` | 会话、传输辅助组件和内置 MQTT-C / Session, transport helpers, and in-tree MQTT-C |
| `tests/` | 独立组件测试及平台适配桩 / Component tests and platform stubs |

所有生成文件放在 `build/` 和 `dist/`。发布时打包完整的 `dist/<preset>/`；不要直接打包构建目录。`cmake --install` 不会清除目标目录内的旧文件，切换依赖或自定义安装目录时应使用新的空目录。默认四个发布预设各自使用独立目录。

Generated files belong in `build/` and `dist/`. Distribute the complete `dist/<preset>/` directory. Installation does not remove stale files: use a fresh staging directory when changing dependencies or a custom install location. The four release presets use separate directories.

## 构建选项 / Build options

| 选项 / Option | 默认值 / Default | 含义 / Meaning |
| --- | --- | --- |
| `WIN32MQTT_BUILD_APP` | Windows 目标为 ON，其他为 OFF / ON for Windows targets | 是否构建 Win32 应用 / Build the GUI |
| `BUILD_TESTING` | ON；发布预设为 OFF / ON; OFF in release presets | 是否构建组件测试 / Build component tests |
| `WIN32MQTT_ENABLE_TLS` | ON | 应用支持 MQTTS，并查找 OpenSSL / Enable application TLS |
| `WIN32MQTT_BUNDLE_OPENSSL` | OFF；TLS 发布预设为 ON / OFF; ON in TLS release presets | 复制和安装 OpenSSL DLL，不改变链接方式 / Stage DLLs; does not select static/dynamic linkage |
| `WIN32MQTT_OPENSSL_RUNTIME_DIR` | 空 / Empty | 启用 DLL 打包时，指定匹配的 DLL 目录 / Override the runtime DLL source directory |

Linux 原生测试在发现 OpenSSL 后测试 BIO，即使应用 TLS 选项为 OFF；不需要 OpenSSL 的最小测试构建可传入 `-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON`。没有 GUI 且没有测试时，配置会明确失败。

Native Linux tests exercise BIO when OpenSSL is found, independently of the application TLS switch. For tests without OpenSSL, pass `-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON`. Configuration fails if both the app and tests are disabled.

旧选项 `WIN32MQTT_OPENSSL_DYNAMIC` 已替换为 `WIN32MQTT_BUNDLE_OPENSSL`，自定义命令和本地预设应同步更新。链接方式由选择的 OpenSSL 安装或 vcpkg triplet 决定。

Update custom commands and local presets from the old `WIN32MQTT_OPENSSL_DYNAMIC` option to `WIN32MQTT_BUNDLE_OPENSSL`. The selected OpenSSL installation or vcpkg triplet determines linkage.

## 工具链与本地设置 / Toolchains and local settings

Linux 交叉编译器通过 PATH 查找，不要求固定安装目录：

Cross-compilers are found through PATH, without a fixed installation directory:

| 目标 / Target | C | C++ | Resources |
| --- | --- | --- | --- |
| x86_64 | `x86_64-w64-mingw32-gcc` | `x86_64-w64-mingw32-g++` | `x86_64-w64-mingw32-windres` |
| x86 | `i686-w64-mingw32-gcc` | `i686-w64-mingw32-g++` | `i686-w64-mingw32-windres` |

TLS 预设通过 `$env{VCPKG_ROOT}` 找到 vcpkg。包安装是显式的准备步骤，本项目没有在配置时自动下载依赖。vcpkg 自身及 OpenSSL port 的工具版本要求可能高于项目最低 CMake 版本。

TLS presets locate vcpkg through `$env{VCPKG_ROOT}`. Install packages explicitly before configuring; the project does not automatically download dependencies during configuration. vcpkg and its OpenSSL port may require newer tools than the project's CMake minimum.

个人路径、编译器和其他覆盖配置放入未跟踪的 `CMakeUserPresets.json`。例如：

Keep personal paths and overrides in the ignored `CMakeUserPresets.json`. For example:

```json
{
  "version": 2,
  "configurePresets": [
    {
      "name": "local-tls",
      "inherits": "mingw64-tls-release",
      "environment": { "VCPKG_ROOT": "/path/to/vcpkg" }
    }
  ],
  "buildPresets": [
    { "name": "local-tls", "configurePreset": "local-tls" }
  ]
}
```

此例使用 `build/local-tls/` 和 `dist/local-tls/`。编译命令数据库随 Ninja 预设生成；在编辑器里为 clangd 设置 `--compile-commands-dir=build/<preset>`，或在项目根目录放置指向该目录中 `compile_commands.json` 的符号链接。仓库的 `.clangd` 不再强制所有文件使用某个交叉编译器。

This example uses `build/local-tls/` and `dist/local-tls/`. Ninja presets export a compilation database. Point clangd at it with `--compile-commands-dir=build/<preset>`, or symlink its `compile_commands.json` into the project root. The shared `.clangd` no longer forces a particular cross-compiler on every file.

## Windows 本机构建 / Native Windows builds

顶层 CMake 支持 MSVC。以下命令在安装了 Visual Studio C++ 工具链的 Windows 终端中构建非 TLS 版本；这些不是 Linux 交叉编译预设。

The top-level CMake supports MSVC. In a Windows terminal with the Visual Studio C++ toolchain installed, use the following for a TCP-only build. These commands do not use the Linux cross-compilation presets.

```sh
cmake -S . -B build/windows -A x64 -DBUILD_TESTING=OFF -DWIN32MQTT_ENABLE_TLS=OFF
cmake --build build/windows --config Release
cmake --install build/windows --config Release --prefix dist/windows
```

开发输出为 `build/windows/bin/Release/`。MSVC 和真实 Windows 运行仍需在 Windows 环境中验证。

Development output is in `build/windows/bin/Release/`. MSVC builds and actual Windows execution need validation on Windows.

## OpenSSL 与运行文件 / OpenSSL and runtime files

应用使用标准 `find_package(OpenSSL)` 和导入目标。除 vcpkg 预设外，也可以使用 `OPENSSL_ROOT_DIR` 指定目标平台的 OpenSSL 安装。

The app uses standard `find_package(OpenSSL)` and imported targets. Outside the vcpkg presets, `OPENSSL_ROOT_DIR` can point to an OpenSSL installation for the target platform.

- 静态链接或由开发者管理 DLL：保持 `WIN32MQTT_BUNDLE_OPENSSL=OFF`。动态链接时仍需自行确保 DLL 可被加载；此选项不会消除运行时依赖。
- 自动复制和安装 DLL：设为 ON。默认使用 vcpkg triplet 的 `bin/`；也可以通过 `WIN32MQTT_OPENSSL_RUNTIME_DIR` 指定目录，其中必须恰有一个 `libssl-*.dll` 和一个 `libcrypto-*.dll`。
- MSVC 使用 vcpkg 时，Debug 选择 `debug/bin/`，其他配置选择 `bin/`。显式指定 DLL 目录时，开发者负责确保架构、链接库和配置匹配；必要时分开建立 Debug/Release 构建目录。
- TLS 应用每次构建都会将 CA 和启用打包的 DLL 复制到 EXE 的实际目录。修改 CA 或移走复制的 DLL 后，重新构建应用即可恢复文件，不需要修改源码触发重新链接。

For static linkage or externally managed DLLs, leave `WIN32MQTT_BUNDLE_OPENSSL=OFF`. Dynamically linked builds still require their DLLs at runtime. With bundling ON, vcpkg's triplet `bin/` is used unless overridden; a custom directory must contain exactly one `libssl-*.dll` and one `libcrypto-*.dll`. MSVC/vcpkg Debug builds use `debug/bin/`. For an explicit directory, ensure the architecture, linked libraries, and configuration match; use separate Debug/Release build trees if needed.

Every TLS app build stages the CA and any bundled DLLs beside the actual EXE, including configuration subdirectories. Rebuilding restores removed runtime files and updates the CA without requiring a source change or relink. The runtime staging target serves the application; manually enabled Windows tests may need their own DLL search path configured.

## 运行时边界 / Runtime boundaries

网络操作集中在会话线程，UI 每 50 ms 从有界队列取最多 64 条事件并批量更新日志。普通命令最多 256 条、1 MiB 字符串内容；断开有预留位置，Stop 不依赖入队。普通事件最多 512 条、1 MiB，最新连接状态独立保留。日志最多 1000 条、256 Ki UTF-16 单位，单条最多 8192 单位。

Network work belongs to the session thread. The UI polls up to 64 events every 50 ms and refreshes logs in batches. The waiting command queue allows 256 ordinary commands and 1 MiB of strings, with a reserved Disconnect slot; Stop bypasses the queue. Ordinary events are limited to 512 entries and 1 MiB, with the latest connection state retained separately. Logs retain up to 1000 records and 256 Ki UTF-16 units, with an 8192-unit per-record limit.

连接阶段的 DNS/TCP/TLS/CONNACK 期限分别为 5/10/10/10 秒。取消标识属于单次连接请求；正常断开最多给 DISCONNECT 1 秒发送时间。这些不是整个 GUI 操作的硬实时保证，也不代表已实现所有连接存活检测。测试范围与限制见[测试说明](../tests/README.md)。

DNS/TCP/TLS/CONNACK deadlines are 5/10/10/10 seconds. Cancellation belongs to individual connection attempts; graceful shutdown allows up to one second to send DISCONNECT. These are not hard real-time GUI guarantees or a claim of complete connection-liveness detection. See the [test guide](../tests/README.md) for validation boundaries.
