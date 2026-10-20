# 工业现场 FTP 自动上传工具 开发文档

| 项目 | 内容 |
|---|---|
| 软件名称 | FtpUploader（工业现场多通道 FTP 自动上传工具） |
| 开发环境 | Visual Studio 2010 SP1，MFC 对话框程序 |
| 目标平台 | Windows XP SP3 / Win7 / Win10 / Win11，x86（可选 x64） |
| 字符集 | Unicode |
| 运行要求 | 7×24 小时无人值守连续运行 |
| 文档版本 | V1.0 |

## 目录

1. 概述
2. 需求说明
3. 总体设计
4. 工程结构与编译配置
5. 数据结构与配置文件
6. FTP 协议客户端模块（CFtpClient）
7. 连接工作线程与断线重连（CFtpWorker）
8. 本地目录监控与文件稳定性判断
9. 上传流程
10. 上传后处理与防重复上传
11. 日志模块
12. 界面设计
13. 线程间通信
14. 7×24 稳定性设计
15. 异常处理与错误码
16. 测试方案
17. 部署与运维
18. 开发计划
19. 附录

---

## 1. 概述

### 1.1 背景

生产线上的检测设备、PLC 上位机、视觉系统等会持续在本地目录生成数据文件（图片、CSV、报表等），需要实时上传到一台或多台 FTP 服务器，供 MES / 数据中心使用。现场网络环境不稳定（交换机重启、网线松动、服务器维护），要求上传工具能长期无人值守运行，断线后自动恢复，并且不丢文件、不重复传、不传半截文件。

### 1.2 设计目标

1. 一个程序内支持多个 FTP 连接（通道），每个通道独立线程、独立配置、独立日志，互不影响。
2. 监控本地目录，新文件写完后自动上传。
3. 网络中断、服务器重启后自动重连，无需人工干预。
4. 上传日志保存到文件，并在界面实时显示。
5. 程序本身连续运行数月不崩溃、不卡死、内存和句柄不增长。

### 1.3 术语

| 术语 | 说明 |
|---|---|
| 通道 / 连接 | 一组完整的上传配置（服务器 + 本地目录 + 远程目录 + 策略），对应一个工作线程 |
| 控制连接 | FTP 的命令连接（默认 21 端口） |
| 数据连接 | FTP 传输文件用的连接，PASV 模式下由客户端主动连服务器 |
| 稳定文件 | 大小和修改时间在设定时间内不再变化、且能独占读打开的文件 |
| 临时文件名 | 上传过程中使用的远程文件名（如 `xxx.csv.uploading`），传完后再改名 |

---

## 2. 需求说明

### 2.1 功能需求

| 编号 | 功能 | 说明 | 优先级 |
|---|---|---|---|
| F01 | 连接参数配置 | 手动输入服务器 IP 或域名、端口、用户名、密码、本地路径、远程路径 | 高 |
| F02 | 多连接管理 | 新增、编辑、删除、复制通道；单独启用/停用；数量建议上限 32 | 高 |
| F03 | 连接测试 | 编辑界面一键测试登录与远程目录可写 | 高 |
| F04 | 目录监控 | 定时扫描本地目录（可含子目录），按通配符过滤文件 | 高 |
| F05 | 文件稳定性判断 | 只上传已写完的文件，避免传半截 | 高 |
| F06 | 自动上传 | 稳定文件进入队列，按顺序上传 | 高 |
| F07 | 断线自动重连 | 任何网络错误都触发重连，退避间隔，无限重试 | 高 |
| F08 | 心跳保活 | 空闲时定期发 NOOP，提前发现断线 | 高 |
| F09 | 远程目录自动创建 | 远程目录不存在时逐级创建，支持 `{yyyy}{MM}{dd}` 日期占位符 | 中 |
| F10 | 上传后处理 | 每通道可选：删除 / 移到备份目录 / 保留 | 高 |
| F11 | 防重复上传 | “保留”模式下记录已上传文件，重启程序后依然有效 | 高 |
| F12 | 原子上传 | 先传临时名，完成并校验大小后再改名 | 高 |
| F13 | 日志保存 | 运行日志 + 上传记录（CSV），按天分文件，自动清理过期日志 | 高 |
| F14 | 界面日志显示 | 实时滚动显示，按通道过滤，错误标红 | 高 |
| F15 | 状态显示 | 每通道显示连接状态、今日成功/失败数、待传数、最后错误 | 高 |
| F16 | 托盘运行 | 最小化到托盘，托盘图标反映整体状态 | 中 |
| F17 | 开机自启 | 可选开机自动运行并自动启动所有已启用通道 | 中 |
| F18 | 密码加密存储 | 配置文件中不出现明文密码 | 中 |
| F19 | 主动/被动模式 | 默认 PASV，可选 PORT | 中 |
| F20 | 中文文件名 | 支持 UTF-8 和 GBK 两种服务器编码 | 中 |

### 2.2 非功能需求

| 编号 | 类别 | 指标 |
|---|---|---|
| N01 | 连续运行 | ≥ 90 天不重启，无崩溃、无卡死 |
| N02 | 资源 | 内存、句柄数、GDI 对象数长期稳定，不随时间增长 |
| N03 | 恢复时间 | 网络恢复后 ≤ 1 个重连周期（默认最大 60 秒）恢复上传 |
| N04 | 数据完整性 | 不丢文件、不传残缺文件；断线中断的文件下次重传 |
| N05 | 隔离性 | 单个通道异常（卡死、服务器不可达）不影响其他通道和界面 |
| N06 | 界面响应 | 任何网络操作不得在 UI 线程执行，界面始终可操作 |
| N07 | CPU | 空闲时 < 1%，上传时单通道 < 5% |
| N08 | 吞吐 | 千兆局域网单通道 ≥ 30 MB/s |
| N09 | 可追溯 | 每个文件的上传结果（时间、大小、耗时、结果、错误）可查 |

### 2.3 不在本期范围

FTPS/SFTP 加密传输、下载功能、远程文件浏览管理、断点续传（REST）。第 19 章附录给出扩展建议。

---

## 3. 总体设计

### 3.1 架构图

```
┌──────────────────────────── UI 线程（MFC 主线程）────────────────────────────┐
│  CFtpUploaderDlg                                                              │
│   ├─ 通道列表 CListCtrl（定时器 500ms 读取各通道状态快照）                     │
│   ├─ 日志列表 CListCtrl（定时器 200ms 从 UI 日志队列批量取出）                 │
│   ├─ CConnEditDlg（新增/编辑通道）                                             │
│   └─ 托盘图标 / 菜单                                                           │
└───────────────┬──────────────────────────────────────────▲───────────────────┘
                │ Start/Stop/UpdateConfig                  │ 状态快照 / 日志
┌───────────────▼──────────────────────────────────────────┴───────────────────┐
│  CChannelManager（通道管理器，持有所有 CFtpWorker）                            │
└───────┬───────────────────────┬───────────────────────────┬──────────────────┘
        │                       │                           │
┌───────▼───────┐       ┌───────▼───────┐           ┌───────▼───────┐
│ CFtpWorker #1 │       │ CFtpWorker #2 │    ...    │ CFtpWorker #N │  每通道 1 个线程
│  状态机        │       │               │           │               │
│  CDirScanner  │       │               │           │               │
│  CFtpClient   │       │               │           │               │
│  CUploadHist  │       │               │           │               │
└───────┬───────┘       └───────────────┘           └───────────────┘
        │ 写日志（只入队，不碰磁盘）
┌───────▼──────────────────────────────────────────────────────────────────────┐
│  CLogger（单例，独立日志线程负责写文件、滚动、清理）                           │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  CWatchdog（看门狗定时器：检测工作线程心跳，卡死则强制重建线程）              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 线程模型

| 线程 | 数量 | 职责 | 禁止事项 |
|---|---|---|---|
| UI 主线程 | 1 | 界面、配置编辑、定时刷新 | 禁止任何网络/阻塞磁盘操作 |
| 通道工作线程 | N | 扫描目录、FTP 连接、上传、重连、后处理 | 禁止直接操作任何窗口/控件 |
| 日志线程 | 1 | 日志写盘、按天切换、清理过期文件 | 禁止调用会再次写日志的函数（防递归） |
| 连接测试线程 | 临时 | 编辑界面“测试连接”时创建，完成即退出 | 同工作线程 |

设计原则：

1. 每个通道“一个线程 + 一个 CFtpClient 实例 + 一份配置副本”，彼此之间零共享，一个通道卡死或异常不会波及其他通道。
2. 工作线程与 UI 之间只通过两样东西交互：加锁的状态快照结构体、有上限的日志队列。工作线程从不 SendMessage 到 UI（避免 UI 忙时工作线程被反向阻塞，也避免关闭程序时死锁）。
3. 所有阻塞操作都有超时；所有等待都同时等“停止事件”，保证停止通道时 ≤ 1 秒响应。
4. 配置修改采用“停止 → 替换配置 → 启动”的方式，工作线程运行期间配置只读。

### 3.3 VS2010 技术约束

VS2010 只支持部分 C++11：可用 `auto`、lambda、`nullptr`、`std::shared_ptr`、`std::unique_ptr`、右值引用；**不可用** `std::thread`、`std::mutex`、`std::atomic`、`std::chrono`，也不支持范围 for 循环（`for (auto& x : v)`，VS2012 才支持）、初始化列表、`= delete`。因此本项目统一使用 Win32 原语：

| 用途 | 使用 |
|---|---|
| 线程 | `_beginthreadex`（不要用 `CreateThread`，CRT 每线程数据会泄漏；也可用 `AfxBeginThread`） |
| 互斥 | `CRITICAL_SECTION`，配合 RAII 包装类 `CAutoLock` |
| 事件 | `CreateEvent`（手动复位的停止事件） |
| 原子计数 | `InterlockedIncrement` / `InterlockedExchange` |
| 计时 | `GetTickCount()` 差值（DWORD 无符号相减可正确处理 49.7 天回绕） |

```cpp
// 通用 RAII 锁，所有共享数据访问都用它，防止异常/提前 return 导致忘记解锁
class CCritSec {
public:
    CCritSec()  { ::InitializeCriticalSectionAndSpinCount(&m_cs, 4000); }
    ~CCritSec() { ::DeleteCriticalSection(&m_cs); }
    void Lock()   { ::EnterCriticalSection(&m_cs); }
    void Unlock() { ::LeaveCriticalSection(&m_cs); }
private:
    CRITICAL_SECTION m_cs;
    CCritSec(const CCritSec&); CCritSec& operator=(const CCritSec&); // 禁止拷贝
};

class CAutoLock {
public:
    explicit CAutoLock(CCritSec& cs) : m_cs(cs) { m_cs.Lock(); }
    ~CAutoLock() { m_cs.Unlock(); }
private:
    CCritSec& m_cs;
};

// 计时工具：判断从 dwStart 起是否已过去 dwMs 毫秒（自动处理 GetTickCount 回绕）
inline bool TickElapsed(DWORD dwStart, DWORD dwMs) {
    return (DWORD)(::GetTickCount() - dwStart) >= dwMs;
}
```

> 注意：所有“间隔类”计时（重连间隔、心跳、稳定时间）一律用 `GetTickCount` 差值，**不要用系统时间**。工业现场经常有 NTP 校时或人工改时间，用系统时间计算间隔会导致心跳或重连长时间不触发。

---

## 4. 工程结构与编译配置

### 4.1 创建工程

VS2010 → 新建项目 → MFC 应用程序 → 应用程序类型选“基于对话框”，MFC 使用方式选“在静态库中使用 MFC”（现场电脑不用装 VC 运行库，直接拷贝 exe 即可运行），字符集 Unicode，取消“ActiveX 控件”，勾选“Windows 套接字”可不选（我们直接用 Winsock2）。

### 4.2 目录与文件

```
FtpUploader/
├─ FtpUploader.sln
├─ FtpUploader/
│  ├─ FtpUploader.h/.cpp          应用类：单实例、崩溃处理、Winsock 初始化
│  ├─ FtpUploaderDlg.h/.cpp       主界面
│  ├─ ConnEditDlg.h/.cpp          通道编辑对话框
│  ├─ SettingsDlg.h/.cpp          全局设置（开机自启、日志保留天数等）
│  ├─ Common/
│  │  ├─ Sync.h                    CCritSec / CAutoLock / TickElapsed
│  │  ├─ StrUtil.h/.cpp            UTF-8/GBK/Unicode 转换、路径处理、通配符匹配
│  │  ├─ Crypto.h/.cpp             密码加解密
│  │  └─ CrashDump.h/.cpp          未处理异常 → MiniDump
│  ├─ Config/
│  │  ├─ ConnConfig.h              通道配置结构体
│  │  └─ ConfigStore.h/.cpp        INI 读写
│  ├─ Ftp/
│  │  ├─ FtpSocket.h/.cpp          带超时的 socket 封装
│  │  └─ FtpClient.h/.cpp          FTP 协议实现
│  ├─ Core/
│  │  ├─ FtpWorker.h/.cpp          通道工作线程 + 状态机
│  │  ├─ DirScanner.h/.cpp         目录扫描 + 稳定性判断
│  │  ├─ UploadHistory.h/.cpp      已上传记录（防重复）
│  │  ├─ ChannelManager.h/.cpp     管理所有通道
│  │  └─ Watchdog.h/.cpp           线程心跳监测
│  ├─ Log/
│  │  └─ Logger.h/.cpp             日志单例 + 日志线程
│  └─ res/                          图标（托盘：绿/黄/红/灰 四种）
└─ Release/
```

### 4.3 编译配置（Release）

| 配置项 | 值 | 原因 |
|---|---|---|
| 常规 → MFC 的使用 | 在静态库中使用 MFC | 免装运行库 |
| C/C++ → 代码生成 → 运行库 | /MT | 同上 |
| C/C++ → 优化 | /O2 | |
| C/C++ → 调试信息格式 | 程序数据库 /Zi | Release 也生成 PDB，崩溃 dump 才能定位到行 |
| 链接器 → 调试 → 生成调试信息 | 是 | 同上，PDB 每个版本存档，不随程序发布 |
| C/C++ → 代码生成 → 启用 C++ 异常 | 是 /EHsc | 不要用 /EHa 让 `catch(...)` 吞掉访问违例；真正的崩溃交给 dump + 守护进程重启（第 14 章） |
| C/C++ → 预处理器 | `_WIN32_WINNT=0x0501;WINVER=0x0501` | 兼容 XP；若无 XP 机器可改 0x0601 |
| 链接器 → 输入 → 附加依赖项 | `ws2_32.lib;dbghelp.lib;crypt32.lib;shlwapi.lib` | Winsock、Dump、DPAPI、路径函数 |
| 链接器 → 系统 → 子系统 | Windows | |
| 清单 → 附加清单 | 可加 `requestedExecutionLevel = asInvoker` | 不要求管理员权限 |

> XP 兼容提示：VS2010 编译出的程序默认可在 XP SP3 运行。代码中不要使用 `GetTickCount64`、`inet_pton`、`CancelSynchronousIo` 等 Vista+ API，或者用 `GetProcAddress` 动态加载。

### 4.4 Winsock 初始化

在 `CFtpUploaderApp::InitInstance()` 中调用一次 `WSAStartup(MAKEWORD(2,2), &wsa)`，`ExitInstance()` 中调用 `WSACleanup()`。各线程不要重复初始化。

---

## 5. 数据结构与配置文件

### 5.1 通道配置结构体

```cpp
// ConnConfig.h
enum PostAction  { POST_DELETE = 0, POST_MOVE = 1, POST_KEEP = 2 };
enum RemoteExist { EXIST_OVERWRITE = 0, EXIST_RENAME = 1, EXIST_SKIP = 2 };
enum FtpEncoding { ENC_AUTO = 0, ENC_UTF8 = 1, ENC_GBK = 2 };

