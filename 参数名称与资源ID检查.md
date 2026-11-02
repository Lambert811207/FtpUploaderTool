# 参数名称与资源 ID 检查

> 历史资料：本文记录特定时期的设计、分析或验证，可能与当前实现不一致。当前使用说明见 [README](README.md)，测试入口见 [TESTING](TESTING.md)。

更新日期：2026-10-28。检查主窗口、任务配置、系统设置的参数名称、选项、说明、校验提示和资源 ID。

日志格式更新：运行、操作及上传明细统一使用 TXT。系统设置的归档数量说明已改为系统 TXT 与每个任务 TXT 分别计数，上传明细和任务运行日志共用一个系列；不再生成或管理 CSV，已有 CSV 保留。资源 ID 与界面布局不因日志格式调整而改变。

最新更新：新增“自动删除旧空文件夹”及天数参数，分组更名为“本地扫描及清理”，原有控件位置不变。当前任务窗口 74 个控件，全部对话框 100 个控件，应用资源定义 106 个。主窗口仍固定大小且无底部汇总状态栏；此前删除的“仅上传近期修改文件”没有恢复，文件上传仍不按修改时间新旧筛选。

空目录清理参数位于“本地扫描及清理”组：开关 IDC_CHK_DELETE_OLD_EMPTY_DIRS=1442；“超过”标签 IDC_STATIC_EMPTY_DIR_AGE_DAYS=1443；天数输入 IDC_EDIT_EMPTY_DIR_AGE_DAYS=1444；单位“天” IDC_STATIC_EMPTY_DIR_AGE_DAYS_UNIT=1445。原分组 ID 改为 IDC_GRP_LOCAL_SCAN_AND_CLEANUP，数值 1413 保留；资源编辑器下一控件值为 1446。

默认关闭，天数默认 7，范围 1–36500；关闭时关联参数禁用且不校验其编辑文本，旧配置缺少字段时自动使用关闭/7 天。tip 说明按文件夹最后修改时间、运行时每小时检查、清理范围、根目录保留和删除失败跳过；保存参数和清理执行结果均记录操作日志。

## 当前易读名称

针对仍需猜测含义的参数，再按本地/服务器、适用对象及触发条件调整如下；下方早期记录中与本表不同的名称，以本表为准。

| 本轮调整前 | 当前名称或选项 | 对应资源 ID 示例 |
|---|---|---|
| 服务端口 | 服务器端口 | IDC_EDIT_FTP_SERVER_PORT |
| 被动模式 | 本软件发起数据连接（被动模式） | IDC_CHK_CLIENT_INITIATES_DATA_CONNECTION |
| 监控目录 | 本地监控目录 | IDC_EDIT_LOCAL_WATCH_DIR |
| 远程目录 | 服务器上传目录 | IDC_EDIT_SERVER_UPLOAD_DIR |
| 包含规则 | 上传文件名规则 | IDC_EDIT_UPLOAD_FILENAME_MASK |
| 排除规则 | 排除文件名规则 | IDC_EDIT_SKIP_FILENAME_MASK |
| 扫描子目录 | 扫描本地子文件夹 | IDC_CHK_SCAN_LOCAL_SUBDIRECTORIES |
| 扫描间隔 | 本地文件扫描间隔 | IDC_EDIT_SCAN_INTERVAL_SEC |
| 文件稳定等待 | 上传前文件不变时间 | IDC_EDIT_PREUPLOAD_UNCHANGED_SEC |
| 失败次数上限 | 单文件失败上限 | IDC_EDIT_FILE_FAILURE_LIMIT |
| 上传后处理 | 上传成功后 | IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION |
| 删除、移动到备份、保留 | 删除本地文件、移到备份目录、保留本地文件 | 同上 |
| 备份目录 | 本地备份目录 | IDC_EDIT_LOCAL_BACKUP_DIR |
| 远程同名处理 | 服务器同名文件处理 | IDC_CMB_REMOTE_NAME_CONFLICT_POLICY |
| 覆盖、改名加时间标识、跳过并搁置 | 覆盖服务器文件、改名后上传、不上传该文件 | 同上 |
| 临时名上传 | 先用临时文件名上传 | IDC_CHK_USE_TEMP_FILENAME |
| 临时文件后缀 | 临时文件名后缀 | IDC_EDIT_TEMP_FILENAME_SUFFIX |
| 校验文件大小 | 上传后核对大小 | IDC_CHK_VERIFY_UPLOADED_FILE_SIZE |
| 心跳发送间隔 | 连接状态检查间隔 | IDC_EDIT_CONNECTION_CHECK_INTERVAL |

