#pragma once
#include "Config.h"

// 一条统一的 TXT 日志，入队时复制任务身份并供界面显示。
struct LogItem
{
    CString m_strTime;
    CString m_strId;
    CString m_strName;
    CString m_strText;
    int m_nLevel;
    UINT m_unTaskNumber;

    LogItem()
        : m_nLevel(1),
          m_unTaskNumber(0)
    {
    }
};

// 异步日志器，调用线程只入队，后台线程负责写盘及按大小归档。
class CLogger
{
public:
    CLogger();
    ~CLogger();
    bool Start(const CString& strRoot, const GlobalConfig& stConfig);
    // 在锁内发布新设置并唤醒日志线程，使限制及时生效。
    void Configure(const GlobalConfig& stConfig);
    // 请求日志线程排空队列后退出，不阻塞调用线程。
    void RequestStop();
    // 非阻塞检查并回收已经退出的日志线程。
    bool PollStopped();
    // 请求停止并等待完成，用于最终析构回收。
    void Stop();
    void Write(const TaskConfig* pConfig, int nLevel, const CString& strText);
    // 将操作及结果写入系统日志，调用方不要传入密码等敏感值。
    void Operation(const CString& strAction, const CString& strResult, const TaskConfig* pConfig = NULL, int nLevel = 1);
    // 将完整上传明细写入任务 TXT 日志，同时进入界面日志队列。
    void Record(const TaskConfig& stConfig, const CString& strLocal, const CString& strRemoteDir,
                const CString& strRemoteName, ULONGLONG ullBytes, DWORD dwMs, const CString& strResult,
                const CString& strError);
    // 在锁内交换待显示队列，界面在锁外处理日志内容。
    void PopUi(std::deque<LogItem>& deqItems);

private:
    // 单个 TXT 文件的写入及归档故障状态，只由日志线程访问。
    struct FileState
    {
        DWORD m_dwWriteFailTick;
        DWORD m_dwArchiveFailTick;
        bool m_bWriteFailed;
        bool m_bArchiveFailed;

        FileState()
            : m_dwWriteFailTick(0),
              m_dwArchiveFailTick(0),
              m_bWriteFailed(false),
              m_bArchiveFailed(false)
        {
        }
    };

    static unsigned __stdcall ThreadProc(void* pContext);
    void Run();
    void Push(const LogItem& stItem);
    // 打开文件一次写入同一文件的一批日志，按大小在条目边界归档；unDropped 非 0 时先写丢弃提示。
    bool WriteLines(const CString& strPath, const std::vector<CStringA>& vecLines, bool bFlush,
                    const GlobalConfig& stConfig, FileState& stState, UINT& unDropped);
    bool OpenLog(const CString& strPath, CAutoHandle& hFile, ULONGLONG& ullSize);
    bool Archive(const CString& strPath, UINT unArchiveCount);
    bool CleanupArchives(const GlobalConfig& stConfig);
    CCritSec m_csLock;
    // 磁盘写入队列最多 50000 条，超出时累计丢弃数量。
    std::deque<LogItem> m_deqFileQueue;
    // 界面待显示队列最多 5000 条，独立于磁盘写入队列。
    std::deque<LogItem> m_deqUiQueue;
    HANDLE m_hThread;
    HANDLE m_hStop;
    HANDLE m_hWake;
    CString m_strRoot;
    GlobalConfig m_stConfig;
    bool m_bSettingsChanged;
    UINT m_unDropped;
};
