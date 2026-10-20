#pragma once
#include "FtpClient.h"
#include "Files.h"
#include "Logger.h"
enum TaskState { CS_STOPPED, CS_CONNECTING, CS_IDLE, CS_UPLOADING, CS_RECONNECT, CS_CONFIG_ERROR, CS_STOPPING };
struct TaskStatus {
    int m_nState; CString m_strText, m_strCurrentFile, m_strLastError, m_strLastOk; ULONGLONG m_ullTotal, m_ullSent, m_ullTodayBytes;
    UINT m_unPending, m_unParked, m_unTodayOk, m_unTodayFail, m_unReconnects;
    TaskStatus() : m_nState(CS_STOPPED), m_strText(L"已停止"), m_ullTotal(0), m_ullSent(0), m_ullTodayBytes(0), m_unPending(0), m_unParked(0), m_unTodayOk(0), m_unTodayFail(0), m_unReconnects(0) {}
};
class CTaskWorker : public IFtpProgress {
public:
    CTaskWorker(const TaskConfig& stConfig, const CString& strRoot, CLogger& loggerFile, UINT unBackupKeepDays);
    ~CTaskWorker();
    bool Start(); void RequestStop(); bool PollStopped();
    bool Running() const { return m_hThread != NULL; }
    const TaskConfig& Config() const { return m_stConfig; }
    TaskStatus Status(); void Progress(ULONGLONG ullSent);
    DWORD Beat() const { return (DWORD)InterlockedCompareExchange((LONG*)&m_lBeat, 0, 0); }
private:
    static unsigned __stdcall ThreadProc(void* pContext); void Run();
    void State(int nState, const CString& strText); void Error(const CString& strText);
    int UploadOne(CFtpClient& ftpClientSession, CUploadHistory& historyUpload, const PendingFile& stFile, CString& strRemoteDir, CString& strFinalName);
    bool PostProcess(const PendingFile& stFile); void CleanupBackup();
    TaskConfig m_stConfig; CString m_strRoot; CLogger& m_loggerFile; UINT m_unBackupKeepDays;
    HANDLE m_hThread, m_hStop; CCritSec m_csStatusLock; TaskStatus m_stStatus;
    volatile LONG m_lBeat;
    CTaskWorker(const CTaskWorker&); CTaskWorker& operator=(const CTaskWorker&);
};