struct ConnConfig {
    // ---- 基本 ----
    CString strId;              // 唯一 ID（GUID），日志目录、历史记录文件以此命名，改名不影响
    CString strName;            // 显示名称，如“1号线视觉”
    BOOL    bEnabled;           // 是否启用
    // ---- 服务器 ----
    CString strHost;            // IP 或域名
    UINT    nPort;              // 默认 21
    CString strUser;            // 用户名，空表示 anonymous
    CString strPassword;        // 内存中为明文，存盘时加密
    BOOL    bPassive;           // TRUE=PASV（默认），FALSE=PORT
    int     nEncoding;          // FtpEncoding
    // ---- 路径 ----
    CString strLocalDir;        // 本地监控目录，如 D:\Data\Line1
    CString strRemoteDir;       // 远程目录，支持占位符 /line1/{yyyy}{MM}{dd}
    BOOL    bIncludeSubDir;     // 是否包含子目录（远程保持相同相对结构）
    CString strIncludeMask;     // 包含过滤，分号分隔，如 *.csv;*.jpg，空=全部
    CString strExcludeMask;     // 排除过滤，默认 *.tmp;*.part;~*
    // ---- 上传策略 ----
    UINT    nScanIntervalSec;   // 扫描间隔，默认 2 秒
    UINT    nStableSec;         // 稳定时间，默认 3 秒
    int     nPostAction;        // PostAction
    CString strBackupDir;       // POST_MOVE 时的备份目录（按日期建子目录）
    int     nRemoteExist;       // 远程同名且大小不同时：覆盖 / 改名加时间戳 / 跳过并搁置（大小相同一律视为已上传）
    BOOL    bUseTempName;       // 上传时使用临时名，默认 TRUE
    CString strTempSuffix;      // 默认 .uploading
    BOOL    bVerifySize;        // 上传后 SIZE 校验，默认 TRUE
    UINT    nMaxFileRetry;      // 单文件连续失败次数上限，默认 5，超过移入失败列表
    UINT    nMaxFileAgeDays;    // 只处理最近 N 天修改的文件，0=不限（默认 0）
    UINT    nKeepDays;          // “保留”模式下本地文件保留天数，到期自动删除，0=永不删除
    // ---- 超时与重连 ----
    UINT    nConnTimeoutSec;    // 连接超时，默认 10
    UINT    nIoTimeoutSec;      // 收发超时，默认 30
    UINT    nHeartbeatSec;      // 空闲心跳间隔，默认 30
    UINT    nReconnMinSec;      // 重连最小间隔，默认 3
    UINT    nReconnMaxSec;      // 重连最大间隔，默认 60
    UINT    nIdleDisconnSec;    // 空闲多久主动断开，0=保持长连接（默认 0）

    ConnConfig();               // 构造函数中填默认值
};
```

### 5.2 配置文件（INI）

位置：`程序目录\Config\FtpUploader.ini`。工业电脑程序一般装在 D 盘非系统目录，放程序目录便于整体拷贝备份；若装在 `Program Files` 下则改为 `%ProgramData%\FtpUploader\`，避免 UAC 虚拟化导致配置写不进去。

```ini
[Global]
Version=1
AutoStart=1                 ; 开机自启
StartMinimized=1            ; 启动后最小化到托盘
LogKeepDays=30              ; 日志保留天数
UiMaxLogLines=2000          ; 界面日志最大行数
ChannelCount=2
Channel0=6F1C2A0E-8B7D-4C51-9E33-0A7B1D2C3E4F
Channel1=A2B3C4D5-...

