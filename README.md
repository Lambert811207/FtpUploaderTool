# FtpUploaderTool（VS2010 + MFC）

这是按 `FTP自动上传工具_开发文档.md` 重建的 **Visual Studio 2010 SP1 / MFC Unicode 对话框程序**。解决方案和程序名均为 `FtpUploaderTool`，目标平台为 Win32，工程配置使用 v100、静态 MFC、`/MT`，最低系统版本为 Windows XP SP3。

## 构建

在安装 VS2010 的开发人员命令提示符中运行：

```bat
build.cmd Release
```

或者打开 [FtpUploaderTool.sln](FtpUploaderTool.sln)，选择 `Release|Win32` 后生成。输出文件为 [Release/FtpUploaderTool.exe](Release/FtpUploaderTool.exe)。测试程序为 `Release\FtpUploaderTool.Tests.exe`。

`build.cmd` 会调用 VS2010 的 x86 环境和 .NET Framework 4.0 MSBuild，不依赖新版本 .NET。项目源文件使用 UTF-8 BOM；资源脚本使用 UTF-16 LE，VS2010 可直接打开。

## 运行

程序在运行、保存配置和上传时，会按需在 exe 目录创建：

```text
Config\FtpUploaderTool.ini
Data\History_<任务 GUID>.txt
Log\yyyy-MM-dd\System.log / <任务>_<GUID前8位>.log/.csv
Dump\FtpUploaderTool_*.dmp
```

配置文件不存在时程序使用空任务列表启动；在界面点击“新增”填写服务器、账号、本地目录、远程目录和策略。密码保存为 Windows DPAPI 机器绑定密文，配置文件不保存明文。程序每个任务使用独立 `_beginthreadex` 工作线程，UI 线程只读取状态快照和有界日志队列。全部已配置任务在程序启动时自动启动，单个任务可用“启动/停止”控制，不再设置启用状态。

新配置使用 `TaskCount`、`Task0…` 和 `[Task_<GUID>]`；旧版 `ChannelCount`、`Channel0…`、`[Ch_<GUID>]` 仍可读取，保存时转为新格式。旧 `Enabled` 字段不再使用。

主界面支持：

- 新增、编辑任务；任务数量上限 32。
- 启动/停止选中任务、全部启动/停止。
- 连接状态、待传数、搁置数、今日成功/失败、当前进度和最后错误。
- 通配符过滤、子目录扫描、文件稳定时间和写锁判断。
- PASV/PORT、UTF-8/GBK、临时名上传、SIZE 校验、RNFR/RNTO 原子改名。
- 删除、移动到日期备份目录、保留并写历史记录三种后处理。
- 托盘隐藏、双击恢复、右键退出；关闭按钮和 Esc 只隐藏窗口。
- “测试连接”后台线程：登录、进入/创建目录、上传 16 字节测试文件、校验并删除。

界面不再提供复制任务、删除任务、重试失败或启用任务选项。失败文件达到重试上限后搁置；停止并重新启动该任务可重新尝试。

远程目录占位符：`{yyyy}`、`{MM}`、`{dd}`、`{HH}`、`{sub}`、`{pc}`；日期使用文件修改时间。数据连接和控制连接均有超时，停止事件以 200ms 分片检查；网络错误按 3/6/12/24/48/60 秒退避无限重连。日志线程不在工作线程写磁盘，队列有上限，磁盘故障不会阻塞上传。

## 原生回归测试

在 VS2010 工具链下执行：

```bat
build.cmd Release
Release\FtpUploaderTool.Tests.exe
```

当前 12/12 通过：配置 Unicode/DPAPI、新旧任务配置兼容、MFC 对话框资源及已删除控件检查、主窗口初始化/缩放/定时器、稳定文件与写锁、持久化历史、PASV/NAT/UTF-8 中文路径精确匹配、PORT/GBK/空文件、含零字节二进制文件逐字节一致、工作线程重启去重、删除/备份、传输断开重连、大小校验失败保护、550 后重启任务恢复。测试包含本地 FTP 协议夹具，真实 FileZilla/IIS/vsftpd 联调、XP 实机运行和 7×24 长稳仍需现场验收。

## 命名约定

资源 ID 使用控件类型前缀，例如 `IDD_DLG_MAIN`、`IDD_DLG_TASK`、`IDC_BTN_ADD`、`IDC_LIST_TASKS`、`IDC_EDIT_NAME`、`IDC_CMB_ENCODING`、`IDC_CHK_PASSIVE`。标准 MFC ID `IDOK`、`IDCANCEL`、`IDC_STATIC` 保持标准名称。

成员变量和文件作用域变量使用 `m_` + 类型前缀 + 驼峰名称，例如 `m_strRoot`、`m_listTasks`、`m_stConfig`；局部变量及参数使用类型前缀 + 驼峰名称，例如 `strPath`、`nIndex`、`bSuccess`、`pParent`。配置窗口代码位于 `TaskEditDlg.h/.cpp`。

## 本批边界

已实现 M1–M4 的核心配置、FTP、扫描、工作线程、日志和 MFC UI。尚未实现独立 `FtpGuard.exe` 守护进程、看门狗自动重建、崩溃后自动拉起、远程目录浏览以及 FTPS/SFTP/断点续传。原始设计文档保留在项目根目录，旧版 C# MVP 已移至 `D:\工作\小工具2_旧版CSharp归档_20261020`。
