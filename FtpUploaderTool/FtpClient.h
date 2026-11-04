#pragma once
#include "Config.h"

// 统一的网络、FTP 应答及本地文件错误码，成功为 0。
enum FtpError
{
    FE_OK = 0,
    FE_TIMEOUT = -1,
    FE_STOPPED = -2,
    FE_SOCKET = -3,
    FE_CLOSED = -4,
    FE_PROTOCOL = -5,
    FE_RESOLVE = -6,
    FE_LOGIN = -7,
    FE_SESSION = -8,
    FE_FILE_TEMP = -20,
    FE_FILE_PERM = -21,
    FE_LOCAL_OPEN = -22,
    FE_LOCAL_READ = -23,
    FE_VERIFY = -24,
    // 本地同名策略拒绝上传，与 FTP 权限错误分开记录。
    FE_REMOTE_EXISTS = -25,
    // 服务器缺少必要的目标状态查询能力时保留本地源文件。
    FE_REMOTE_STATE_UNKNOWN = -26,
    // 远程临时文件清理记录无法落盘，未开始上传。
    FE_TEMP_JOURNAL = -27,
    // 上传已完成但成功记录无法落盘，不确认成功，保留本地源文件。
    FE_HISTORY = -28
};

// 会话级错误需要重建连接；主动停止不属于重连错误。
inline bool IsSessionError(int nError)
{
    return nError < 0 && nError > -20 && nError != FE_STOPPED;
}

CString FtpErrorText(int nError, int nReplyCode, int nWsaError);

// 非阻塞套接字封装，等待过程同时受毫秒超时及停止事件控制。
class CFtpSocket
{
public:
    CFtpSocket();
    ~CFtpSocket();

    void SetStop(HANDLE hStop)
    {
        m_hStop = hStop;
    }

    int Connect(const sockaddr_in& stAddress, DWORD dwTimeout);
    // 发送全部字节，每次取得发送进展后重新计算无进展超时。
    int SendAll(const char* pBytes, int nCount, DWORD dwTimeout);
    // 读取一次可用数据，通过 nCount 返回实际字节数。
    int Receive(char* pBytes, int nCapacity, int& nCount, DWORD dwTimeout);
    // 主动模式等待服务器反向连接，并核对来源地址。
    int Accept(SOCKET sockListener, const sockaddr_in& stPeer, DWORD dwTimeout);
    void ShutdownSend();
    void Close(bool bAbort = true);

    SOCKET Get() const
    {
        return m_sockSocket;
    }

    int WsaError() const
    {
        return m_nWsaError;
    }

private:
    int Ready(SOCKET sockSocket, bool bWrite, DWORD dwTimeout);
    SOCKET m_sockSocket;
    HANDLE m_hStop;
    int m_nWsaError;
    CFtpSocket(const CFtpSocket&);
    CFtpSocket& operator=(const CFtpSocket&);
};

// 上传过程中报告累计字节数，接收方自行保证线程安全。
class IFtpProgress
{
public:
    virtual void Progress(ULONGLONG ullSent) = 0;

    virtual ~IFtpProgress()
    {
    }
};

// 管理一个任务的 FTP 控制会话及临时数据连接。
class CFtpClient
{
public:
    explicit CFtpClient(const TaskConfig& stConfig, HANDLE hStop);
    ~CFtpClient();
    int Connect();
    int Noop();
    int EnsureDirectory(const CString& strDirectory);
    // 准备完整目标目录后回到配置的固定目录，返回带尾斜杠的相对子路径。
    int PrepareUploadDirectory(const CString& strDirectory, CString& strRelativeDir);
    // 将相对于登录目录的路径转换为绝对路径，供路径比较及持久化清理使用。
    CString AbsoluteDirectory(const CString& strDirectory) const;
    // 返回文件大小；550 时大小保留 -1，命令不支持时 bSupported 为 false。
    int Size(const CString& strName, LONGLONG& llSize, bool& bSupported);
    int Upload(HANDLE hFile, const CString& strName, IFtpProgress* pProgress);
    int Rename(const CString& strFrom, const CString& strTo);
    int Delete(const CString& strName);
    // 清理精确临时路径，550 后用目录清单确认文件是否确实不存在。
    int DeleteTemporary(const CString& strPath);
    void Disconnect();

    bool Connected() const
    {
        return m_ftpSocketControl.Get() != INVALID_SOCKET;
    }

    int LastCode() const
    {
        return m_nCode;
    }

    CString ErrorText(int nError) const;

private:
    int Command(const CString& strCommand);
    int ReadReply();
    int ReadLine(CStringA& strALine);
    int DataConnection(CFtpSocket& ftpSocketData, SOCKET& sockListener);
    int MapError() const;
    int Expect(int nCode1, int nCode2 = 0) const;
    // 相对目录配置始终按当前目录生成命令参数，不向服务器发送前导斜杠。
    CString DirectoryArgument(const CString& strAbsolute) const;
    TaskConfig m_stConfig;
    HANDLE m_hStop;
    CFtpSocket m_ftpSocketControl;
    CStringA m_strABuffer;
    CString m_strReply;
    // 仅保留脱敏命令，用于错误诊断；不记录登录凭据。
    CString m_strCommand;
    CString m_strCwd;
    CString m_strLoginDir;
    sockaddr_in m_stPeer;
    // 上次成功解析的服务器地址，解析失败或超时时用于尝试连接。
    sockaddr_in m_stResolved;
    bool m_bResolved;
    UINT m_unCodePage;
    int m_nCode;
    CFtpClient(const CFtpClient&);
    CFtpClient& operator=(const CFtpClient&);
};
