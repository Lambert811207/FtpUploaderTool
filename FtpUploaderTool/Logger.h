#pragma once
#include "Config.h"
struct LogItem {
    CString m_strTime, m_strDate, m_strId, m_strName, m_strText; int m_nLevel; bool m_bCsv;
    LogItem() : m_nLevel(1), m_bCsv(false) {}
};
class CLogger {
public:
    CLogger(); ~CLogger();
    bool Start(const CString& strRoot, UINT unKeepDays);
    void Stop();
    void Write(const TaskConfig* pConfig, int nLevel, const CString& strText);
    void Record(const TaskConfig& stConfig, const CString& strLocal, const CString& strRemoteDir, const CString& strRemoteName,
        ULONGLONG ullBytes, DWORD dwMs, const CString& strResult, const CString& strError);
    void PopUi(std::vector<LogItem>& vecItems);
    bool DiskError() const { return InterlockedCompareExchange((LONG*)&m_lDiskError, 0, 0) != 0; }
private:
    static unsigned __stdcall ThreadProc(void* pContext); void Run(); void Push(const LogItem& stItem);
    CCritSec m_csLock; std::deque<LogItem> m_deqFileQueue, m_deqUiQueue; // Bounded: 50000 / 5000.
    HANDLE m_hThread, m_hStop, m_hWake; CString m_strRoot; UINT m_unKeepDays;
    volatile LONG m_lDiskError; UINT m_unDropped;
};
