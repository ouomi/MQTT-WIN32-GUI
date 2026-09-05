# 配置文件 / Configuration

程序读取 EXE 旁的 `WIN32-MQTT.ini`。文件使用 **UTF-8**，允许 UTF-8 BOM，支持 LF 和 CRLF 换行。保存时写入无 BOM 的 UTF-8 文本。

The application reads `WIN32-MQTT.ini` beside the EXE. Use **UTF-8**, with an optional UTF-8 BOM and LF or CRLF line endings. Saves use UTF-8 without a BOM.

```ini
[Display]
Language=Chinese
[Connection]
ServerUri=mqtt://localhost:1883
ClientId=mqttwin-client
[Window]
Width=900
Height=600
SubscriptionPanelWidth=380
[Subscriptions]
Count=2
Topic0=设备/温度
Active0=1
Topic1=sensors/+
Active1=0
```

## 文本规则 / Text rules

- 节名、键名和枚举值区分大小写；示例中的固定字段全部必需。
- `ServerUri`、`ClientId` 和 `TopicN` 中，第一个 `=` 后的内容全部是实际值，不去掉首尾空格，不去掉引号，不解释反斜杠转义。普通文本直接写入即可，**不要额外加引号**。
- 支持空行，以及第一个非空白字符为 `;` 或 `#` 的整行注释。没有行尾注释：`Topic0=sensors/#` 中的 `#` 是 Topic 的一部分。
- 允许中文和其他有效 Unicode 字符；值中不允许换行、制表符、NUL 等控制字符，也不允许 Unicode 行分隔符或嵌入的 BOM。每个文本值最多 4088 个 UTF-8 字节。
- 自动保存会重新生成配置文件，因此手写注释和排版不会保留。

Section names, keys and enum values are case-sensitive. All fixed fields shown above are required. For `ServerUri`, `ClientId` and `TopicN`, everything after the first `=` is literal: spaces, quotes, backslashes, semicolons and equals signs are preserved. Do not add quotes around a value unless the quotes are part of it. Only whole-line comments starting with `;` or `#` after optional whitespace are supported. There are no inline comments or escape sequences. Values must be valid Unicode, contain no control characters, Unicode line separators or embedded BOM, and fit within 4088 UTF-8 bytes. Saving regenerates the document and does not retain comments or formatting.

## 字段检查 / Validation

| 字段 / Field | 允许的值 / Allowed values |
| --- | --- |
| `Language` | `Chinese` 或 / or `English` |
| `ServerUri` | 空值，或应用支持的 `mqtt://host[:port]` / `mqtts://host[:port]` 地址；端口为 1–65535 / Empty, or an MQTT URI accepted by the application; port 1–65535 |
| `ClientId` | 可读文本，允许空值 / Literal text; may be empty |
| `Width`、`Height`、`SubscriptionPanelWidth` | 0–32767 的整数；0 使用默认值，显示时仍受窗口和面板最小尺寸限制 / Integers from 0 to 32767; 0 selects defaults; layout still enforces minimum sizes |
| `Count` | 0–256 的整数 / Integer from 0 to 256 |
| `TopicN` | 非空、合法且不重复的订阅过滤器 / Nonempty, valid, unique subscription filter |
| `ActiveN` | `0` 或 / or `1` |

订阅索引必须从 0 连续到 `Count-1`，每条订阅都必须同时包含 `TopicN` 和 `ActiveN`。`Count=0` 时不能再有订阅条目。

Subscription indexes must run from 0 through `Count-1`, with both `TopicN` and `ActiveN` present. No indexed entries are allowed when `Count=0`.

## 启动和保存 / Startup and saving

创建主窗口前检查整个配置文件。文件为空、超过 4 MiB、无法读取、编码错误、存在未知或重复节/字段、字段缺失或值无效时，会显示文件路径及具体原因并拒绝启动。原文件不会被覆盖。修正文件后重新启动，或者先将文件重命名，再启动程序生成默认配置。

没有配置文件属于正常首次启动，会使用默认设置并生成客户端 ID。已有文件中的空 `ClientId` 会按空值保留。

不再支持旧配置编码，也没有迁移逻辑。旧的 UTF-16 文件和带 `[Format]` 的配置需要手工重建为上述格式，或重命名后让程序重新生成。

界面输入地址时，尚未有效的地址只保留在输入框中；自动保存使用最近一次通过检查的地址。尚未填写地址时允许保存空值。保存仍使用先写临时文件、再替换的方式，避免写入失败破坏原配置。

Before creating the main window, the application validates the complete document. An empty, oversized (over 4 MiB), unreadable or malformed file, invalid encoding, unknown or duplicate sections/keys, missing fields or invalid values stop startup with the path and a specific error. The file is left untouched. Fix it, or rename it and restart to generate defaults.

A missing file is a normal first run and generates a client ID. An explicitly empty client ID in an existing file is preserved. Previous encodings and migration are unsupported: recreate UTF-16 files or documents containing `[Format]` using the format above, or rename them to generate defaults.

Incomplete or invalid server URI edits stay in the input control; autosave retains the most recent valid URI. An empty URI is valid during initial setup. Saves still write a temporary file before replacing the destination.
