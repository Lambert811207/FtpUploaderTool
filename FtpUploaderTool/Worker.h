#pragma once
#include "FtpClient.h"
#include "Files.h"
#include "Logger.h"

// 供主窗口显示和着色使用的任务执行状态。
enum TaskState
{
    CS_STOPPED,
    CS_CONNECTING,
    CS_IDLE,
    CS_UPLOADING,
    CS_RECONNECT,
    CS_CONFIG_ERROR,
    CS_STOPPING
};

// 工作线程发布的状态快照，主窗口通过加锁复制读取。
struct TaskStatus
{
    int m_nState;
    CString m_strText;
    CString m_strCurrentFile;
    CString m_strLastError;
    CString m_strLastOk;
    ULONGLONG m_ullTotal;
    ULONGLONG m_ullSent;
    ULONGLONG m_ullTodayBytes;
    UINT m_unPending;
    UINT m_unParked;
    UINT m_unTodayOk;
    UINT m_unTodayFail;
    UINT m_unReconnects;

    TaskStatus()
        : m_nState(CS_STOPPED),
          m_strText(L"已停止"),
          m_ullTotal(0),
          m_ullSent(0),
          m_ullTodayBytes(0),
          m_unPending(0),
          m_unParked(0),
          m_unTodayOk(0),
          m_unTodayFail(0),
          m_unReconnects(0)
    {
    }
};

// 每个任务一个后台线程，依次处理扫描、连接、上传及本地文件后处理。
class CTaskWorker : public IFtpProgress
{
public:
    CTaskWorker(const TaskConfig& stConfig, const CString& strRoot, CLogger& loggerFile);
    ~CTaskWorker();
    // 使用当前配置启动线程；已有线程或配置无效时返回失败。
    bool Start();
    // 仅发送停止请求，不等待网络或文件操作结束。
    void RequestStop();
    // 非阻塞检查线程完成情况，完成后回收线程句柄。
    bool PollStopped();

    bool Running() const
    {
        return m_hThread != NULL;
    }

    const TaskConfig& Config() const
    {
        return m_stConfig;
    }

    // 在状态锁内复制快照，界面不直接访问工作线程的内部状态。
    TaskStatus Status();
    void Progress(ULONGLONG ullSent);

    // 读取工作线程最近活动的毫秒计时值。
    DWORD Beat() const
    {
        return (DWORD)InterlockedCompareExchange((LONG*)&m_lBeat, 0, 0);
    }

private:
    static unsigned __stdcall ThreadProc(void* pContext);
    void Run();
    void State(int nState, const CString& strText);
    void Error(const CString& strText);
    int UploadOne(CFtpClient& ftpClientSession, CUploadHistory& historyUpload, const PendingFile& stFile,
                  CString& strRemoteDir, CString& strFinalName);
    // 最多尝试 nAttempts 次，两次之间等待 500 毫秒；失败原因通过 strReason 返回。
    bool PostProcess(const PendingFile& stFile, int nAttempts, CString& strReason);
    // 执行一次删除或备份；源文件已不存在或版本已变化时视为完成。
    bool PostOnce(const PendingFile& stFile, CString& strReason);
    // 累计后处理失败次数，并按 1、2、4、8… 次记录原因，避免持续失败时刷屏。
    void PostFailed(PostInfo& stPost, const CString& strReason);
    TaskConfig m_stConfig;
    CString m_strRoot;
    CLogger& m_loggerFile;
    HANDLE m_hThread;
    HANDLE m_hStop;
    CCritSec m_csStatusLock;
    TaskStatus m_stStatus;
    volatile LONG m_lBeat;
    CTaskWorker(const CTaskWorker&);
    CTaskWorker& operator=(const CTaskWorker&);
};