易读性调整曾同步更名 23 个资源符号及所有代码引用，标签、输入框和单位 ID 对应一致，原数值不变。参数警告与 tip 同步使用新名称，并说明空值、0、不上传以及本地文件删除/移动/保留的含义；随后按用户要求移除了文件修改时间新旧筛选，默认排除规则为空、默认删除本地文件等其他行为保持。

真实窗口的标签、复选框及下拉选项文字宽度、全部控件边界和重叠检查通过，参数联动、测试连接和保留文件历史回归通过。此前四种构建、355 项参数检查、主窗口新增/启停和日志提示回归均通过；后续功能验证见文末。当前 100 个控件、106 个资源定义，无重复、默认或遗留 ID。

本轮原始证据：

```text
C:\Users\LC\AppData\Local\Temp\FtpUploaderToolPlainLabels_a04c4dfc065242e98d1b176a857233d2
```

## 此前名称调整记录

| 原名称 | 当前名称 | 对应资源 ID 示例 |
|---|---|---|
| 地址 | 服务器地址 | IDC_EDIT_FTP_SERVER_ADDRESS |
| 端口 | 服务端口 | IDC_EDIT_FTP_SERVER_PORT |
| 用户、密码 | 登录账号、登录密码 | IDC_EDIT_FTP_LOGIN_USERNAME、IDC_EDIT_FTP_LOGIN_PASSWORD |
| 编码格式 | 文件名编码 | IDC_CMB_FILENAME_ENCODING |
| 本地目录 | 监控目录 | IDC_EDIT_LOCAL_WATCH_DIR |
| 包含子目录 | 扫描子目录 | IDC_CHK_SCAN_SUBDIRECTORIES |
| 包含、排除 | 包含规则、排除规则 | IDC_EDIT_INCLUDE_FILE_MASK、IDC_EDIT_EXCLUDE_FILE_MASK |
| 扫描秒 | 扫描间隔（秒） | IDC_EDIT_SCAN_INTERVAL_SEC |
| 稳定秒 | 文件稳定等待（秒） | IDC_EDIT_FILE_STABLE_WAIT_SEC |
| 重试次数 | 失败次数上限（次） | IDC_EDIT_FILE_FAILURE_LIMIT |
| 无独立标签的后处理选项 | 上传后处理 | IDC_STATIC_UPLOAD_POST_ACTION、IDC_CMB_UPLOAD_POST_ACTION |
| 远程同名 | 远程同名处理 | IDC_CMB_REMOTE_NAME_CONFLICT_POLICY |
| 改名加时间戳 | 改名加时间标识 | 同上，选项值不变 |
| 临时名 | 临时名上传 | IDC_CHK_USE_TEMP_FILENAME |
| SIZE 校验 | 校验文件大小 | IDC_CHK_VERIFY_FILE_SIZE |
| 最近 N 天（0 不限） | 已删除修改时间新旧筛选功能 | 对应资源 ID 已删除 |
| 开机自动运行（当前用户） | 当前用户登录后自动运行 | IDC_CHK_AUTORUN_AT_LOGIN |
| 启动后最小化到托盘 | 启动时隐藏到托盘 | IDC_CHK_START_HIDDEN_TO_TRAY |
| 运行期间禁止系统睡眠 | 运行时阻止自动睡眠 | IDC_CHK_PREVENT_AUTO_SLEEP |
| 单个日志上限（MB） | 单个日志大小上限（MB） | IDC_EDIT_LOG_FILE_MAX_SIZE_MB |
| 最多保留归档（个） | 归档文件上限（个） | IDC_EDIT_LOG_ARCHIVE_FILE_LIMIT |
| 界面日志行数 | 界面日志行数上限 | IDC_EDIT_UI_LOG_LINE_LIMIT |

上一轮已经改为完整名称的六项，本轮同步更正输入框、标签及单位标签的资源 ID：

