#pragma once
#include "FtpClient.h"
#include "Logger.h"

// 管理一次临时上传的持久化记录，未提交时析构尝试清理远端文件。
class CRemoteTempFile
{
public:
    CRemoteTempFile(const CString& strRoot, CFtpClient& ftpSession, CLogger* pLogger = NULL);
    ~CRemoteTempFile();
    // 上传前保存服务器信息和精确临时路径，记录失败时不得开始上传。
    bool Begin(const TaskConfig& stConfig, const CString& strDirectory, const CString& strName);
    // 正式改名或确认删除成功后撤销本次待清理记录。
    void Commit();

private:
    CString m_strRoot;
    CString m_strJournal;
    CString m_strKey;
    TaskConfig m_stConfig;
    CString m_strRemotePath;
    CFtpClient& m_ftpSession;
    CLogger* m_pLogger;
    CRemoteTempFile(const CRemoteTempFile&);
    CRemoteTempFile& operator=(const CRemoteTempFile&);
};

// 后台重试未完成的精确路径清理，重启后仍可读取持久化记录。
class CRemoteTempCleanup
{
public:
    CRemoteTempCleanup();
    ~CRemoteTempCleanup();
    bool Start(const CString& strRoot, CLogger& loggerFile);
    // 只请求停止，界面用 PollStopped 轮询完成情况。
    void RequestStop();
    bool PollStopped();
    // 等待线程退出的最终回收接口，正常界面退出采用异步轮询。
    void Stop();

private:
    static unsigned __stdcall ThreadProc(void* pContext);
    void Run();
    CString m_strRoot;
    CLogger* m_pLogger;
    HANDLE m_hThread;
    HANDLE m_hStop;
    CRemoteTempCleanup(const CRemoteTempCleanup&);
    CRemoteTempCleanup& operator=(const CRemoteTempCleanup&);
};
