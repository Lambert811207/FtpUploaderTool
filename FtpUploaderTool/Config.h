#pragma once
#include "Common.h"
#include "ConfigLimits.h"

// 界面和配置存储共同遵守的任务数量上限。
const UINT m_unMaxTasks = 32;

// 上传成功后对本地源文件执行的操作，枚举值同时用于配置存储。
enum PostAction
{
    POST_DELETE,
    POST_MOVE,
    POST_KEEP
};

// 服务器已有同名文件时的处理方式，文件大小不作为成功依据。
enum RemoteExist
{
    EXIST_OVERWRITE,
    EXIST_RENAME,
    EXIST_SKIP
};

// FTP 路径和文件名编码，不改变文件内容。
enum FtpEncoding
{
    ENC_AUTO,
    ENC_UTF8,
    ENC_GBK
};

// 单个任务的持久化参数，工作线程启动时持有自己的副本。
struct TaskConfig
{
    // 任务 GUID，用于关联配置、上传历史和远程临时文件记录。
    CString m_strId;
    CString m_strName;
    CString m_strHost;
    CString m_strUser;
    // 按用户要求明文保存在 INI 中，操作日志不记录此值。
    CString m_strPassword;
    // 本地监控目录，必须为绝对路径。
    CString m_strLocalDir;
    // 服务器上传目录，可包含占位符；无前导 / 时相对于 FTP 登录目录。
    CString m_strRemoteDir;
    // 仅移动策略使用的本地备份根目录，不按天数自动删除。
    CString m_strBackupDir;
    // 文件名包含规则，空串或 * 均允许全部文件名。
    CString m_strIncludeMask;
    // 文件名排除规则，多个规则用分号分隔，默认不排除。
    CString m_strExcludeMask;
    CString m_strTempSuffix;
    // 附加配置错误文本，非空时由统一校验返回。
    CString m_strError;
    // true 为客户端发起数据连接的 PASV，被动模式不改变控制连接方向。
    bool m_bPassive;
    // 主窗口复选框的持久化值，表示是否参与全部启动。
    bool m_bSelected;
    // 默认开启，仅控制本地子文件夹扫描，不枚举服务器目录。
    bool m_bIncludeSubDir;
    // 独立清理本地监控目录内的旧空子文件夹，默认关闭。
    bool m_bDeleteOldEmptyDirs;
    // 先上传临时文件名，完成后再改成正式名称。
    bool m_bUseTempName;
    // 使用 SIZE 核对上传字节数，不进行内容哈希校验。
    bool m_bVerifySize;
    // 持久化日志编号，改名或删除其他任务不会重新编号。
    UINT m_unTaskNumber;
    // FTP 控制连接端口，默认 21。
    UINT m_unPort;
    // 重新扫描本地目录的间隔，单位为秒。
    UINT m_unScanInterval;
    // 上一次文件上传尝试结束到下一次开始的最短间隔，单位为毫秒，0 表示连续上传。
    UINT m_unUploadIntervalMs;
    // 文件大小及修改时间连续不变后才允许上传，单位为秒。
    UINT m_unStableSec;
    // 单文件可计数失败上限，包含首次失败，网络重连不占用此计数。
    UINT m_unMaxFileRetry;
    // 按文件夹最后修改时间计算天数，仅启用空目录清理时使用。
    UINT m_unEmptyDirAgeDays;
    // 控制或数据连接建立的等待上限，单位为秒。
    UINT m_unConnTimeout;
    // 网络收发或应答等待的超时，单位为秒，不是整文件上传总时限。
    UINT m_unIoTimeout;
    // 连接空闲时发送 NOOP 的间隔，单位为秒。
    UINT m_unHeartbeat;
    // 网络故障后首次重连等待，单位为秒。
    UINT m_unReconnMin;
    // 连续失败的重连等待上限，单位为秒；达到后仍继续重试。
    UINT m_unReconnMax;
    // 没有待传文件时的主动断开等待，单位为秒，0 表示不主动断开。
    UINT m_unIdleDisconn;
    int m_nEncoding;
    int m_nPostAction;
    int m_nRemoteExist;
    TaskConfig();
    // 只返回首个配置错误，供编辑窗口及工作线程共用。
    bool Validate(CString& strMessage) const;
};

// 程序全局设置，日志限制分别作用于系统及各任务的日志系列。
struct GlobalConfig
{
    // 设置当前用户登录后的自启动，不是系统服务启动。
    bool m_bAutoStart;
    // 启动时隐藏到托盘，用户可恢复主窗口。
    bool m_bStartMinimized;
    // 阻止运行期间的自动睡眠，不拦截用户手动睡眠或关机。
    bool m_bPreventSleep;
    // 单个当前日志文件的大小上限，单位为 MB。
    UINT m_unLogMaxSizeMb;
    // 系统及每个任务的 TXT 日志各自允许保留的 backup 文件数量。
    UINT m_unLogArchiveCount;
    // 界面保留的日志行数上限，不限制磁盘日志总条数。
    UINT m_unUiMaxLines;
    // 下一个新任务日志编号，仅在任务成功保存后推进。
    UINT m_unNextTaskNumber;
    // 按参数顺序返回首个全局设置错误。
    bool Validate(CString& strMessage) const;

    GlobalConfig()
        : m_bAutoStart(false),
          m_bStartMinimized(false),
          m_bPreventSleep(true),
          m_unLogMaxSizeMb(20),
          m_unLogArchiveCount(10),
          m_unUiMaxLines(2000),
          m_unNextTaskNumber(1)
    {
    }
};

// 读取兼容旧格式的 INI，并通过临时文件及备份保存配置。
class CConfigStore
{
public:
    // 主配置不可用时尝试 .bak，读取旧字段后按需要迁移。
    bool Load(const CString& strPath, GlobalConfig& stGlobal, std::vector<TaskConfig>& vecConfigs, CString& strMessage);
    // 校验全局参数后写入 UTF-16 INI；失败原因返回到 strMessage。
    bool Save(const CString& strPath, const GlobalConfig& stGlobal, const std::vector<TaskConfig>& vecConfigs,
              CString& strMessage, bool bUpdateBackup = false);
};