[Ch_6F1C2A0E-8B7D-4C51-9E33-0A7B1D2C3E4F]
Name=1号线视觉
Enabled=1
Host=192.168.10.20
Port=21
User=line1
Password=ENC:AQAAANCMnd8BFdER...      ; 加密后 Base64
PasswordPlain=                         ; 批量部署时可在此填明文，程序启动读取后自动加密写回 Password 并清空本项
Passive=1
Encoding=0
LocalDir=D:\Data\Line1
RemoteDir=/line1/{yyyy}{MM}{dd}
IncludeSubDir=0
IncludeMask=*.csv;*.jpg
ExcludeMask=*.tmp;*.part;~*
ScanInterval=2
StableSec=3
PostAction=1
BackupDir=D:\Backup\Line1
RemoteExist=0
UseTempName=1
TempSuffix=.uploading
VerifySize=1
MaxFileRetry=5
MaxFileAgeDays=0
KeepDays=0
ConnTimeout=10
IoTimeout=30
Heartbeat=30
ReconnMin=3
ReconnMax=60
IdleDisconn=0
```

读写要点：

1. INI 默认按 ANSI 读写，含中文时，首次创建文件要先写入 UTF-16 LE BOM（`0xFF 0xFE`），之后 `WritePrivateProfileStringW` 会按 Unicode 读写，中文名称、中文路径不会乱码。
2. 保存采用“写临时文件 → `MoveFileEx(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 替换”，防止保存过程中断电导致配置文件损坏；同时保留一份 `FtpUploader.ini.bak`，加载失败时自动用备份。
3. 每个字段读取后做范围校验（端口 1–65535、间隔 ≥ 1 等），非法值回退默认并写日志。

### 5.3 密码加密

| 方案 | 实现 | 优点 | 缺点 |
|---|---|---|---|
| A. DPAPI（推荐） | `CryptProtectData` + `CRYPTPROTECT_LOCAL_MACHINE` | 真加密，无需自己管理密钥 | 配置拷到另一台电脑解不开，需重新输入密码 |
| B. 固定密钥混淆 | 自定义密钥 XOR/RC4 + Base64 | 配置可在电脑间拷贝 | 只能防肉眼查看，反编译可破解 |

采用方案 A，并配合上面的 `PasswordPlain` 机制解决批量部署问题。解密失败时，该通道状态显示“密码无效，请重新输入”，不启动该通道，也不影响其他通道。

```cpp
// Crypto.cpp（方案 A）
CString EncryptPassword(const CString& plain) {
    DATA_BLOB in, out;
    in.pbData = (BYTE*)(LPCWSTR)plain;
    in.cbData = (plain.GetLength() + 1) * sizeof(WCHAR);
    if (!CryptProtectData(&in, L"FtpUploader", NULL, NULL, NULL,
                          CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &out))
        return CString();
    CString b64 = L"ENC:" + Base64Encode(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b64;
}
// DecryptPassword 为逆过程：去掉 "ENC:"，Base64 解码，CryptUnprotectData，结果用完后 SecureZeroMemory
```

---

## 6. FTP 协议客户端模块（CFtpClient）

### 6.1 为什么自己实现

WinINet 的超时设置（`INTERNET_OPTION_RECEIVE_TIMEOUT` 等）在 FTP 上存在长期未修复的问题，网络异常时 `FtpPutFile` 可能无限阻塞；它还依赖 IE 的全局状态，服务进程中行为不可控。自己基于 Winsock 实现只需要 FTP 的一个很小子集，所有阻塞点都能加超时、都能被停止事件打断，这是 7×24 运行的基础。

### 6.2 用到的 FTP 命令

| 命令 | 用途 | 成功应答 |
|---|---|---|
| （连接） | 读取欢迎语 | 220（120 表示稍候，继续读） |
| USER / PASS | 登录 | 331 → 230 |
| FEAT | 查询 UTF8、SIZE 支持 | 211 |
| OPTS UTF8 ON | 开启 UTF-8 文件名 | 200 |
| TYPE I | 二进制模式（必须，否则文本文件大小会变） | 200 |
| PASV / PORT | 建立数据连接 | 227 / 200 |
| CWD / MKD | 切换 / 创建目录 | 250 / 257 |
| STOR | 上传 | 150 或 125 → 226 或 250 |
| SIZE | 校验远程文件大小 | 213 |
| RNFR / RNTO | 临时名改为正式名 | 350 → 250 |
| DELE | 删除远程同名文件 | 250 |
| NOOP | 心跳 | 200 |
| QUIT | 断开 | 221 |

### 6.3 统一错误码

所有函数返回 `int` 错误码。错误分为两类，这是状态机决定“重连”还是“只标记文件失败”的依据：

```cpp
enum FtpErr {
    FE_OK          =  0,
    // ---- 会话级错误：连接已不可用，必须断开重连 ----
    FE_TIMEOUT     = -1,   // 收发/连接超时
    FE_STOPPED     = -2,   // 收到停止事件（不是错误，直接退出）
    FE_SOCKET      = -3,   // socket 错误
    FE_CLOSED      = -4,   // 对端关闭连接
    FE_PROTOCOL    = -5,   // 应答格式非法
    FE_RESOLVE     = -6,   // 域名解析失败
    FE_LOGIN       = -7,   // 登录失败（530）
    FE_SESSION     = -8,   // 421/425/426 等会话级应答
    // ---- 文件级错误：连接仍可用，只影响当前文件 ----
    FE_FILE_TEMP   = -20,  // 450/451/452/552：稍后重试
    FE_FILE_PERM   = -21,  // 550/553：权限、文件名非法等，计入重试次数
    FE_LOCAL_OPEN  = -22,  // 本地文件打不开（被占用），不计失败，放回队列
    FE_LOCAL_READ  = -23,  // 本地读失败
    FE_VERIFY      = -24,  // 大小校验不一致
};
inline bool IsSessionError(int e) { return e < 0 && e > -20 && e != FE_STOPPED; }
```

### 6.4 Socket 封装（CFtpSocket）

核心思路：socket 设为非阻塞，所有等待都用 `select` 以 200ms 为片轮询，每片检查一次停止事件。这样“超时”和“随时可停止”两件事都能保证，而且不依赖另一个线程去 `closesocket`（跨线程关闭 socket 在 Windows 上行为不可靠）。

```cpp
class CFtpSocket {
public:
    CFtpSocket() : m_s(INVALID_SOCKET), m_hStop(NULL) {}
    ~CFtpSocket() { Close(); }
    void SetStopEvent(HANDLE h) { m_hStop = h; }
    int  Connect(const sockaddr_in& addr, DWORD dwTimeoutMs);
    int  SendAll(const char* p, int len, DWORD dwTimeoutMs);
    int  RecvSome(char* p, int cap, int& got, DWORD dwTimeoutMs);
    void ShutdownSend() { if (m_s != INVALID_SOCKET) ::shutdown(m_s, SD_SEND); }
    void Close(bool bAbort = true);
    SOCKET Handle() const { return m_s; }
    void Attach(SOCKET s);                       // PORT 模式 accept 后接管
private:
    int  WaitReady(bool bWrite, DWORD dwTimeoutMs);
    SOCKET m_s;
    HANDLE m_hStop;
    int    m_nWsaErr;                            // 最近一次 WSAGetLastError()，写日志用（第 15 章）
};

int CFtpSocket::WaitReady(bool bWrite, DWORD dwTimeoutMs) {
    DWORD dwStart = ::GetTickCount();
    for (;;) {
        if (m_hStop && ::WaitForSingleObject(m_hStop, 0) == WAIT_OBJECT_0)
            return FE_STOPPED;
        fd_set fds, efds; FD_ZERO(&fds); FD_ZERO(&efds);
        FD_SET(m_s, &fds); FD_SET(m_s, &efds);
        timeval tv = { 0, 200 * 1000 };                       // 200ms 一片
        int n = ::select(0, bWrite ? NULL : &fds, bWrite ? &fds : NULL, &efds, &tv);
        if (n == SOCKET_ERROR)     return FE_SOCKET;
        if (FD_ISSET(m_s, &efds))  return FE_SOCKET;          // 非阻塞 connect 失败在 except 集合里报告
        if (n > 0)                 return FE_OK;
        if (TickElapsed(dwStart, dwTimeoutMs)) return FE_TIMEOUT;
    }
}
```

```cpp
int CFtpSocket::Connect(const sockaddr_in& addr, DWORD dwTimeoutMs) {
    Close();
    m_s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_s == INVALID_SOCKET) return FE_SOCKET;

    u_long nb = 1;  ::ioctlsocket(m_s, FIONBIO, &nb);           // 非阻塞
    BOOL on = TRUE; ::setsockopt(m_s, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));

    // TCP KeepAlive：对端断电/网线拔掉时，没有数据收发也能在约 30 秒内发现
    tcp_keepalive ka = { 1, 15000, 3000 };                      // 开启、15s 空闲后探测、3s 间隔
    DWORD dwRet = 0;
    ::WSAIoctl(m_s, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), NULL, 0, &dwRet, NULL, NULL);

    int r = ::connect(m_s, (const sockaddr*)&addr, sizeof(addr));
    if (r == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK) { Close(); return FE_SOCKET; }
    int e = WaitReady(true, dwTimeoutMs);
    if (e != FE_OK) { Close(); return e; }
    return FE_OK;
}

int CFtpSocket::SendAll(const char* p, int len, DWORD dwTimeoutMs) {
    while (len > 0) {
        int e = WaitReady(true, dwTimeoutMs);                   // 每次等待单独计时：连续 N 秒无进展才算超时
        if (e != FE_OK) return e;
        int n = ::send(m_s, p, len, 0);
        if (n == SOCKET_ERROR) {
            if (::WSAGetLastError() == WSAEWOULDBLOCK) continue;
            return FE_SOCKET;
        }
        p += n; len -= n;
    }
    return FE_OK;
}

int CFtpSocket::RecvSome(char* p, int cap, int& got, DWORD dwTimeoutMs) {
    got = 0;
    int e = WaitReady(false, dwTimeoutMs);
    if (e != FE_OK) return e;
    int n = ::recv(m_s, p, cap, 0);
    if (n == 0) return FE_CLOSED;                               // 对端正常关闭
    if (n == SOCKET_ERROR)
        return ::WSAGetLastError() == WSAEWOULDBLOCK ? FE_OK : FE_SOCKET;
    got = n;
    return FE_OK;
}

void CFtpSocket::Close(bool bAbort /* = true */) {
    if (m_s != INVALID_SOCKET) {
        if (bAbort) {                                           // 出错关闭：立即发 RST，不留 TIME_WAIT
            linger lg = { 1, 0 };
            ::setsockopt(m_s, SOL_SOCKET, SO_LINGER, (char*)&lg, sizeof(lg));
        }
        ::closesocket(m_s);
        m_s = INVALID_SOCKET;
    }
}
```

要点：

1. `tcp_keepalive` 和 `SIO_KEEPALIVE_VALS` 需要 `#include <mstcpip.h>`。
2. 超时是“无进展超时”，大文件传输只要一直有数据在走就不会超时；网络卡住 `nIoTimeoutSec` 秒无任何进展才报超时。
3. `Close(true)` 用于出错后关闭；正常上传结束关闭**数据连接**时必须用 `ShutdownSend()` → 等 226 → `Close(false)`，否则 RST 可能导致服务器丢掉最后一段数据。

### 6.5 CFtpClient 接口

```cpp
class CFtpClient {
public:
    CFtpClient();
    void SetStopEvent(HANDLE h);                  // 同时设置给控制和数据 socket
    void SetTimeouts(DWORD connMs, DWORD ioMs);
    void SetPassive(BOOL b)  { m_bPassive = b; }
    void SetEncoding(int e)  { m_nEnc = e; }

    int  Connect(const CString& host, UINT port); // 解析域名 + 连接 + 读欢迎语
    int  Login(const CString& user, const CString& pass);
    void Disconnect();                            // 尽量发 QUIT（1 秒超时），然后关闭
    bool IsConnected() const;

    int  Noop();                                  // 心跳
    int  EnsureRemoteDir(const CString& dir);     // 逐级 CWD/MKD，带缓存
    int  GetFileSize(const CString& name, __int64& size);  // SIZE：213 → 大小；550 不存在或 500/502 不支持 → size=-1 且返回 FE_OK
    int  UploadFile(const CString& local, const CString& remoteName,  // remoteName 为纯文件名
                    volatile LONG* pProgress);    // 已传字节数供 UI 显示
    int  Rename(const CString& from, const CString& to);
    int  Delete(const CString& path);

    int     LastCode() const   { return m_nLastCode; }    // 最后一次应答码
    CString LastReply() const  { return m_strLastReply; } // 最后一次应答全文，写日志用
private:
    int  SendCmd(const CString& cmd);                     // 编码转换 + 追加 \r\n
    int  ReadReply();                                     // 读完整应答（含多行）
    int  ReadLine(CStringA& line);
    int  Cmd(const CString& cmd) { int e = SendCmd(cmd); return e ? e : ReadReply(); }
    int  OpenDataConn(CFtpSocket& data);                  // PASV 或 PORT
    int  OpenPortListen(CFtpSocket& data);                // PORT：监听 + 发送 PORT 命令
    int  AcceptData(CFtpSocket& data);                    // PORT：STOR 150 后带超时 accept
    SOCKET m_listen;                                      // PORT 模式监听 socket
    int  MapReplyError(int code);                         // 应答码 → FtpErr
    CStringA Encode(const CString& s);
    CString  Decode(const CStringA& s);

    CFtpSocket m_ctrl;
    CStringA   m_rbuf;            // 控制连接接收缓冲（可能一次 recv 收到多行或半行）
    sockaddr_in m_peer;           // 服务器地址（PASV 时替换返回的内网 IP 用）
    int     m_nLastCode;
    CString m_strLastReply;
    BOOL    m_bPassive, m_bUtf8;
    int     m_nEnc;
    DWORD   m_dwConnMs, m_dwIoMs;
    HANDLE  m_hStop;
    CString m_strCwd;             // 当前远程工作目录缓存，重连时清空；相同目录不重复 CWD
};
```

### 6.6 读取应答（重点：多行应答）

FTP 应答格式：`NNN 文本` 为单行；`NNN-文本` 开头表示多行，一直读到以 `NNN ` （同一个码 + 空格）开头的行为止。中间行可能是任意内容，FEAT 的应答就是典型多行。

```cpp
int CFtpClient::ReadLine(CStringA& line) {
    for (;;) {
        int pos = m_rbuf.Find("\r\n");
        if (pos < 0) pos = m_rbuf.Find('\n');               // 兼容只发 \n 的服务器
        if (pos >= 0) {
            line = m_rbuf.Left(pos);
            int skip = (m_rbuf.GetLength() > pos && m_rbuf[pos] == '\r') ? 2 : 1;
            m_rbuf.Delete(0, pos + skip);
            return FE_OK;
        }
        if (m_rbuf.GetLength() > 64 * 1024) return FE_PROTOCOL;  // 防异常服务器撑爆内存
        char buf[4096]; int got = 0;
        int e = m_ctrl.RecvSome(buf, sizeof(buf), got, m_dwIoMs);
        if (e != FE_OK) return e;
        m_rbuf.Append(buf, got);
    }
}
```

```cpp
// 返回 FE_OK 表示读到了一个完整应答（应答码在 m_nLastCode），由调用者判断码是否符合预期
int CFtpClient::ReadReply() {
    CStringA line, all;
    int e = ReadLine(line);
    if (e != FE_OK) return e;
    if (line.GetLength() < 3 || !isdigit((BYTE)line[0]) ||
        !isdigit((BYTE)line[1]) || !isdigit((BYTE)line[2]))
        return FE_PROTOCOL;
    int code = atoi(line.Left(3));
    all = line;
    if (line.GetLength() > 3 && line[3] == '-') {          // 多行应答
        CStringA endTag; endTag.Format("%03d ", code);
        for (;;) {
            e = ReadLine(line);
            if (e != FE_OK) return e;
            all += "\n"; all += line;
            if (line.Left(4) == endTag) break;
            if (all.GetLength() > 64 * 1024) return FE_PROTOCOL;
        }
    }
    m_nLastCode    = code;
    m_strLastReply = Decode(all);
    if (code == 421) return FE_SESSION;                     // 服务器要关闭连接（超时踢人、维护）
    return FE_OK;
}

// 应答码 → 错误分类
int CFtpClient::MapReplyError(int code) {
    switch (code) {
    case 421: case 425: case 426:           return FE_SESSION;   // 会话/数据连接问题 → 重连
    case 530:                               return FE_LOGIN;
    case 450: case 451: case 452: case 552: return FE_FILE_TEMP; // 临时错误，稍后重试
    case 550: case 553: case 501: case 504: return FE_FILE_PERM; // 权限/文件名问题
    default:  return (code >= 400) ? FE_FILE_TEMP : FE_PROTOCOL;
    }
}
```

### 6.7 连接与登录

```cpp
int CFtpClient::Connect(const CString& host, UINT port) {
    m_strCwd.Empty();
    m_rbuf.Empty();
    m_bUtf8 = FALSE;

    // 每次重连都重新解析域名（服务器换 IP 后能自动适应）；getaddrinfo 本身不可中断，
    // 现场一般用 IP 直连；如用域名，请确保 DNS 可达，否则单次解析可能阻塞数秒
    addrinfoW hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    CString strPort; strPort.Format(L"%u", port);
    if (::GetAddrInfoW(host, strPort, &hints, &res) != 0 || !res) return FE_RESOLVE;
    m_peer = *(sockaddr_in*)res->ai_addr;
    ::FreeAddrInfoW(res);

    int e = m_ctrl.Connect(m_peer, m_dwConnMs);
    if (e != FE_OK) return e;
    do {                                                    // 120 = 服务稍后就绪，继续等 220
        e = ReadReply();
        if (e != FE_OK) return e;
    } while (m_nLastCode == 120);
    return (m_nLastCode == 220) ? FE_OK : FE_SESSION;     // 非 220 一律按会话错误处理，触发重连
}
```

> XP 注意：`GetAddrInfoW` 在 XP SP2+ 可用，需要 `#include <ws2tcpip.h>`。

```cpp
int CFtpClient::Login(const CString& user, const CString& pass) {
    CString u = user.IsEmpty() ? CString(L"anonymous") : user;
    int e = Cmd(L"USER " + u);
    if (e) return e;
    if (m_nLastCode == 331 || m_nLastCode == 332) {
        e = Cmd(L"PASS " + pass);          // 注意：PASS 命令内容绝不能写入日志
        if (e) return e;
    }
    if (m_nLastCode != 230 && m_nLastCode != 202) return FE_LOGIN;

    // 文件名编码协商
    if (m_nEnc == ENC_GBK) {
        m_bUtf8 = FALSE;
    } else {
        bool bUtf8 = (m_nEnc == ENC_UTF8);
        e = Cmd(L"FEAT");
        if (IsSessionError(e)) return e;
        if (e == FE_OK && m_nLastCode == 211 && m_strLastReply.Find(L"UTF8") >= 0) bUtf8 = true;
        if (bUtf8) {
            e = Cmd(L"OPTS UTF8 ON");      // IIS 等部分服务器不支持该命令但默认就是 UTF-8，失败忽略
            if (IsSessionError(e)) return e;
            m_bUtf8 = TRUE;
        }
    }
    e = Cmd(L"TYPE I");                    // 二进制模式，必须
    if (e) return e;
    return (m_nLastCode == 200) ? FE_OK : FE_SESSION;
}
```

登录失败（530）的处理：可能是密码错，也可能是服务器连接数已满。不当作永久错误，仍按重连流程处理，但重连间隔直接取最大值（60 秒），避免高频尝试触发服务器的防暴力破解封 IP（如 FileZilla Server 的 autoban、fail2ban）。界面上该通道显示红色“登录失败”。

`Encode()` / `Decode()`：`m_bUtf8 ? CP_UTF8 : 936`，用 `WideCharToMultiByte` / `MultiByteToWideChar` 转换。登录前（编码尚未协商）用户名密码按 `nEncoding == ENC_UTF8 ? UTF-8 : GBK` 编码。

### 6.8 被动模式（PASV）数据连接

```cpp
int CFtpClient::OpenDataConn(CFtpSocket& data) {
    data.SetStopEvent(m_hStop);
    if (!m_bPassive) return OpenPortListen(data);          // PORT 模式见下文
    int e = Cmd(L"PASV");
    if (e) return e;
    if (m_nLastCode != 227) return MapReplyError(m_nLastCode);
    // 标准格式：227 Entering Passive Mode (192,168,10,20,195,80)；部分服务器没有括号
    LPCWSTR p = (LPCWSTR)m_strLastReply + 4;
    while (*p && !iswdigit(*p)) ++p;
    int h1, h2, h3, h4, p1, p2;
    if (swscanf_s(p, L"%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return FE_PROTOCOL;
    sockaddr_in addr = m_peer;                              // 关键：IP 用控制连接的服务器地址
    addr.sin_port = htons((u_short)((p1 << 8) | p2));
    return data.Connect(addr, m_dwConnMs);
}
```

忽略 PASV 返回的 IP、改用控制连接对端 IP，是解决“服务器在 NAT/防火墙后返回内网地址导致数据连接超时”的通用做法（FileZilla 客户端默认也这么做）。

PORT 模式（仅当现场防火墙只放行服务器主动连客户端时使用）：用 `getsockname` 取控制连接本地 IP，绑定端口 0 建立监听，`getsockname` 取得系统分配的端口，发送 `PORT h1,h2,h3,h4,p1,p2`；在 STOR 返回 150 之后再用 `select` + 超时 `accept`，接受后的 socket 用 `data.Attach()` 接管。注意 Windows 防火墙需放行本程序，否则服务器连不进来。

### 6.9 上传单个文件（STOR）

为了兼容不支持绝对路径或中文绝对路径的服务器，统一采用“先 CWD 到目标目录，再用纯文件名操作”的方式。`remoteName` 只是文件名，目录由调用者先用 `EnsureRemoteDir` 切换好。

```cpp
int CFtpClient::UploadFile(const CString& local, const CString& remoteName, volatile LONG* pProgress) {
    // 只允许别人读，不允许别人写：如果设备程序还在写这个文件，这里会打开失败
    HANDLE hFile = ::CreateFileW(local, GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FE_LOCAL_OPEN;

    CFtpSocket data;
    int e = OpenDataConn(data);
    if (e) { ::CloseHandle(hFile); return e; }

    e = Cmd(L"STOR " + remoteName);
    if (e) { ::CloseHandle(hFile); return e; }
    if (m_nLastCode != 150 && m_nLastCode != 125) {        // 服务器拒绝（如 550 无权限）
        ::CloseHandle(hFile); return MapReplyError(m_nLastCode);
    }
    if (!m_bPassive && (e = AcceptData(data)) != FE_OK) {  // PORT 模式：150 之后才 accept
        ::CloseHandle(hFile); return e;
    }

    static const DWORD BUF = 64 * 1024;
    char* buf = new char[BUF];
    LONG sent = 0;
    for (;;) {
        DWORD rd = 0;
        if (!::ReadFile(hFile, buf, BUF, &rd, NULL)) { e = FE_LOCAL_READ; break; }
        if (rd == 0) break;                                 // 读完
        e = data.SendAll(buf, (int)rd, m_dwIoMs);
        if (e) break;
        sent += (LONG)rd;
        if (pProgress) ::InterlockedExchange(pProgress, sent);
    }
    delete[] buf;
    ::CloseHandle(hFile);

    if (e) {                       // 传输中断：控制连接上可能还挂着 426 等应答，状态已不可信
        data.Close(true);
        return IsSessionError(e) ? e : FE_SESSION;          // 一律重连，保证控制连接重新同步
    }
    data.ShutdownSend();           // 告诉服务器数据发完了（EOF）
    e = ReadReply();               // 等 226/250
    data.Close(false);
    if (e) return e;
    return (m_nLastCode == 226 || m_nLastCode == 250) ? FE_OK : MapReplyError(m_nLastCode);
}
```

说明：

1. `sent` 用 `LONG` 只够 2GB，显示进度足够；文件大小比较用 `__int64`。如有超过 2GB 的文件，进度改为按 KB 计数。
2. 本地读失败也返回会话错误并重连，因为此时数据连接已经打开，服务器那边留下了半截文件，必须断开让服务器结束这次 STOR，再由临时名机制保证半截文件不会被误用。
3. PORT 模式的 `OpenPortListen()` / `AcceptData()` 按 6.8 末尾的说明实现，`AcceptData` 成功后关闭监听 socket。

### 6.10 远程目录创建、心跳与断开

```cpp
// dir 为已展开占位符的绝对路径，如 /line1/20261020
int CFtpClient::EnsureRemoteDir(const CString& dir) {
    if (dir.CompareNoCase(m_strCwd) == 0) return FE_OK;     // 已在该目录
    int e = Cmd(L"CWD " + dir);
    if (e) return e;
    if (m_nLastCode == 250) { m_strCwd = dir; return FE_OK; }

    // 目录不存在：从根开始逐级 CWD，不存在就 MKD
    e = Cmd(L"CWD /");
    if (e) return e;
    int start = 0;
    CString part;
    while (!(part = dir.Tokenize(L"/", start)).IsEmpty()) {
        e = Cmd(L"CWD " + part);
        if (e) return e;
        if (m_nLastCode == 250) continue;
        e = Cmd(L"MKD " + part);                             // 257 成功；550 可能是并发创建已存在
        if (e) return e;
        e = Cmd(L"CWD " + part);
        if (e) return e;
        if (m_nLastCode != 250) { m_strCwd.Empty(); return FE_FILE_PERM; }  // 确实没权限
    }
    m_strCwd = dir;
    return FE_OK;
}

int CFtpClient::Noop() {
    int e = Cmd(L"NOOP");
    if (e) return e;
    return (m_nLastCode == 200) ? FE_OK : FE_SESSION;
}

void CFtpClient::Disconnect() {
    if (m_ctrl.Handle() != INVALID_SOCKET) {
        DWORD old = m_dwIoMs; m_dwIoMs = 1000;               // QUIT 最多等 1 秒，不影响停止速度
        if (SendCmd(L"QUIT") == FE_OK) ReadReply();
        m_dwIoMs = old;
    }
    m_ctrl.Close(true);
    m_rbuf.Empty();
    m_strCwd.Empty();
}
```

远程路径占位符由工作线程在上传前展开，时间取**文件的修改时间**而不是当前时间，保证 23:59:59 生成、00:00:03 才上传的文件仍进入前一天的目录：

| 占位符 | 含义 | 示例 |
|---|---|---|
| `{yyyy}` `{MM}` `{dd}` | 年 月 日 | 2026 10 20 |
| `{HH}` | 小时 | 08 |
| `{sub}` | 本地相对子目录（含子目录模式） | A\B → A/B |
| `{pc}` | 本机计算机名 | LINE1-PC |

---

## 7. 连接工作线程与断线重连（CFtpWorker）

### 7.1 通道状态

```cpp
enum ChState {
    CS_STOPPED = 0,     // 已停止（灰）
    CS_CONNECTING,      // 正在连接（黄）
    CS_IDLE,            // 已连接，空闲（绿）
    CS_UPLOADING,       // 正在上传（绿，显示进度）
    CS_WAIT_RECONN,     // 连接失败，等待重连（红，显示倒计时）
    CS_CONFIG_ERR,      // 配置错误：密码无法解密、必填项为空等（红，不重试，需人工处理）
                        // 注意：本地目录暂时不存在（U 盘、网络共享掉线）不算配置错误，扫描时持续重试
};
```

```
               Start()
  [STOPPED] ───────────▶ [CONNECTING] ──成功──▶ [IDLE] ◀──────────┐
      ▲                      │  ▲                 │  ▲            │
      │ Stop()（任意状态）     │  │                 │  │ 传完       │
      │                  失败 │  │ 倒计时到         │有文件│          │
      │                      ▼  │                 ▼  │            │
      │                  [WAIT_RECONN] ◀─会话错误─ [UPLOADING]     │
      │                      ▲                                    │
      │                      └──────────心跳失败───────────────────┘
```

### 7.2 状态快照（供 UI 读取）

```cpp
struct ChannelStatus {
    int      nState;
    CString  strStateText;      // “已连接”“第 5 次重连，32 秒后重试”等
    CString  strCurFile;        // 正在上传的文件名
    __int64  nCurTotal;         // 当前文件大小
    LONG     nCurSent;          // 当前文件已传字节
    DWORD    nTodayOk, nTodayFail;   // 今日成功/失败数（跨天自动清零）
    __int64  nTodayBytes;
    int      nPending;          // 待上传数量
    int      nFailedList;       // 超过重试次数被搁置的文件数
    CTime    tLastOk;           // 最后一次成功上传时间
    CString  strLastErr;        // 最后一次错误
    CTime    tLastErr;
    DWORD    nReconnCount;      // 本次运行累计重连次数
};
```

工作线程在状态变化时加锁更新 `m_status`；UI 定时器调用 `GetStatus()` 加锁整体拷贝一份。锁内只做拷贝，不做任何 I/O，持锁时间微秒级。

### 7.3 CFtpWorker 类

```cpp
class CFtpWorker {
public:
    explicit CFtpWorker(const ConnConfig& cfg);   // 配置按值拷贝，运行期间只读
    ~CFtpWorker();
    BOOL Start();
    BOOL Stop(DWORD dwWaitMs = 5000);             // 置停止事件并等待线程退出
    void GetStatus(ChannelStatus& out);           // 加锁拷贝
    void RetryFailedFiles();                      // UI“重试失败文件”按钮
    DWORD GetHeartbeat() const { return m_dwBeat; }  // 看门狗读取
    const ConnConfig& Config() const { return m_cfg; }
private:
    static unsigned __stdcall ThreadProc(void* p);
    void Run();                                   // 主循环
    bool StopRequested(DWORD dwWaitMs = 0);       // 等待指定毫秒，期间收到停止返回 true
    int  DoConnect();
    int  UploadOne(const PendingFile& f);         // 第 9 章
    void PostProcess(const PendingFile& f);       // 第 10 章
    void OnSessionError(int err);                 // 断开 + 计算下次重连时间
    void SetState(int st, const CString& text);
    void Beat() { ::InterlockedExchange((LONG*)&m_dwBeat, (LONG)::GetTickCount()); }

    ConnConfig     m_cfg;
    CFtpClient     m_ftp;
    CDirScanner    m_scanner;                     // 第 8 章
    CUploadHistory m_history;                     // 第 10 章
    HANDLE         m_hThread, m_hStop;
    volatile DWORD m_dwBeat;
    CCritSec       m_csStatus;
    ChannelStatus  m_status;
    std::deque<PendingFile> m_queue;              // 待上传队列，只有本线程访问，无需加锁
    std::map<CString, FailInfo> m_fails;          // 文件 → 失败次数/下次可重试时间
    std::set<CString> m_postPending;              // 已上传但删除/移动失败、待重试后处理的文件（10.2）
    volatile LONG  m_bRetryFailedReq;             // UI 请求“重试失败文件”（9.3）
    // 重连控制
    DWORD m_dwFailTick;                           // 最近一次会话错误的时刻
    UINT  m_nWaitSec;                             // 当前重连等待秒数（0=立即连）
    UINT  m_nConnFailTimes;                       // 连续失败次数（有一次成功操作才清零）
    CTime m_tOffline;                             // 本次断线开始时间，恢复时计算中断时长
    CString m_strLastLoggedErr;                   // 日志降噪：上次记录的错误文本
    DWORD   m_dwLastErrLogTick;                   // 日志降噪：上次记录错误的时刻
    DWORD m_dwLastIoTick;                         // 最后一次与服务器交互的时刻（心跳用）
};
```

### 7.4 线程入口与停止

```cpp
BOOL CFtpWorker::Start() {
    ::ResetEvent(m_hStop);
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL);
    return m_hThread != NULL;
}

unsigned __stdcall CFtpWorker::ThreadProc(void* p) {
    CFtpWorker* self = (CFtpWorker*)p;
    for (;;) {
        try {
            self->Run();                          // 正常情况下只有收到停止才返回
            break;
        } catch (CException* ex) {                // MFC 异常（CMemoryException、CFileException 等）
            TCHAR msg[256] = {0}; ex->GetErrorMessage(msg, 255); ex->Delete();
            LOG_ERR(self->m_cfg.strId, L"线程异常: %s，5 秒后恢复", msg);
        } catch (std::exception& ex) {            // std::bad_alloc 等
            LOG_ERR(self->m_cfg.strId, L"线程异常: %S，5 秒后恢复", ex.what());
        } catch (...) {
            LOG_ERR(self->m_cfg.strId, L"线程未知异常，5 秒后恢复");
        }
        self->m_ftp.Disconnect();
        if (self->StopRequested(5000)) break;     // 异常后不退出线程，休息 5 秒重新进入主循环
    }
    self->m_ftp.Disconnect();
    self->SetState(CS_STOPPED, L"已停止");
    return 0;
}

bool CFtpWorker::StopRequested(DWORD dwWaitMs) {
    return ::WaitForSingleObject(m_hStop, dwWaitMs) == WAIT_OBJECT_0;
}

BOOL CFtpWorker::Stop(DWORD dwWaitMs) {
    if (!m_hThread) return TRUE;
    ::SetEvent(m_hStop);
    if (::WaitForSingleObject(m_hThread, dwWaitMs) != WAIT_OBJECT_0) {
        LOG_ERR(m_cfg.strId, L"线程 %u ms 内未退出", dwWaitMs);
        return FALSE;                             // 调用者不得 delete 本对象（见 7.7）
    }
    ::CloseHandle(m_hThread); m_hThread = NULL;
    return TRUE;
}
```

### 7.5 主循环

主循环是单线程顺序执行的，每轮最多做一件耗时的事（扫描 / 连接 / 传一个文件 / 心跳），每轮都会更新心跳并检查停止事件。

```cpp
void CFtpWorker::Run() {
    m_ftp.SetStopEvent(m_hStop);
    m_ftp.SetTimeouts(m_cfg.nConnTimeoutSec * 1000, m_cfg.nIoTimeoutSec * 1000);
    m_ftp.SetPassive(m_cfg.bPassive);
    m_ftp.SetEncoding(m_cfg.nEncoding);
    m_nWaitSec = 0;                                         // 启动后立即连接
    DWORD dwLastScan = ::GetTickCount() - m_cfg.nScanIntervalSec * 1000;  // 启动后立即扫描

    while (!StopRequested(0)) {
        Beat();
        CheckDayRollover();                                 // 跨天：今日统计清零、日志切换由日志线程负责
        if (::InterlockedExchange(&m_bRetryFailedReq, 0)) UnparkAllFailed();  // UI 点了“重试失败文件”

        // ① 扫描本地目录（不管有没有连上都扫，界面才能看到积压数量）
        if (TickElapsed(dwLastScan, m_cfg.nScanIntervalSec * 1000)) {
            dwLastScan = ::GetTickCount();
            m_scanner.Scan(m_queue, m_history, m_fails, m_postPending);  // 稳定的新文件追加到队列尾
            UpdatePendingCount();
        }

        // ② 未连接：到点就连（空闲断开模式下只有队列有文件才连）
        if (!m_ftp.IsConnected()) {
            bool bNeed = (m_cfg.nIdleDisconnSec == 0) || !m_queue.empty();
            if (bNeed && TickElapsed(m_dwFailTick, m_nWaitSec * 1000)) {
                int e = DoConnect();
                if (e == FE_STOPPED) break;
                if (e != FE_OK) OnSessionError(e);
            } else {
                UpdateCountdownText();                      // “32 秒后重试”
                StopRequested(200);
            }
            continue;
        }

        // ③ 已连接且有文件：传一个
        if (!m_queue.empty()) {
            PendingFile f = m_queue.front();
            m_queue.pop_front();
            int e = UploadOne(f);                           // 成功/文件级失败的善后都在内部完成
            if (e == FE_STOPPED) { m_queue.push_front(f); break; }
            if (IsSessionError(e)) { m_queue.push_front(f); OnSessionError(e); }
            continue;                                       // 不休眠，连续传下一个
        }

        // ④ 空闲：主动断开 或 心跳
        if (m_cfg.nIdleDisconnSec && TickElapsed(m_dwLastIoTick, m_cfg.nIdleDisconnSec * 1000)) {
            m_ftp.Disconnect();
            SetState(CS_IDLE, L"空闲，已断开");
        } else if (TickElapsed(m_dwLastIoTick, m_cfg.nHeartbeatSec * 1000)) {
            int e = m_ftp.Noop();
            m_dwLastIoTick = ::GetTickCount();
            if (e == FE_STOPPED) break;
            if (e != FE_OK) { OnSessionError(e); continue; }
            m_nConnFailTimes = 0; m_nWaitSec = 0;           // 心跳成功说明链路确实可用，退避清零
        }
        StopRequested(200);
    }
}
```

### 7.6 连接与重连退避

```cpp
int CFtpWorker::DoConnect() {
    CString s; s.Format(L"正在连接 %s:%u", m_cfg.strHost, m_cfg.nPort);
    SetState(CS_CONNECTING, s);
    int e = m_ftp.Connect(m_cfg.strHost, m_cfg.nPort);
    if (e == FE_OK) e = m_ftp.Login(m_cfg.strUser, m_cfg.strPassword);
    if (e != FE_OK) return e;

    m_dwLastIoTick = ::GetTickCount();
    if (m_nConnFailTimes > 0) {                         // 断线后恢复，记录中断时长，便于追溯网络故障
        CTimeSpan ts = CTime::GetCurrentTime() - m_tOffline;
        LOG_INFO(m_cfg.strId, L"连接已恢复，中断 %s，期间重试 %u 次",
                 ts.Format(L"%H:%M:%S"), m_nConnFailTimes);
    } else {
        LOG_INFO(m_cfg.strId, L"已连接 %s:%u", m_cfg.strHost, m_cfg.nPort);
    }
    SetState(CS_IDLE, L"已连接");
    return FE_OK;
}

void CFtpWorker::OnSessionError(int err) {
    m_ftp.Disconnect();
    if (m_nConnFailTimes == 0) m_tOffline = CTime::GetCurrentTime();
    ++m_nConnFailTimes;
    { CAutoLock lk(m_csStatus); ++m_status.nReconnCount; }

    if (err == FE_LOGIN)       m_nWaitSec = m_cfg.nReconnMaxSec;            // 登录失败直接最大间隔
    else if (m_nWaitSec == 0)  m_nWaitSec = m_cfg.nReconnMinSec;
    else                       m_nWaitSec = min(m_nWaitSec * 2, m_cfg.nReconnMaxSec);
    m_dwFailTick = ::GetTickCount();

    // 日志降噪：前 3 次都记；之后同一错误每 10 分钟记一次；错误类型变化立即记
    CString msg = ErrText(err, m_ftp.LastReply());
    if (m_nConnFailTimes <= 3 || msg != m_strLastLoggedErr ||
        TickElapsed(m_dwLastErrLogTick, 10 * 60 * 1000)) {
        LOG_ERR(m_cfg.strId, L"%s（第 %u 次，%u 秒后重连）", msg, m_nConnFailTimes, m_nWaitSec);
        m_strLastLoggedErr = msg;
        m_dwLastErrLogTick = ::GetTickCount();
    }
    SetLastErrText(msg);                                // 注意不要命名为 SetLastError，会与 Win32 API 混淆
    SetState(CS_WAIT_RECONN, msg);
}
```

退避序列（默认 min=3、max=60）：3 → 6 → 12 → 24 → 48 → 60 → 60 → …，**无限重试，永不放弃**。只有“一次成功的上传或心跳”才把退避清零，而不是“连接成功”就清零。原因：常见故障是控制连接能连上、数据连接被防火墙拦截，如果连上就清零，会变成每 3 秒一次“连接 → 上传失败 → 断开”的死循环，刷爆日志也给服务器造成压力。

各类故障的恢复路径：

| 故障 | 发现方式 | 发现时间 | 恢复 |
|---|---|---|---|
| 服务器 FTP 服务重启 | 心跳/命令收到 421 或连接被关闭 | 立即 ~ 30 秒 | 自动重连 |
| 网线拔掉 / 交换机断电 | TCP KeepAlive 或心跳超时 | ≤ 30 秒（空闲）/ ≤ IoTimeout（传输中） | 网络恢复后下一次重连 |
| 服务器断电 | 同上 | 同上 | 同上 |
| 服务器空闲超时踢人 | 下次心跳/命令失败 | ≤ 30 秒 | 立即重连（退避从最小值开始） |
| 传输过程中断开 | SendAll 超时/错误 | ≤ IoTimeout | 重连后该文件重新上传（临时名机制保证不留残文件） |
| 密码被修改 | 530 | 立即 | 每 60 秒重试，界面红色提示，修改配置后恢复 |
| 服务器磁盘满 | 452 / 552 | 立即 | 文件级临时错误，延迟重试，界面告警 |
| 本机 IP 变化 / 网卡重启 | socket 错误 | 立即 | 自动重连 |

### 7.7 通道管理器（CChannelManager）

```cpp
class CChannelManager {
public:
    void LoadAndStartAll();                         // 程序启动：读配置，启动所有 bEnabled 通道
    BOOL StartChannel(const CString& id);
    BOOL StopChannel(const CString& id);
    BOOL ApplyConfig(const ConnConfig& cfg);        // 新增或修改：停止旧 worker → 用新配置创建 → 启动
    BOOL RemoveChannel(const CString& id);
    void StopAll(DWORD dwTotalWaitMs = 8000);       // 程序退出：先对所有通道 SetEvent，再统一等待
    void GetAllStatus(std::vector<std::pair<ConnConfig, ChannelStatus> >& out);
    void CheckWatchdog();                           // UI 定时器每 5 秒调用一次，见 14.3
private:
    CCritSec m_cs;                                  // 保护 m_workers（只有 UI 线程修改，但看门狗/状态读取也会访问）
    std::vector<CFtpWorker*> m_workers;
    std::vector<CFtpWorker*> m_zombies;             // 停止超时、尚未退出的 worker
};
```

要点：

1. 程序退出时 `StopAll` 先给**所有**通道置停止事件，再统一 `WaitForMultipleObjects`（一次最多 64 个句柄，通道数上限 32 足够），这样 N 个通道的退出时间是 max 而不是累加。
2. `Stop()` 超时返回 FALSE 时，worker 对象移入 `m_zombies`，**不 delete、不 TerminateThread**（TerminateThread 可能在线程持有 CRT 堆锁时杀掉它，导致整个进程之后任何 `new` 都死锁）。同一通道在旧线程真正退出前不允许重新启动，界面显示“停止中…”，防止两个线程同时上传同一目录造成重复上传。UI 定时器每次检查 zombie 线程句柄，退出了再 delete。
3. 程序退出时如果仍有 zombie，记录日志后直接 `ExitProcess`，由操作系统回收。
4. 修改配置只影响该通道，其他通道不停。修改的是“服务器地址/路径”等关键项时，弹框提示“修改将重启该通道，当前正在上传的文件会中断并重传”。

---

## 8. 本地目录监控与文件稳定性判断

### 8.1 轮询还是 ReadDirectoryChangesW

| 方式 | 优点 | 缺点 |
|---|---|---|
| 定时轮询 FindFirstFile（采用） | 简单可靠；网络共享目录也能用；程序重启后自动补传积压文件 | 有 1 个扫描周期的延迟；文件数极多时有 CPU 开销 |
| ReadDirectoryChangesW | 实时 | 缓冲区溢出会丢事件；网络共享上不可靠；文件开始写就触发，仍需稳定性判断；重启后漏掉停机期间的文件 |

采用轮询为主。如需更低延迟，可增加 `ReadDirectoryChangesW` 仅作为“立即触发一次扫描”的信号，扫描本身仍以轮询结果为准。

### 8.2 数据结构

```cpp
struct PendingFile {
    CString  strFullPath;   // D:\Data\Line1\A\20261020_0001.csv
    CString  strRelDir;     // A（相对监控根目录，根目录下为空）
    CString  strName;       // 20261020_0001.csv
    __int64  nSize;
    FILETIME ftWrite;       // 修改时间，用于远程目录日期占位符和排序
};

struct FailInfo {
    UINT  nTimes;           // 连续失败次数
    DWORD dwLastTick;       // 最近失败时刻
    bool  bParked;          // 超过 nMaxFileRetry 被搁置，等待人工“重试失败文件”
};

class CDirScanner {
public:
    void Init(const ConnConfig& cfg);
    void Scan(std::deque<PendingFile>& queue, CUploadHistory& hist,
              std::map<CString, FailInfo>& fails, const std::set<CString>& postPending);
private:
    struct Candidate { __int64 size; FILETIME ft; DWORD dwStableSince; bool bSeen; };
    std::map<CString, Candidate> m_cand;   // 正在观察稳定性的文件
    std::set<CString> m_inQueue;           // 已在队列中的文件，避免重复入队（出队时同步移除）
};
```

### 8.3 扫描与稳定性算法

每次扫描：

1. 检查本地根目录是否存在。不存在时状态栏显示“本地目录不可访问”，日志每 10 分钟最多记一次，本轮结束，下轮继续检查。
2. `FindFirstFileW(dir\*)` 枚举。跳过 `.` `..`、隐藏/系统文件、`FILE_ATTRIBUTE_REPARSE_POINT`（符号链接、目录联接，防止递归死循环）。子目录模式下递归，深度上限 10。
3. 文件名过滤：先匹配排除列表（`*.tmp;*.part;~*` 以及本通道的临时后缀），再匹配包含列表，使用 `PathMatchSpecW`。
4. 跳过：已在队列中；“保留”模式下已在上传记录中（大小和修改时间都一致）；失败表中已搁置；失败表中距上次失败未满 30 秒；在 `postPending` 中（已传完、只差删除/移动，由工作线程单独重试后处理）。
5. 稳定性判断：
   - 新文件：加入 `m_cand`，记录大小、修改时间、当前 tick。
   - 已在观察中，大小或修改时间变了：更新并重新计时。
   - 已在观察中，未变化且已满 `nStableSec`：尝试 `CreateFileW(GENERIC_READ, FILE_SHARE_READ)`。打开成功说明没有其他进程以写方式持有它，立即关闭句柄，入队；打开失败（`ERROR_SHARING_VIOLATION`）继续观察。
6. 本轮未出现的候选文件（已被删除或改名）从 `m_cand` 中清除，防止 map 无限增长。
7. 本轮新入队的文件按修改时间升序排序后追加到队列尾，保证先产生的先上传。

> “能独占读打开”这一步专门对付“边写边落盘、写一会停一会”的设备程序。只有稳定时间判断不够：设备程序可能停顿超过 3 秒后继续写。两个条件都满足才入队。

### 8.4 特殊情况

| 情况 | 处理 |
|---|---|
| 备份目录在监控目录内部 | 保存配置时校验并拒绝（否则移过去的文件会被再次上传，无限循环） |
| 单目录文件数巨大（“保留”模式长期累积） | 增加配置 `MaxFileAgeDays`（默认 0=不限），只处理最近 N 天修改的文件；并建议现场配合“保留 N 天后删除” |
| 设备写完后会生成标志文件（如 `xxx.csv.ok`） | 可选“标志文件模式”：只有标志文件存在时才上传数据文件，传完后一起处理标志文件 |
| 0 字节文件 | 默认照常上传（部分设备用空文件做触发标记）；可配置跳过 |
| 网络共享目录（`\\server\share`） | 扫描可能阻塞数秒，属正常；看门狗超时阈值要大于此值（见 14.3） |
| 文件在入队后、上传前又被修改 | 上传前重新取一次大小和修改时间，与入队时不一致则放回稳定性观察 |
| 设备在上传过程中删除文件 | 打开失败或读失败，按 `FE_LOCAL_OPEN` 处理，不计失败次数，下轮扫描自然消失 |

---

## 9. 上传流程

### 9.1 流程图

```
取出队列头 PendingFile
   │
   ├─ 重新 stat 本地文件 ── 不存在 ─────────────▶ 丢弃（记日志 WARN）
   │                    └─ 大小/时间变了 ───────▶ 放回稳定性观察
   ▼
展开远程目录模板（文件修改时间 + 相对子目录）
   ▼
EnsureRemoteDir ── 失败 ─▶ 会话错误→重连 / 权限错误→计入文件失败
   ▼
SIZE 正式名（检查远程是否已存在）
   ├─ 已存在且大小相同 ─▶ 视为已上传（断线重传场景），直接做上传后处理
   ├─ 已存在且大小不同 ─▶ 按策略：覆盖 / 改名为 name_时间戳.ext / 跳过并搁置（等人工确认）
   ▼
STOR 临时名（name.ext.uploading）
   ▼
SIZE 临时名 == 本地大小？ ── 否 ─▶ DELE 临时名，FE_VERIFY，计入文件失败
   ▼
RNFR 临时名 / RNTO 正式名 ── 失败(550/553) ─▶ DELE 正式名后重试一次 RNTO
   ▼
上传后处理（删除 / 移动 / 记录）
   ▼
写上传记录 CSV，更新统计，退避清零
```

### 9.2 UploadOne 实现

```cpp
int CFtpWorker::UploadOne(const PendingFile& f) {
    // 注意：m_inQueue 中的标记在本函数“最终结束”（成功/放弃/放回观察）时才清除；
    // 会话错误时主循环把 f 放回队列头，标记保持，扫描器不会重复入队
    PendingFile cur;
    if (!StatFile(f.strFullPath, cur)) {                    // 文件已不存在
        m_scanner.OnDequeued(f.strFullPath);
        LOG_WARN(m_cfg.strId, L"文件已消失，跳过: %s", f.strFullPath);
        return FE_OK;
    }
    if (cur.nSize != f.nSize || CompareFileTime(&cur.ftWrite, &f.ftWrite) != 0) {
        m_scanner.OnDequeued(f.strFullPath);                // 又被修改了，下轮扫描重新观察稳定性
        return FE_OK;
    }

    CString remoteDir = ExpandRemoteDir(m_cfg.strRemoteDir, f);  // /line1/20261020[/子目录]
    SetCurrentFile(f);                                       // 状态：上传中 + 文件名 + 大小
    DWORD dwStart = ::GetTickCount();

    int e = m_ftp.EnsureRemoteDir(remoteDir);
    if (e) return OnFileError(f, e, L"进入远程目录失败");

    // 远程是否已有同名文件
    CString finalName = f.strName;
    __int64 remoteSize = -1;
    e = m_ftp.GetFileSize(f.strName, remoteSize);            // 不存在时 remoteSize = -1，返回 FE_OK
    if (IsSessionError(e)) return e;
    if (remoteSize == f.nSize) {                             // 典型场景：上次已传完但后处理前断线
        LOG_INFO(m_cfg.strId, L"远程已存在且大小一致，视为已上传: %s", f.strName);
        m_scanner.OnDequeued(f.strFullPath);
        WriteRecord(f, remoteDir, finalName, dwStart, L"SKIP_EXIST", L"");
        PostProcess(f);
        return FE_OK;
    }
    if (remoteSize >= 0 && m_cfg.nRemoteExist == EXIST_SKIP) {  // 同名但大小不同：不传，搁置等人工确认
        m_scanner.OnDequeued(f.strFullPath);
        FailInfo& fi = m_fails[f.strFullPath];
        fi.nTimes = m_cfg.nMaxFileRetry; fi.dwLastTick = ::GetTickCount(); fi.bParked = true;
        AddTodayFail();
        LOG_WARN(m_cfg.strId, L"远程已有同名文件（大小不同），按策略跳过并搁置: %s", f.strName);
        WriteRecord(f, remoteDir, f.strName, dwStart, L"FAIL", L"远程同名文件大小不同");
        return FE_OK;
    }
    if (remoteSize >= 0 && m_cfg.nRemoteExist == EXIST_RENAME)
        finalName = MakeTimestampName(f.strName);            // abc.csv → abc_20261020_083015.csv

    CString upName = m_cfg.bUseTempName ? finalName + m_cfg.strTempSuffix : finalName;
    e = m_ftp.UploadFile(f.strFullPath, upName, &m_status.nCurSent);
    if (e) return OnFileError(f, e, L"上传失败");

    if (m_cfg.bVerifySize) {
        __int64 sz = -1;
        e = m_ftp.GetFileSize(upName, sz);
        if (IsSessionError(e)) return e;
        if (sz >= 0 && sz != f.nSize) {                      // sz<0 表示服务器不支持 SIZE，跳过校验
            m_ftp.Delete(upName);
            return OnFileError(f, FE_VERIFY, L"大小校验不一致");
        }
    }
    if (upName != finalName) {
        e = m_ftp.Rename(upName, finalName);
        if (e && !IsSessionError(e) && remoteSize >= 0) {    // 部分服务器 RNTO 不允许覆盖已存在文件
            m_ftp.Delete(finalName);
            e = m_ftp.Rename(upName, finalName);
        }
        if (e) return OnFileError(f, e, L"改名失败");
    }

    m_dwLastIoTick = ::GetTickCount();
    m_nConnFailTimes = 0; m_nWaitSec = 0;                    // 成功上传，退避清零
    m_fails.erase(f.strFullPath);
    m_scanner.OnDequeued(f.strFullPath);
    WriteRecord(f, remoteDir, finalName, dwStart, L"OK", L"");
    AddTodayOk(f.nSize);
    PostProcess(f);
    return FE_OK;
}
```

### 9.3 文件级错误处理

```cpp
// 返回值交给主循环：会话错误原样返回（主循环负责放回队列 + 重连），文件级错误返回 FE_OK
int CFtpWorker::OnFileError(const PendingFile& f, int e, LPCWSTR stage) {
    CString msg; msg.Format(L"%s: %s", stage, ErrText(e, m_ftp.LastReply()));
    if (IsSessionError(e) || e == FE_STOPPED) {
        WriteRecord(f, L"", L"", 0, L"RETRY", msg);          // 记录本次中断，重连后会重传
        return e;
    }
    m_scanner.OnDequeued(f.strFullPath);
    if (e == FE_LOCAL_OPEN) {                                 // 被占用，不计失败，下轮扫描再来
        LOG_DEBUG(m_cfg.strId, L"文件被占用，稍后重试: %s", f.strName);
        return FE_OK;
    }
    FailInfo& fi = m_fails[f.strFullPath];
    ++fi.nTimes;
    fi.dwLastTick = ::GetTickCount();                         // 扫描器据此延迟 30 秒再入队
    if (fi.nTimes >= m_cfg.nMaxFileRetry) {
        fi.bParked = true;                                    // 搁置，需人工“重试失败文件”
        AddTodayFail();
        LOG_ERR(m_cfg.strId, L"%s 连续失败 %u 次，已搁置: %s", f.strName, fi.nTimes, msg);
        WriteRecord(f, L"", L"", 0, L"FAIL", msg);
    } else {
        LOG_WARN(m_cfg.strId, L"%s 第 %u 次失败: %s", f.strName, fi.nTimes, msg);
        WriteRecord(f, L"", L"", 0, L"RETRY", msg);
    }
    return FE_OK;
}
```

“重试失败文件”按钮：UI 线程只设置 `m_bRetryFailedReq = TRUE`（`volatile LONG`，`InterlockedExchange`），工作线程在主循环开头看到标志后清空 `m_fails` 中所有 `bParked` 项。`m_fails` 始终只被工作线程访问，不需要加锁。另外 `m_fails` 中对应文件已不存在的项在每次扫描时清理，防止长期运行后 map 膨胀。

### 9.4 关键设计说明

1. **临时名 + 改名**：服务器上的读取方（MES 等）只会看到完整文件。下游程序需配合忽略 `*.uploading`，交付时务必告知对方。
2. **“远程已存在且大小一致”视为已上传**：解决“文件已传完、改名完成，但删除本地文件前断线/断电”导致的重复上传。代价是同名同大小但内容不同的文件会被跳过，工业现场文件名一般带时间戳和序号，可接受；不可接受时把 `RemoteExist` 设为“改名”并关闭此判断。
3. **一个通道串行上传**：同一通道内文件按顺序一个一个传，保证顺序且实现简单。需要更高吞吐时，可对同一目录配置多个通道并用不同的文件过滤分流，不建议在单通道内做并发。
4. **二进制模式**：登录后固定 `TYPE I`，任何文件都不做换行转换。

---

## 10. 上传后处理与防重复上传

### 10.1 三种模式

| 模式 | 动作 | 防重复依据 | 适用 |
|---|---|---|---|
| 删除 | `DeleteFileW` | 文件已不在目录中 | 本地磁盘小、服务器是唯一存档 |
| 移到备份目录 | 移到 `BackupDir\yyyyMMdd\相对子目录\` | 文件已不在目录中 | 最常用，本地留底可追溯 |
| 保留 | 不动文件，写入上传记录 | 上传记录（路径 + 大小 + 修改时间） | 设备程序要求文件不能动；或其他程序也在读这个目录 |

### 10.2 删除 / 移动失败的处理

文件刚传完，可能被杀毒软件、索引服务或设备程序短暂占用，导致删除或移动失败。处理方式：

1. 立即重试 3 次，间隔 500ms（间隔中检查停止事件）。
2. 仍失败：把文件路径 + 大小 + 修改时间加入内存中的 `m_postPending` 集合，记 WARN 日志。扫描器遇到该集合中的文件不再入队，而是只重试后处理动作，每 30 秒一次。
3. 即使程序此时重启、内存集合丢失，下次扫描会重新上传该文件，到第 9 章的“远程已存在且大小一致”分支被识别为已上传，直接做后处理，不会真的重复传。

移动的实现：

```cpp
BOOL MoveToBackup(const PendingFile& f, const CString& backupRoot) {
    CString dstDir = backupRoot + L"\\" + FileTimeToDateStr(f.ftWrite);  // D:\Backup\Line1\20261020
    if (!f.strRelDir.IsEmpty()) dstDir += L"\\" + f.strRelDir;
    ::SHCreateDirectoryExW(NULL, dstDir, NULL);          // 逐级创建，已存在不报错
    CString dst = dstDir + L"\\" + f.strName;
    if (::PathFileExistsW(dst)) dst = dstDir + L"\\" + MakeTimestampName(f.strName);  // 不覆盖备份
    // 同盘为改名（瞬间完成）；跨盘时自动复制+删除；WRITE_THROUGH 保证断电不丢
    return ::MoveFileExW(f.strFullPath, dst, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
}
```

> 备份目录也会无限增长。全局设置提供“备份保留天数”（默认 30 天），由每个通道的工作线程每天凌晨（跨天时）删除过期的日期目录。删除过程分批进行，每删 100 个文件检查一次停止事件并更新心跳，避免一次删几十万个文件导致看门狗误判卡死。

### 10.3 上传记录（“保留”模式专用）

文件：`程序目录\Data\History_<通道ID>.txt`，每行一条，UTF-8：

```
<相对路径>|<大小>|<修改时间 FILETIME 十六进制>|<上传时间>
A\20261020_0001.csv|20480|01DB42C8A1F3B200|2026-10-20 08:30:15
```

```cpp
class CUploadHistory {
public:
    void Load(const CString& file, UINT nKeepDays);  // 启动时加载，只保留最近 N 天（默认 = KeepDays 或 30）
    bool Contains(const CString& rel, __int64 size, const FILETIME& ft) const;
    void Add(const CString& rel, __int64 size, const FILETIME& ft);   // 追加写一行并 FlushFileBuffers
    void Compact();                                  // 每天一次：重写文件，去掉过期记录
private:
    std::map<CString, std::pair<__int64, ULONGLONG> > m_map;  // 只有工作线程访问
    HANDLE m_hFile;
};
```

要点：

1. 判断“已上传”要求路径、大小、修改时间三者都一致；设备覆盖写同名文件后，修改时间变化，会重新上传。
2. 追加写后立即 `FlushFileBuffers`，断电最多丢最后一条，丢了也只是重传一次，再被“远程已存在”分支拦下。
3. 加载时遇到格式错误的行（断电写了半行）直接跳过。
4. `Compact()` 采用“写新文件 → `MoveFileEx` 替换”，与配置文件相同的原子替换方式。
5. `KeepDays > 0` 时，工作线程每小时删除一次修改时间早于 N 天且已在记录中的本地文件，同时删除对应记录。

---

## 11. 日志模块

### 11.1 日志文件组织

```
Log\
├─ 2026-10-20\
│  ├─ System.log                 程序启动/退出、配置修改、看门狗、资源监控
│  ├─ 1号线视觉_6F1C2A0E.log      通道运行日志（连接、重连、上传、错误）
│  ├─ 1号线视觉_6F1C2A0E.csv      通道上传记录（一个文件一行，可用 Excel 打开）
│  └─ 2号线称重_A2B3C4D5.log
└─ 2026-10-21\
Dump\                             崩溃转储
```

文件名带通道名便于现场查看，带 ID 前 8 位保证通道改名后仍能对应。按天分目录，单文件超过 20MB 时滚动为 `xxx.1.log`、`xxx.2.log`，防止异常刷日志时单个文件过大打不开。

### 11.2 格式

运行日志（UTF-8 带 BOM，记事本可直接打开）：

```
2026-10-20 08:30:15.123 [INFO ] [T1a2c] 已连接 192.168.10.20:21
2026-10-20 08:30:15.456 [INFO ] [T1a2c] 上传成功 20261020_0001.csv -> /line1/20261020/ 20.0KB 0.12s 166.7KB/s
2026-10-20 09:12:01.002 [ERROR] [T1a2c] 连接超时（第 1 次，3 秒后重连）
2026-10-20 09:13:20.877 [INFO ] [T1a2c] 连接已恢复，中断 00:01:19，期间重试 5 次
```

上传记录 CSV（UTF-8 带 BOM，Excel 打开中文不乱码）：

```
时间,通道,本地文件,远程目录,远程文件名,大小(字节),耗时(ms),速度(KB/s),结果,说明
2026-10-20 08:30:15,1号线视觉,D:\Data\Line1\20261020_0001.csv,/line1/20261020,20261020_0001.csv,20480,120,166.7,OK,
2026-10-20 09:12:01,1号线视觉,D:\Data\Line1\20261020_0002.csv,,,,,,RETRY,上传失败: 连接超时
```

结果取值：`OK` 成功、`SKIP_EXIST` 远程已存在视为成功、`RETRY` 本次失败将重试、`FAIL` 超过重试次数已搁置。字段中含逗号或引号时按 CSV 规则用双引号包裹并转义。

日志级别：`DEBUG`（默认关闭，排查问题时在全局设置中打开，会记录每条 FTP 命令和应答）、`INFO`、`WARN`、`ERROR`。**任何级别都不记录 PASS 命令内容**，DEBUG 下记为 `PASS ******`。

### 11.3 日志架构

```
工作线程 ─LOG_INFO()─▶ 格式化（在调用线程完成）─▶ 加锁 push ─┬─▶ 文件队列 m_fileQ（上限 50000 条）
                                                            └─▶ UI 队列   m_uiQ  （上限 5000 条）
日志线程：每 200ms 或队列超过 500 条时，整批取出 → 按目标文件分组 → WriteFile
UI 线程： 定时器每 200ms 取出 UI 队列全部条目 → 追加到日志列表
```

设计原则：

1. **写日志的线程永远不碰磁盘**，只做一次加锁 push（微秒级）。磁盘慢、U 盘拔掉、磁盘满，都不会拖慢上传。
2. **两个队列都有上限**。超过上限丢弃最旧的条目并计数，恢复后补写一条“丢弃了 N 条日志”。这样即使日志线程卡死，内存也不会无限增长。
3. 文件句柄按“日期 + 通道”缓存，跨天时关闭旧句柄、打开新目录；连续 5 分钟没写的句柄自动关闭。
4. 磁盘写失败（满、目录被删）时，日志线程每 60 秒重试一次打开，同时通过状态标志让界面状态栏显示“日志写入失败”。

```cpp
struct LogItem {
    SYSTEMTIME st;
    int        level;
    CString    chId;      // 空 = 系统日志
    CString    text;
    bool       bCsv;      // true = 上传记录，写入 .csv
};

class CLogger {
public:
    static CLogger& Inst();
    void Start(const CString& logRoot, UINT keepDays);
    void Stop();                                       // 退出前把队列写完
    void Write(int level, const CString& chId, LPCWSTR fmt, ...);
    void WriteCsv(const CString& chId, const CString& line);
    void PopUi(std::vector<LogItem>& out);             // UI 线程调用，取走全部
    void SetChannelName(const CString& chId, const CString& name);  // 文件名用
    void SetDebug(bool b) { ::InterlockedExchange(&m_bDebug, b ? 1 : 0); }
    bool IsDebug() const  { return m_bDebug != 0; }
private:
    static unsigned __stdcall ThreadProc(void*);
    void Flush(std::vector<LogItem>& batch);
    void CleanOld();                                   // 每天一次，删除 keepDays 之前的日期目录
    CCritSec m_cs;
    std::deque<LogItem> m_fileQ, m_uiQ;
    DWORD    m_dwDropped;
    volatile LONG m_bDebug;
    HANDLE   m_hThread, m_hStop, m_hWake;
};

#define LOG_DEBUG(ch, fmt, ...) do { if (CLogger::Inst().IsDebug()) \
        CLogger::Inst().Write(LV_DEBUG, ch, fmt, __VA_ARGS__); } while (0)
#define LOG_INFO(ch, fmt, ...)  CLogger::Inst().Write(LV_INFO,  ch, fmt, __VA_ARGS__)
#define LOG_WARN(ch, fmt, ...)  CLogger::Inst().Write(LV_WARN,  ch, fmt, __VA_ARGS__)
#define LOG_ERR(ch, fmt, ...)   CLogger::Inst().Write(LV_ERROR, ch, fmt, __VA_ARGS__)
```

注意：`Write` 中格式化用 `CString::FormatV`。MFC 的 `CString` 对象只含一个字符指针，可以直接作为 `%s` 的可变参数传入（本文档示例即如此）；但 `std::wstring` 必须传 `.c_str()`，否则运行时崩溃，代码审查重点检查。日志线程写盘时用 `WriteFile`，每批写完不强制 `FlushFileBuffers`（性能），而是每 5 秒或遇到 ERROR 级别时 Flush 一次，断电最多丢 5 秒日志。

---

## 12. 界面设计

### 12.1 主界面布局（默认 1024×700，可缩放，最小 800×500）

```
┌─ FTP 自动上传工具 V1.0 ─────────────────────────────────────────────── _ □ × ┐
│ [新增] [编辑] [复制] [删除] │ [启动] [停止] [全部启动] [全部停止] │ [重试失败] [设置] │
├──────────────────────────────────────────────────────────────────────────────┤
│ 启用│名称      │服务器              │状态               │待传│今日成功│今日失败│最后上传 │最后错误       │
│ ☑  │1号线视觉 │192.168.10.20:21    │● 上传中 45% a.jpg │ 12 │ 3521   │ 0      │08:30:15│              │
│ ☑  │2号线称重 │ftp.factory.local:21│● 已连接           │  0 │ 862    │ 2      │08:29:58│09:12 连接超时 │
│ ☐  │3号线备用 │192.168.10.22:2121  │○ 已停止           │  - │ -      │ -      │        │              │
│ ☑  │4号线视觉 │192.168.10.23:21    │● 第3次重连 18s后  │ 57 │ 1204   │ 0      │07:55:02│连接被拒绝    │
├──────────────────────────────────────────────────────────────────────────────┤
│ 日志  通道:[全部      ▼] 级别:[INFO及以上▼] [☑自动滚动] [清屏] [打开日志目录]   │
│ 08:30:15.456 1号线视觉  INFO  上传成功 20261020_0001.csv 20.0KB 0.12s          │
│ 09:12:01.002 2号线称重  ERROR 连接超时（第 1 次，3 秒后重连）     ← 红色         │
│ 09:12:30.118 4号线视觉  WARN  文件被占用，稍后重试                ← 橙色         │
├──────────────────────────────────────────────────────────────────────────────┤
│ 运行 12天03:15:22 │ 通道 3/4 在线 │ 今日上传 5587 个 1.2GB │ 内存 18MB 句柄 412 │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 12.2 控件清单

| 控件 ID | 类型 | 说明 |
|---|---|---|
| IDC_BTN_ADD / EDIT / COPY / DEL | Button | 通道管理；删除需确认，且必须先停止 |
| IDC_BTN_START / STOP | Button | 对选中通道（支持多选） |
| IDC_BTN_START_ALL / STOP_ALL | Button | 全部 |
| IDC_BTN_RETRY | Button | 选中通道的搁置文件重新排队 |
| IDC_BTN_SETTINGS | Button | 全局设置 |
| IDC_LIST_CHANNEL | List Control（Report，整行选择，带复选框） | 通道列表；双击 = 编辑；勾选框 = 启用/停用 |
| IDC_CMB_LOG_CH | Combo Box（下拉列表） | 日志按通道过滤，第一项“全部”“系统” |
| IDC_CMB_LOG_LEVEL | Combo Box | 日志级别过滤 |
| IDC_CHK_AUTOSCROLL | Check Box | 自动滚动到底 |
| IDC_BTN_CLEAR_LOG / OPEN_LOG_DIR | Button | 清屏只清界面，不删文件 |
| IDC_LIST_LOG | List Control（Report，**Owner Data 虚拟列表**） | 日志显示 |
| IDC_STATUSBAR | CStatusBar 或底部 Static | 运行时长、在线数、统计、资源 |

### 12.3 通道列表

1. 由 UI 定时器（`TIMER_STATUS`，500ms）调用 `CChannelManager::GetAllStatus()` 刷新，**只更新变化了的单元格**（对比上次的快照），避免整表重绘闪烁。
2. 列表开启 `LVS_EX_DOUBLEBUFFER`（XP 及以上支持）进一步消除闪烁。
3. 通过 `NM_CUSTOMDRAW` 给“状态”列着色：绿 = 已连接/上传中，黄 = 连接中，红 = 等待重连/配置错误/登录失败，灰 = 已停止。
4. 复选框变化通过 `LVN_ITEMCHANGED` 捕获（判断 `uNewState & LVIS_STATEIMAGEMASK`），注意刷新时用代码设置勾选也会触发该消息，需用 `m_bUpdating` 标志屏蔽。
5. 右键菜单：启动、停止、编辑、打开本地目录、打开今日日志、打开今日上传记录、重试失败文件。

### 12.4 日志列表（虚拟列表）

普通 `InsertItem` 方式在长期运行中会越来越慢，且删除首行会导致整表重排。采用 `LVS_OWNERDATA` 虚拟列表 + 环形缓冲：

```cpp
// FtpUploaderDlg.h
std::deque<LogItem> m_uiLogs;         // 全部日志（上限 UiMaxLogLines，默认 2000）
std::vector<int>    m_viewIdx;        // 过滤后的下标（通道/级别过滤）

void CFtpUploaderDlg::OnTimer(UINT_PTR id) {
    if (id == TIMER_LOG) {
        std::vector<LogItem> items;
        CLogger::Inst().PopUi(items);
        if (items.empty()) return;
        for (size_t i = 0; i < items.size(); ++i) m_uiLogs.push_back(items[i]);
        while (m_uiLogs.size() > m_nMaxLines) m_uiLogs.pop_front();
        RebuildView();                                     // 重算 m_viewIdx
        m_listLog.SetItemCountEx((int)m_viewIdx.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
        m_listLog.Invalidate(FALSE);
        if (m_bAutoScroll) m_listLog.EnsureVisible((int)m_viewIdx.size() - 1, FALSE);
    }
    CDialogEx::OnTimer(id);
}

void CFtpUploaderDlg::OnGetDispInfoLog(NMHDR* pNMHDR, LRESULT* pResult) {
    LVITEM& it = ((NMLVDISPINFO*)pNMHDR)->item;
    if ((it.mask & LVIF_TEXT) && it.iItem < (int)m_viewIdx.size()) {
        const LogItem& li = m_uiLogs[m_viewIdx[it.iItem]];
        CString s;
        switch (it.iSubItem) {
        case 0: s.Format(L"%02d:%02d:%02d.%03d", li.st.wHour, li.st.wMinute, li.st.wSecond, li.st.wMilliseconds); break;
        case 1: s = ChannelNameOf(li.chId); break;
        case 2: s = LevelText(li.level); break;
        case 3: s = li.text; break;
        }
        lstrcpynW(it.pszText, s, it.cchTextMax);
    }
    *pResult = 0;
}
```

要点：

1. 自动滚动：用户手动往上翻看时（滚动条不在底部）自动暂停滚动，回到底部后恢复，避免“正在看日志时被新日志顶走”。
2. `NM_CUSTOMDRAW` 按级别设置文字颜色：ERROR 红、WARN 橙、DEBUG 灰。
3. 支持 Ctrl+C 复制选中行（多选），方便现场人员发给开发排查。
4. 界面只显示最近 2000 行，完整内容看日志文件，“打开日志目录”按钮直接 `ShellExecute` 打开当天目录。

### 12.5 防误关闭与托盘

工业现场最常见的“故障”是操作员随手点了 × 或按了 Esc，程序就没了。处理方式：

```cpp
void CFtpUploaderDlg::OnOK()     { /* 屏蔽回车关闭 */ }
void CFtpUploaderDlg::OnCancel() { ShowWindow(SW_HIDE); }        // Esc 和 × 都只隐藏到托盘
void CFtpUploaderDlg::OnSysCommand(UINT nID, LPARAM lParam) {
    if ((nID & 0xFFF0) == SC_MINIMIZE) { ShowWindow(SW_HIDE); return; }
    CDialogEx::OnSysCommand(nID, lParam);
}
// 真正退出只有一个入口：托盘菜单“退出”→ 确认框（可选输入退出密码）→ StopAll → EndDialog
```

托盘：

1. `Shell_NotifyIcon(NIM_ADD)` 添加图标，四种图标：绿 = 全部启用通道在线，黄 = 有通道在重连，红 = 有通道配置错误/登录失败/文件搁置，灰 = 全部停止。
2. 鼠标悬停提示：`FTP上传 3/4 在线，待传 69`（`szTip` 最多 128 字符）。
3. 通道掉线超过 5 分钟、恢复时，弹气泡提示（`NIF_INFO`），同一通道 10 分钟内最多提示一次。
4. 处理 `RegisterWindowMessage(L"TaskbarCreated")`：资源管理器崩溃重启后托盘图标会消失，收到这个消息时重新 `NIM_ADD`。很多工具忽略这一点，导致现场“图标没了以为程序退出了”又双开一个。
5. 双击托盘图标显示主窗口并 `SetForegroundWindow`。

### 12.6 通道编辑对话框（CConnEditDlg）

```
┌─ 通道配置 ─────────────────────────────────────────────────────┐
│ ┌基本──────────────────────────────────────────────────────┐ │
│ │ 名称 [1号线视觉           ]  ☑ 启用                       │ │
│ └──────────────────────────────────────────────────────────┘ │
│ ┌服务器────────────────────────────────────────────────────┐ │
│ │ 地址 [192.168.10.20       ] 端口 [21   ]                  │ │
│ │ 用户 [line1               ] 密码 [********] ☐ 匿名登录    │ │
│ │ 模式 (●)被动PASV ( )主动PORT  编码 [自动 ▼]  [测试连接]   │ │
│ └──────────────────────────────────────────────────────────┘ │
│ ┌路径──────────────────────────────────────────────────────┐ │
│ │ 本地目录 [D:\Data\Line1                 ] [浏览...]       │ │
│ │ 远程目录 [/line1/{yyyy}{MM}{dd}         ] [占位符说明]    │ │
│ │ ☐ 包含子目录  包含 [*.csv;*.jpg ]  排除 [*.tmp;*.part;~*] │ │
│ └──────────────────────────────────────────────────────────┘ │
│ ┌上传策略──────────────────────────────────────────────────┐ │
│ │ 扫描间隔 [2 ]秒  稳定时间 [3 ]秒  单文件最多重试 [5 ]次    │ │
│ │ 上传后 (●)移到备份 ( )删除 ( )保留  备份目录 [D:\Backup\Line1][浏览] │
│ │ 远程同名 [覆盖 ▼]  ☑ 临时文件名上传 [.uploading]  ☑ 大小校验 │ │
│ └──────────────────────────────────────────────────────────┘ │
│ [高级 >>]（超时、心跳、重连间隔、空闲断开、文件天数限制）     │
│                                             [确定]  [取消]     │
└────────────────────────────────────────────────────────────────┘
```

保存时的校验：

| 项 | 规则 |
|---|---|
| 名称 | 非空，不与其他通道重名，不含 `\/:*?"<>|`（用于日志文件名） |
| 地址 | 非空，去除首尾空格；若用户误填 `ftp://192.168.1.1/xxx`，自动拆出主机和路径并提示 |
| 端口 | 1–65535 |
| 本地目录 | 非空；不存在时提示“目录当前不存在，是否仍然保存？”（U 盘/网络共享场景允许） |
| 远程目录 | 自动补前导 `/`，反斜杠替换为 `/`，去掉末尾 `/` |
| 备份目录 | 移动模式下必填；不能等于或位于本地目录之内 |
| 与其他通道 | 两个启用的通道本地目录相同且过滤条件重叠时警告（会导致重复上传或互相删除） |
| 数值项 | 扫描间隔 ≥ 1，稳定时间 ≥ 1，重连最小 ≥ 1 且 ≤ 最大 |

### 12.7 测试连接

点击“测试连接”后，在**临时线程**中用当前界面上的参数（未保存的）执行：解析地址 → 连接 → 登录 → 进入远程目录（不存在则询问是否创建）→ 上传 1 个 16 字节的测试文件 `_ftptest_<时间>.tmp` → SIZE 校验 → 删除。每一步结果实时显示在对话框下方的结果框中，任何一步失败就停止并给出原因和建议（见第 15 章）。

线程安全：测试线程和对话框之间用一个引用计数的共享对象通信。对话框关闭时设置 `bCancel` 并置停止事件，测试线程退出前检查 `bCancel`，不再 `PostMessage` 给已销毁的窗口。测试期间“确定”“取消”“测试连接”按钮仍可用（取消测试 = 置停止事件，最多 200ms 响应）。

### 12.8 全局设置对话框

开机自动启动、启动后最小化到托盘、日志保留天数（默认 30）、备份保留天数（默认 30）、界面日志行数（默认 2000）、启用 DEBUG 日志、退出密码（可选，存 SHA-256 哈希）、禁止系统睡眠（默认开）。

---

## 13. 线程间通信

| 方向 | 方式 | 说明 |
|---|---|---|
| UI → Worker：启动/停止 | `Start()` / 停止事件 `SetEvent` | 所有等待按 200ms 分片，停止通常 ≤ 1 秒完成 |
| UI → Worker：改配置 | 停止旧 Worker → 用新配置创建新 Worker | 运行中配置只读，无需加锁 |
| UI → Worker：重试失败文件 | `InterlockedExchange` 标志位 | Worker 主循环开头检查 |
| Worker → UI：状态 | `CCritSec` 保护的 `ChannelStatus`，UI 每 500ms 拷贝 | 锁内只拷贝 |
| Worker → UI：日志 | `CLogger` UI 队列（有上限），UI 每 200ms 取 | 不用 PostMessage |
| Worker → 看门狗 | `m_dwBeat`（`InterlockedExchange` 写，直接读） | |
| 测试线程 → 编辑对话框 | `PostMessage(WM_TEST_STEP, ...)`，参数为 `new` 出来的结果对象，对话框处理后 `delete` | 对话框已销毁时测试线程不发 |

为什么 Worker 不用 `PostMessage` 推送日志：Windows 每个线程的消息队列上限默认 10000 条，高频上传或刷错误时会塞满，导致 `PostMessage` 失败、界面无响应，并且消息里 `new` 出来的对象会在失败时泄漏。“UI 定时拉取 + 有界队列”在任何负载下行为都是确定的。

---

## 14. 7×24 稳定性设计

### 14.1 单实例

```cpp
BOOL CFtpUploaderApp::InitInstance() {
    // 用程序目录的哈希做互斥量名：同一台电脑上不同目录的两份程序可以并存（例如测试版与正式版）
    m_hMutex = ::CreateMutexW(NULL, TRUE, L"Local\\FtpUploader_" + HashOfExeDir());
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        // 通知已运行的实例显示窗口，然后自己退出
        ::PostMessage(HWND_BROADCAST, ::RegisterWindowMessageW(L"FtpUploader_Show"), 0, 0);
        return FALSE;
    }
    ...
}
```

两个实例同时监控同一目录会造成重复上传和互相删除文件，单实例是必须的。

### 14.2 崩溃处理

```cpp
// CrashDump.cpp —— 在 InitInstance 最开始调用 InstallCrashHandler()
static LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* ep) {
    WCHAR path[MAX_PATH];
    SYSTEMTIME st; ::GetLocalTime(&st);
    swprintf_s(path, L"%s\\Dump\\FtpUploader_%04d%02d%02d_%02d%02d%02d.dmp",
               (LPCWSTR)GetExeDir(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE h = ::CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei = { ::GetCurrentThreadId(), ep, FALSE };
        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), h,
            (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo),
            &mei, NULL, NULL);
        ::CloseHandle(h);
    }
    return EXCEPTION_EXECUTE_HANDLER;               // 直接结束进程，由守护进程重启
}
static void OnPurecall()     { ::RaiseException(0xE0000001, 0, 0, NULL); }
static void OnInvalidParam(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
                             { ::RaiseException(0xE0000002, 0, 0, NULL); }

void InstallCrashHandler() {
    // 关键：禁止系统弹出“程序已停止工作”对话框。否则进程会挂在对话框上不退出，守护进程无法重启
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    ::SetUnhandledExceptionFilter(OnUnhandled);
    _set_purecall_handler(OnPurecall);              // 纯虚函数调用
    _set_invalid_parameter_handler(OnInvalidParam); // CRT 参数校验失败（如 printf 格式串错误）
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);  // abort 不弹框
}
```

> `SEM_FAILCRITICALERRORS` 还能防止访问已拔出的 U 盘、光驱时弹出“驱动器中没有磁盘”的模态框把扫描线程卡死。Dump 文件配合存档的同版本 PDB，用 VS2010 或 WinDbg 打开即可定位到崩溃的代码行。Dump 目录只保留最近 20 个文件。

### 14.3 看门狗（线程卡死检测）

UI 定时器每 5 秒调用 `CChannelManager::CheckWatchdog()`：

1. 对每个运行中的通道，`GetTickCount() - worker->GetHeartbeat()` 超过阈值（默认 `max(120 秒, IoTimeout × 3)`，本地目录为网络共享时再加 60 秒）判定为卡死。
2. 卡死处理：记 ERROR 日志（含线程 ID，方便对照 dump），界面该通道标红“线程无响应”，调用 `Stop(3000)`。
3. 停止成功：用原配置新建 Worker 启动，记日志“通道已自动重建”。
4. 停止失败：移入 zombie 列表（见 7.7），等待其自然退出后再重建；同一通道在旧线程退出前不重建，避免重复上传。
5. 有 zombie 存在超过 10 分钟：判定进程状态已不可靠，写日志后以退出码 `2`（约定为“请求重启”）调用 `ExitProcess`，由守护进程拉起。

正常情况下工作线程内所有等待都是 200ms 分片的，不应出现卡死；看门狗用来兜底那些不可中断的系统调用：`GetAddrInfoW`（DNS 不通时）、网络共享上的 `FindFirstFile` / `CreateFile`、被杀毒软件拦截的文件操作。

### 14.4 守护进程（FtpGuard.exe）

一个约 200 行的独立小程序（Win32 无窗口程序，不依赖 MFC），开机自启的是它而不是主程序：

```
FtpGuard.exe 启动
  └─ 循环：
       CreateProcess(FtpUploader.exe /guarded)
       WaitForSingleObject(进程句柄, 10 秒) ── 超时 ─┐
         │进程退出                                   │
         ├─ 退出码 0（用户从托盘正常退出）→ 守护也退出  │
         ├─ 其他（崩溃 / 退出码 2 请求重启）         │
         │    → 记 Guard.log，等 5 秒，重新拉起        │
         │    → 10 分钟内重启超过 5 次：间隔改为 5 分钟，防止反复崩溃刷屏
         │                                           ▼
         │                  界面挂死检测：FindWindow 找到主窗口，
         │                  SendMessageTimeout(WM_NULL, SMTO_ABORTIFHUNG, 5000)
         │                  连续 3 次失败 → TerminateProcess → 重新拉起
         └───────────────────────────────────────────┘
```

说明：

1. 用户从托盘正常退出时，主程序以退出码 0 结束，守护进程看到 0 也一起退出，不会出现“关不掉”的情况。
2. 主程序启动时检查命令行，没有 `/guarded` 参数时（用户直接双击了主程序），先检查守护是否在运行，不在则启动守护后自己退出，由守护拉起，保证始终受保护。
3. 守护进程本身极简，不做网络和文件操作，基本不会出问题。
4. 为什么不做成 Windows 服务：Vista 之后服务运行在 Session 0，不能显示界面，MFC 对话框程序直接做服务无法看到界面。若后续需要“未登录也能上传”，需拆成“服务（上传核心）+ 界面客户端（命名管道通信）”两个程序，见附录 19.3。

### 14.5 开机自启与系统设置

| 项 | 做法 |
|---|---|
| 自启 | 写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，值为 `"D:\FtpUploader\FtpGuard.exe"`；不需要管理员权限 |
| 自动登录 | 现场电脑需配置 Windows 自动登录（推荐 Sysinternals Autologon，密码加密存储，不要直接在注册表写明文） |
| 禁止睡眠 | 程序运行期间调用 `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)`；同时在部署时把电源计划设为“从不睡眠”、关闭硬盘休眠、网卡“允许计算机关闭此设备以节约电源”取消勾选 |
| 关机/注销 | 处理 `WM_QUERYENDSESSION` 返回 TRUE，`WM_ENDSESSION` 中快速 `StopAll(3000)` 并刷日志，退出码 0 |
| 系统时间修改 | 所有间隔用 `GetTickCount` 差值；只有“跨天”判断用系统日期，向回调时间不会重复清零统计（记录上次日期，日期变大才算跨天） |

### 14.6 资源泄漏防控

长期运行的程序，最大的敌人是“每次泄漏一点点”。规则：

1. 所有 `HANDLE` / `SOCKET` / `FindFirstFile` 句柄 / GDI 对象都用 RAII 包装类或在同一函数内配对释放，代码审查逐条检查每个 return 路径。
2. 所有容器都有上限或清理机制：日志队列（上限）、界面日志（上限）、`m_cand`（每轮清理不存在的）、`m_fails`（清理不存在的）、上传记录（按天数淘汰）、`m_postPending`（处理成功即删）。
3. 资源自检：UI 定时器每 10 分钟记录一次 `GetProcessMemoryInfo`（工作集、提交内存）、`GetProcessHandleCount`、`GetGuiResources(GR_GDIOBJECTS / GR_USEROBJECTS)` 到 System.log，并显示在状态栏。长期测试时把这些数据画成曲线就能看出有无泄漏。
4. 阈值保护：提交内存 > 500MB 或句柄数 > 5000 或 GDI 对象 > 5000 时记 ERROR，并在空闲时（所有通道无上传）以退出码 2 退出，由守护进程重启。这是最后一道保险，正常不应触发。
5. CString、std::map 等频繁分配释放会产生堆碎片，XP 上建议程序启动时对进程堆启用低碎片堆（Win7 以上默认开启）：`ULONG lfh = 2; HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation, &lfh, sizeof(lfh));`

### 14.7 GetTickCount 回绕

`GetTickCount` 返回 DWORD，约 49.7 天回绕到 0。本项目所有计时都用 `(DWORD)(GetTickCount() - start) >= interval` 形式（见 3.3 的 `TickElapsed`），无符号相减在回绕时结果依然正确。**禁止**写成 `GetTickCount() >= start + interval`，这种写法在回绕前后会出现长达 49 天不触发或立即触发的问题。代码审查重点检查。

---

## 15. 异常处理与错误码

`ErrText(err, lastReply)` 把错误码转成现场人员看得懂的中文，日志和界面统一使用。测试连接时额外给出“建议”列的内容。

| 错误 | 界面文字 | 常见原因 | 建议 |
|---|---|---|---|
| FE_RESOLVE | 无法解析服务器地址 | 域名写错、DNS 不通 | 改用 IP；检查 DNS 设置 |
| FE_TIMEOUT（连接阶段） | 连接超时 | 网线、IP 错、服务器关机、防火墙丢包 | `ping` 服务器；检查防火墙 21 端口 |
| FE_SOCKET（10061） | 连接被拒绝 | 服务器在但 FTP 服务没开或端口错 | 检查 FTP 服务和端口 |
| FE_SOCKET（10065/10051） | 网络不可达 | 本机网卡断开、网段/网关错 | 检查本机网络 |
| FE_SESSION（421） | 服务器断开连接 | 连接数满、服务器空闲超时、维护 | 调大服务器每 IP 连接数 |
| FE_LOGIN（530） | 用户名或密码错误 | 密码错；或服务器限制登录 | 核对账号；检查服务器是否封了本机 IP |
| FE_TIMEOUT（PASV 数据连接） | 数据连接超时 | 服务器防火墙没放行被动端口范围 | 服务器开放被动端口范围；或改用 PORT 模式 |
| FE_SESSION（425） | 无法建立数据连接 | 同上 | 同上 |
| FE_FILE_PERM（550 进入目录） | 远程目录无权限 | 账号无写权限、目录不存在且无创建权限 | 服务器给该账号写/创建目录权限 |
| FE_FILE_PERM（553） | 文件名不被接受 | 文件名含服务器不支持的字符、编码不一致 | 调整编码设置（UTF-8 / GBK） |
| FE_FILE_TEMP（452/552） | 服务器空间不足 | 服务器磁盘满或配额满 | 清理服务器磁盘 |
| FE_VERIFY | 大小校验不一致 | 传输被中间设备篡改、服务器写入异常 | 检查服务器磁盘；查看服务器日志 |
| FE_LOCAL_OPEN | 文件被占用 | 设备程序仍在写、杀毒软件扫描 | 正常现象，会自动重试；频繁出现则加大稳定时间 |
| 本地目录不可访问 | 本地目录不存在 | U 盘拔出、网络共享掉线、路径改了 | 检查路径 |

Winsock 错误码保存在 `CFtpSocket` 的 `m_nWsaErr` 中（每次出错时 `WSAGetLastError()` 记录），日志中同时输出数字，便于开发人员排查，例如：`连接被拒绝 (WSA 10061)`。

---

## 16. 测试方案

### 16.1 测试环境

| 角色 | 配置 |
|---|---|
| 被测机 | Win7 x86 + Win10 x64 各一台（有 XP 现场再加一台 XP SP3） |
| FTP 服务器 | FileZilla Server（Windows）、vsftpd（Linux）、IIS FTP 各一套，三者行为差异较大，都要覆盖 |
| 网络故障模拟 | clumsy（Windows 下模拟丢包、延迟、断流）、手动拔网线、禁用网卡、Windows 防火墙规则 |
| 文件生成器 | 自写小工具：按设定频率生成文件，可模拟“慢速写入”（每秒写 1KB，持续 30 秒）和“写一会停一会” |
| 校验工具 | 对比脚本：本地备份目录与服务器目录的文件列表、大小、MD5 逐一比对 |

### 16.2 功能测试

| 编号 | 用例 | 预期 |
|---|---|---|
| T01 | 新增通道，IP / 域名两种方式，正确参数 | 测试连接各步骤通过；启动后状态“已连接” |
| T02 | 用户名 / 密码 / 端口 / IP 分别填错 | 测试连接给出对应的中文原因 |
| T03 | 本地放入 100 个文件 | 全部上传，顺序按修改时间，远程大小一致 |
| T04 | 慢速写入 1 个文件（持续 30 秒） | 写完之前不上传；写完后上传完整文件 |
| T05 | 远程目录 `{yyyy}{MM}{dd}` 不存在 | 自动逐级创建 |
| T06 | 包含子目录模式，三层子目录 | 远程目录结构一致 |
| T07 | 三种后处理模式 | 删除 / 移动到日期目录 / 保留且不重复上传 |
| T08 | “保留”模式下重启程序 | 已传文件不重复上传 |
| T09 | 远程已有同名文件，三种策略 | 覆盖 / 改名 / 跳过 行为正确 |
| T10 | 中文文件名、中文目录，UTF-8 服务器和 GBK 服务器 | 服务器上显示正确不乱码 |
| T11 | 0 字节文件、2GB 以上大文件 | 正确上传，大文件进度显示正常 |
| T12 | 文件被独占打开（模拟设备在写） | 不上传，记“被占用”，关闭后上传 |
| T13 | 远程目录无写权限 | 文件重试 5 次后搁置；“重试失败文件”后重新排队 |
| T14 | 16 个通道同时运行，指向同一服务器不同目录 | 互不影响；停止其中一个，其他继续 |
| T15 | 编辑运行中的通道（改远程目录） | 该通道重启，其他通道不受影响 |
| T16 | 点 ×、按 Esc、按回车 | 只隐藏到托盘，不退出 |
| T17 | 结束 explorer.exe 再重启 | 托盘图标自动恢复 |
| T18 | 双击两次程序 | 只有一个实例，第二次唤出已有窗口 |
| T19 | PORT 模式 | 能上传（需本机防火墙放行） |
| T20 | 日志：按天分目录、CSV 用 Excel 打开中文正常、过期日志被清理（改小保留天数验证） | 符合 11 章 |

### 16.3 故障注入测试（重点）

每个用例都要在“空闲”和“正在上传大文件”两种状态下各做一次。

| 编号 | 故障 | 预期 |
|---|---|---|
| R01 | 拔掉本机网线 1 分钟后插回 | ≤ 30 秒发现断线；插回后 ≤ 60 秒恢复；中断的文件重传，服务器无残留正式文件 |
| R02 | 拔网线 2 小时 | 期间退避到 60 秒一次，日志不刷屏；积压文件恢复后全部上传 |
| R03 | 禁用 / 启用本机网卡 | 同 R01 |
| R04 | 停止 FTP 服务 → 启动 | 自动恢复 |
| R05 | 服务器断电（虚拟机直接关机） | 同 R01，靠 KeepAlive / 心跳发现 |
| R06 | 服务器防火墙阻断被动端口（控制连接正常） | 数据连接超时 → 重连，退避逐步增大，不出现 3 秒一次的死循环 |
| R07 | 服务器每 IP 最大连接数设为 1，再用 FileZilla 客户端占住 | 421 → 退避重连，客户端断开后恢复 |
| R08 | 服务器上踢掉本连接 | 立即重连 |
| R09 | 服务器磁盘写满 | 文件级临时错误，界面告警；清理后自动继续 |
| R10 | 上传中修改服务器密码 | 530，60 秒重试，界面红色；改配置后恢复 |
| R11 | clumsy 30% 丢包 + 500ms 延迟 | 上传变慢但最终成功，无残缺文件 |
| R12 | 上传完成、改名前断网（调试器断点模拟） | 重连后识别“远程已存在且大小一致”，不重复上传 |
| R13 | 本地目录是 U 盘，拔出再插入 | 显示“本地目录不可访问”，插回后自动继续，无系统弹框 |
| R14 | 修改系统时间（前调 1 天、后调 1 天） | 心跳、重连照常；统计跨天逻辑正确 |
| R15 | 任务管理器结束主程序进程 | 守护进程 5 秒内重新拉起，自动启动所有已启用通道 |
| R16 | 代码中埋入测试开关故意触发访问违例 | 生成 dump，无系统弹框，守护拉起 |
| R17 | 主程序 UI 线程故意死循环（测试开关） | 守护约 1 分钟内（3 次 × 10 秒检查 + 5 秒超时）判定无响应并重启 |
| R18 | 托盘“退出” | 守护进程一起退出 |
| R19 | 断电重启电脑 | 自动登录 → 守护自启 → 通道自启 → 补传断电前未传的文件 |
| R20 | 设置日志目录只读 / 磁盘满 | 上传不受影响，状态栏提示日志写入失败 |

### 16.4 压力与长稳测试

| 编号 | 场景 | 时长 | 通过标准 |
|---|---|---|---|
| S01 | 单通道每秒 10 个 100KB 文件 | 24 小时 | 无积压增长；CPU < 5% |
| S02 | 16 通道，每通道每 5 秒 1 个 2MB 文件 | 72 小时 | 内存、句柄、GDI 曲线平稳（第 1 小时后增长 < 5%） |
| S03 | S02 基础上每 2 小时脚本禁用网卡 2 分钟 | 7 天 | 全部恢复，校验脚本比对零丢失、零重复、零残缺 |
| S04 | 单目录累积 10 万个文件（“保留”模式） | 一次 | 扫描一轮 < 2 秒，界面不卡 |
| S05 | GetTickCount 回绕 | — | 用 Application Verifier 的 TimeRollOver 检查项，或把 `GetTickCount` 封装成可注入偏移的函数，在回绕前 10 分钟启动测试 |

长稳测试期间用 perfmon 记录 `Process\Private Bytes`、`Process\Handle Count`、`Process\Thread Count`，每分钟一个点，结束后导出曲线归档到测试报告。

---

## 17. 部署与运维

### 17.1 发布目录

```
D:\FtpUploader\                  （不要放在 C:\Program Files，避免权限和 UAC 虚拟化问题）
├─ FtpGuard.exe                  守护进程（开机自启的是它）
├─ FtpUploader.exe               主程序
├─ Config\FtpUploader.ini (+ .bak)
├─ Data\History_*.txt            上传记录（“保留”模式）
├─ Log\yyyy-MM-dd\
└─ Dump\
```

PDB 文件不随程序发布，按版本号存档在开发服务器（例如 `\\dev\release\FtpUploader\V1.0.3\`），现场发回 dump 时取对应版本 PDB 分析。

### 17.2 现场部署清单

1. 拷贝发布目录到 D 盘；首次运行在界面中配置通道，或拷贝已配好的 ini（密码填 `PasswordPlain`）。
2. 全局设置中勾选“开机自动启动”（写 Run 注册表，指向 FtpGuard.exe）。
3. 配置 Windows 自动登录；电源计划“从不睡眠”；网卡电源管理取消“允许关闭此设备”。
4. 关闭 Windows 自动更新的自动重启（或设定维护时段），防止半夜被系统重启。
5. 杀毒软件把本地监控目录、程序目录加入排除，减少“文件被占用”。
6. 使用 PORT 模式时，Windows 防火墙放行 FtpUploader.exe。
7. 拔一次网线验证自动重连；重启一次电脑验证自启和补传。
8. 服务器端核对：每 IP 连接数 ≥ 通道数 + 2（测试连接会额外占用）；被动端口范围已在防火墙放行；防暴力破解的封禁阈值不要太低。

### 17.3 版本升级

托盘退出（守护随之退出）→ 备份 `Config`、`Data` 目录 → 替换两个 exe → 运行 FtpGuard.exe。配置文件带 `Version` 字段，新版本读取到旧版本配置时自动补齐新增字段的默认值并写回，不需要重新配置。

### 17.4 常见问题排查

| 现象 | 排查 |
|---|---|
| 能登录，上传一直超时 | 99% 是被动端口被服务器防火墙拦了：服务器开放被动端口范围，或改 PORT 模式 |
| 服务器上中文文件名乱码 | 编码改为另一种（UTF-8 ↔ GBK）；Windows IIS、新版 FileZilla 一般 UTF-8，老设备内置 FTP 多为 GBK |
| 文件一直不上传 | 看日志：是否被过滤规则排除、是否一直“被占用”、是否在搁置列表；DEBUG 日志可看到扫描细节 |
| 文件重复上传 | 检查是否有两个通道监控同一目录；是否两份程序同时在运行（不同目录各一份） |
| 隔几天程序就没了 | 查看 Dump 目录和 Guard.log；查看 Windows 事件查看器 → 应用程序日志 |
| 服务器上残留 `.uploading` 文件 | 中断上传的残留，下次同名文件上传时会被覆盖；服务器端可定期清理超过 1 天的 `*.uploading` |

---

## 18. 开发计划

按 1 名熟悉 MFC 和 Winsock 的开发人员估算：

| 阶段 | 内容 | 工作量（人天） | 产出 / 验收 |
|---|---|---|---|
| M1 基础框架 | 工程搭建、Sync/StrUtil/Crypto、CLogger、ConfigStore、崩溃处理 | 3 | 日志按天写入；配置读写含中文；故意崩溃能生成 dump |
| M2 FTP 客户端 | CFtpSocket、CFtpClient（登录、PASV/PORT、STOR、SIZE、RNFR/RNTO、MKD） | 5 | 控制台测试程序对三种服务器上传成功；拔网线能在超时内返回 |
| M3 通道核心 | CFtpWorker 状态机、重连退避、心跳、CDirScanner、上传流程、后处理、CUploadHistory | 6 | 无界面下单通道连续运行 24 小时，R01–R12 通过 |
| M4 界面 | 主界面、虚拟日志列表、通道编辑、测试连接、全局设置、托盘 | 5 | T01–T20 通过 |
| M5 稳定性 | 看门狗、FtpGuard、单实例、自启、资源监控 | 3 | R13–R20 通过 |
| M6 测试修复 | 全部用例 + S01–S04 | 5 | 测试报告 |
| M7 长稳 | S03（7 天），期间修复问题 | 7（日历时间，可并行做文档） | 资源曲线、比对报告 |
| 合计 | | 约 27 人天 + 7 天长稳 | |

建议 M2 完成后先做一个“命令行版上传器”在现场试跑，尽早暴露现场服务器的兼容性问题（编码、被动端口、权限），再做界面。

---

## 19. 附录

### 19.1 FTP 应答码速查

| 码 | 含义 | 本程序处理 |
|---|---|---|
| 120 | 服务稍后就绪 | 继续等待 220 |
| 125 / 150 | 数据连接已打开 / 即将打开 | 开始发送数据 |
| 200 | 命令成功 | |
| 211 | 系统状态（FEAT 应答） | 解析 UTF8 |
| 213 | 文件状态（SIZE 应答） | 解析大小 |
| 220 | 服务就绪 | 连接成功 |
| 221 | 再见 | QUIT 应答 |
| 226 / 250 | 传输完成 / 文件操作完成 | 成功 |
| 227 | 进入被动模式 | 解析端口 |
| 230 | 登录成功 | |
| 257 | 目录已创建 | |
| 331 | 需要密码 | 发送 PASS |
| 350 | 等待下一步（RNFR 应答） | 发送 RNTO |
| 421 | 服务不可用，关闭连接 | 会话错误 → 重连 |
| 425 / 426 | 无法打开数据连接 / 传输中止 | 会话错误 → 重连 |
| 450 / 451 / 452 | 文件不可用 / 本地错误 / 空间不足 | 文件临时错误 → 延迟重试 |
| 500 / 501 / 502 / 504 | 命令或参数错误 / 未实现 | SIZE、OPTS 等可选命令忽略；其他按文件错误 |
| 530 | 未登录 | 登录错误 → 最大间隔重连 |
| 550 | 文件/目录不可用（不存在、无权限） | 视上下文：SIZE 时表示不存在；STOR/CWD 时为权限错误 |
| 552 | 超出存储分配 | 文件临时错误 |
| 553 | 文件名不允许 | 文件永久错误 |

### 19.2 参数默认值与调整建议

| 参数 | 默认 | 调整建议 |
|---|---|---|
| 扫描间隔 | 2 秒 | 实时性要求高可设 1 秒；单目录文件数上万时设 5–10 秒 |
| 稳定时间 | 3 秒 | 设备写文件慢（大图、网络写入）设 5–10 秒 |
| 连接超时 | 10 秒 | 跨厂区 / VPN 链路设 20 秒 |
| 收发超时 | 30 秒 | 无进展超时，一般不用改 |
| 心跳间隔 | 30 秒 | 必须小于服务器空闲超时（FileZilla 默认 120 秒，vsftpd 默认 300 秒） |
| 重连最小 / 最大 | 3 / 60 秒 | 服务器有防暴力破解时最大值不低于 60 秒 |
| 单文件最多重试 | 5 次 | |
| 界面日志行数 | 2000 | 不建议超过 10000 |
| 日志保留 | 30 天 | 按磁盘大小调整；每通道每天日志通常 < 10MB |

### 19.3 后续扩展方向

| 扩展 | 思路 |
|---|---|
| FTPS（FTP over TLS） | 在 CFtpSocket 下加一层 SChannel（Windows 自带 TLS，XP 只支持到 TLS 1.0，需评估）；或引入 OpenSSL |
| SFTP（SSH） | 协议完全不同，引入 libssh2，新增 CSftpClient 实现同样的接口，CFtpWorker 通过接口类调用 |
| 断点续传 | 失败时记录已传字节，重连后 `REST <offset>` + `STOR`（或 `APPE`），适合几百 MB 以上的大文件 |
| 一个目录上传到多台服务器 | 同一目录配置多个通道，后处理改为“所有通道都成功后才删除/移动”，需要通道间协调 |
| 服务化 | 上传核心做成 Windows 服务，界面独立成客户端，通过命名管道读取状态和日志，实现“不登录也上传” |
| 远程监控 | 增加 HTTP 状态接口或定时写状态文件，供 MES / 监控平台采集；如开放网络接口需加认证 |
| 上传完成通知 | 每个文件传完后在远程写 `.ok` 标志文件，或调用 MES 的 HTTP 接口 |

### 19.4 开发避坑清单（代码审查用）

1. 工作线程中出现任何 `CWnd*`、`GetDlgItem`、`SetWindowText`、`SendMessage` → 不通过。
2. 出现 `GetTickCount() >= x + y` 形式的比较 → 不通过，改用 `TickElapsed`。
3. 出现没有超时的 `recv` / `send` / `connect` / `accept` / `WaitForSingleObject(INFINITE)`（线程退出等待除外）→ 不通过。
4. 出现 `TerminateThread` → 不通过。
5. 出现 `CreateThread` → 改为 `_beginthreadex`。
6. 日志中出现密码明文（包括 DEBUG 级别）→ 不通过。
7. 新增容器（map/set/deque/vector）没有说明上限或清理时机 → 不通过。
8. 句柄、socket、`FindFirstFile` 句柄在某个提前 return 的分支没有释放 → 不通过。
9. 用系统时间（`CTime::GetCurrentTime`）计算间隔 → 不通过，只允许用于显示、日志、跨天判断、目录命名。
10. 文件上传后直接用正式名，没有走“临时名 → 校验 → 改名” → 不通过（除非该通道配置关闭了临时名）。
11. TYPE I 之前就发 STOR、或者重连后没重新发 TYPE I → 不通过。
12. 修改了配置结构体但没有同步：默认值、INI 读写、编辑界面、校验 → 不通过。

---

*文档结束*