| 参数 | 输入框 ID |
|---|---|
| 连接建立超时 | IDC_EDIT_CONNECTION_ESTABLISH_TIMEOUT |
| 数据收发超时 | IDC_EDIT_DATA_IO_TIMEOUT |
| 心跳发送间隔 | IDC_EDIT_HEARTBEAT_INTERVAL |
| 初始重连间隔 | IDC_EDIT_RECONNECT_INITIAL_INTERVAL |
| 最大重连间隔 | IDC_EDIT_RECONNECT_MAX_INTERVAL |
| 空闲断开时间 | IDC_EDIT_IDLE_DISCONNECT_TIMEOUT |

相应的 IDC_STATIC_ 和单位标签使用相同用途名称。任务名称输入框也统一为 IDC_EDIT_TASK_NAME，与 IDC_STATIC_TASK_NAME 对应。名称已明确的其他控件保持原名。

## 语义核对

- “失败次数上限”包含首次可计数上传失败；网络连接中断等错误走重连，不占用该计数。没有改变重试逻辑或默认值。
- 文件名编码影响路径和文件名，不转换文件内容；自动模式按服务器是否声明 UTF-8 支持选择 UTF-8 或 GBK。
- 改名使用源文件修改时间生成标识，不是当前上传时间。服务器同名时始终按选定策略处理，大小相同也不能认定成功；改名目标也已存在时暂停并保留源文件。只有实际上传成功后才写入成功历史，相关 tip 已更新。
- 文件大小校验使用 FTP SIZE 命令，不是内容哈希校验。
- 修改时间新旧筛选功能已删除，不再限制旧文件参与上传；最后修改时间仍用于文件稳定性、上传历史及目录日期占位符。
- 自动运行由当前用户登录触发；禁止睡眠选项阻止自动睡眠，不拦截用户手动睡眠或关机。

## 资源及兼容性

- 参数名称调整共重命名 63 个资源符号，代码、消息映射、数据读写、提示和联动引用均已更新。后续分组调整将路径组更名为 IDC_GRP_MONITOR_AND_UPLOAD_PATHS，数值 1407 保持不变；业务源码没有旧 ID 引用或旧别名。
- 原有 ID 数值逐项保持不变，继续使用十进制。
- “上传成功后”标签 IDC_STATIC_UPLOAD_SUCCESS_LOCAL_ACTION 的数值为 1435。原单位及后缀标签使用 1436–1440；已删除占位符说明 1410 及旧修改时间筛选单位 1439，不再使用。上传处理分组为 1441，空目录清理控件为 1442–1445，资源编辑器下一控件值为 1446。
- 当前 3 个对话框共 100 个控件：主窗口 15、任务配置 74、系统设置 11；应用资源/命令共 106 个定义。已删除底部状态栏 1025，以及修改时间新旧筛选的输入框 1122、标签 1418、单位 1439；空目录清理新增 1442–1445，其余数值不变，资源编辑器下一控件值为 1446。
- 仅在 .rc 中调整相关控件以容纳完整文字，主窗口资源布局保持不变。
- 配置结构、INI 键、默认值及上传、扫描、历史、日志归档等核心逻辑与本轮修改前一致。

## 最新分组和单位

| 分组 | 参数及操作 |
|---|---|
| FTP 服务器 | 地址、端口、登录账号/密码、主动/被动模式、文件名编码 |
| 监控及上传路径 | 本地监控目录、服务器上传目录、测试连接及结果区域 |
| 本地扫描及清理 | 上传/排除文件名规则、本地子文件夹、扫描间隔、上传前文件不变时间、旧空文件夹清理开关及天数 |
| 文件上传及处理 | 单文件失败上限、成功后的本地文件处理、本地备份目录、服务器同名文件处理、临时文件名及后缀、上传后核对大小 |
| 连接维护及重连 | 建立连接及数据收发超时、连接状态检查间隔、初始/最大重连间隔、空闲断开时间 |

任务名称位于分组上方，确定/取消位于分组下方；系统设置保留程序全局参数。本地备份目录作为成功后处理的附属项保留在上传处理组；测试连接按用户要求保留在路径组。扫描和上传已拆分，连接维护明确覆盖保活和空闲断开。

对应资源为 IDC_GRP_LOCAL_SCAN_AND_CLEANUP=1413、IDC_GRP_UPLOAD_FILE_HANDLING=1441、IDC_GRP_CONNECTION_MAINTENANCE_RECONNECT=1419；原分组数值保留，旧符号移除。任务对话框高度仍为 379 个对话框单位，空目录参数放在原有空白区域，其他控件不移动。所有位置均在 .rc 中定义，未增加代码布局。

扫描间隔、上传前文件不变时间、单文件失败上限分别在输入框右侧显示独立的秒、秒、次标签，与连接维护区域一致。临时文件名后缀有独立标签，与编辑框共同响应临时名上传的启用状态。

原“支持 {yyyy}{MM}…”整行说明及其资源 ID 已删除；占位符功能保留，用法移入远程目录的鼠标提示。扫描范围、过滤规则、扫描间隔、备份目录、后缀及测试按钮的提示均已同步更新。

## 验证

Win32/x64 的 Debug、Release 四种构建均通过，无编译警告；四个 EXE 的对话框控件顺序与 ID 数值均与源码一致。

真实 MFC 对话框的 105 项检查通过：标签、下拉选项和复选框文字完整显示，标签与输入框不重叠，改名后的上传策略及临时名选项能正确联动，系统设置的数值和布尔值均读取正确字段。连接测试不弹开始确认，取消正常，参数错误仍单独提示；保留策略读取磁盘历史后不重复上传，修改文件后再次上传，临时文件最终为 0。

同步新名称后的 355 项参数检查通过。真实主窗口回归通过：新增保持停止、勾选持久化、批量启停和日志单元格提示正常。检查进程隐藏运行，使用隔离配置和本机回环 FTP，未操作宿主机鼠标键盘、用户配置或虚拟机，没有增加测试工程。

此前拆分时，对当时的 73 个任务控件检查了预期分组、窗口边界及相互重叠，并验证单位、鼠标提示和标签联动。后续移除了 3 个修改时间筛选控件并新增 4 个空目录清理控件，当前为 74 个；最新验证见文末。主窗口和系统设置的资源布局保持不变。

完整符号对照、原始日志和验证代码位于：

```text
C:\Users\LC\AppData\Local\Temp\FtpUploaderToolParameterNames_140521ea024c456ea49d52b078a5c190
```

分组调整的原始日志、修改前备份、布局检查及离屏绘制文件位于：

```text
C:\Users\LC\AppData\Local\Temp\FtpUploaderToolTaskGroups_00c461e8fe804acbb1f4a91a64d99eb8
```

最新扫描/上传分组拆分的证据：

```text
C:\Users\LC\AppData\Local\Temp\FtpUploaderToolSplitGroups_adfacd147a0442c4b102193472bbfe92
```

## 修改时间新旧筛选移除验证

已删除模型字段 m_unMaxFileAgeDays、范围常量、INI 读写、界面绑定与提示、单项校验及扫描判断；同步删除仅供该判断使用的当前时间变量。源代码中没有功能残留引用。

保留本轮修改前其他控件的位置及属性，只移除对应的标签、输入框和单位三个控件，其余资源数值保持不变。

37 项专项检查通过：旧配置中的 MaxFileAgeDays=1 或无效字符串均可正常加载；200 天前的文件、子文件夹中 365 天前的文件和新文件均上传成功，服务端内容一致且临时文件为 0。重新启动工作线程后，磁盘上传历史使三份文件全部跳过；重新保存的配置不再包含旧字段。剩余 331 项参数检查及四种构建均通过，无编译警告；四个 EXE 的资源与源码一致。

证据目录：

```text
C:\Users\LC\AppData\Local\Temp\FtpUploaderToolRemoveAgeFilter_2a5179443f3941259357780e047895ea
```

## 自动删除旧空文件夹验证

2026-10-28 完成。398 项专项检查、其余 331 项参数检查及固定主窗口回归均通过；四种构建无警告，资源核对通过。验证包含真实旧空目录删除、根目录/文件/新目录/链接目录保护、占用和删除前刚写入文件的竞争、取消、不同天数、旧配置默认关闭、界面联动和提示、启动及每小时调度，以及参数保存与清理结果日志。新增四个控件在扫描组内显示完整且不重叠；其余控件位置保留。

原始证据：

```text
C:\Users\LC\AppData\Local\Temp\FtpUploaderToolEmptyDirCleanup_1b0b3540db3f46bca76287e63a7f0aab
```
