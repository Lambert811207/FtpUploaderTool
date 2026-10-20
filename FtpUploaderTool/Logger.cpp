#include "stdafx.h"
#include "Logger.h"
CLogger::CLogger() : m_hThread(NULL), m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)), m_hWake(CreateEvent(NULL, FALSE, FALSE, NULL)), m_unKeepDays(30), m_lDiskError(0), m_unDropped(0) {}
CLogger::~CLogger() { Stop(); CloseHandle(m_hStop); CloseHandle(m_hWake); }
bool CLogger::Start(const CString& strRoot, UINT unKeepDays) {
    m_strRoot = strRoot; m_unKeepDays = unKeepDays; ResetEvent(m_hStop);
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL); return m_hThread != NULL;
}
void CLogger::Stop() {
    if (!m_hThread) return; SetEvent(m_hStop); SetEvent(m_hWake);
    WaitForSingleObject(m_hThread, INFINITE); CloseHandle(m_hThread); m_hThread = NULL;
}
void CLogger::Push(const LogItem& stItem) {
    CAutoLock lockGuard(m_csLock);
    if (m_deqFileQueue.size() >= 50000) { m_deqFileQueue.pop_front(); ++m_unDropped; }
    m_deqFileQueue.push_back(stItem);
    if (!stItem.m_bCsv) { if (m_deqUiQueue.size() >= 5000) m_deqUiQueue.pop_front(); m_deqUiQueue.push_back(stItem); }
    SetEvent(m_hWake);
}
void CLogger::Write(const TaskConfig* pConfig, int nLevel, const CString& strText) {
    LogItem stItem; stItem.m_strTime = TimeText(); stItem.m_strDate = stItem.m_strTime.Left(10); stItem.m_nLevel = nLevel; stItem.m_strText = strText;
    if (pConfig) { stItem.m_strId = pConfig->m_strId; stItem.m_strName = pConfig->m_strName; } else stItem.m_strName = L"系统";
    Push(stItem);
}
static CString Csv(const CString& strValue) { CString strS(strValue); strS.Replace(L"\"", L"\"\""); return L"\"" + strS + L"\""; }
void CLogger::Record(const TaskConfig& stConfig, const CString& strLocal, const CString& strRemoteDir, const CString& strRemoteName,
    ULONGLONG ullBytes, DWORD dwMs, const CString& strResult, const CString& strError) {
    LogItem stItem; stItem.m_strTime = TimeText(); stItem.m_strDate = stItem.m_strTime.Left(10); stItem.m_strId = stConfig.m_strId; stItem.m_strName = stConfig.m_strName; stItem.m_bCsv = true;
    CString strSize, strDuration, strSpeed; strSize.Format(L"%I64u", ullBytes); strDuration.Format(L"%lu", dwMs); strSpeed.Format(L"%.1f", dwMs ? ullBytes / 1024.0 / (dwMs / 1000.0) : 0.0);
    stItem.m_strText = Csv(stItem.m_strTime) + L"," + Csv(stConfig.m_strName) + L"," + Csv(strLocal) + L"," + Csv(strRemoteDir) + L"," + Csv(strRemoteName) + L"," + Csv(strSize) + L"," + Csv(strDuration) + L"," + Csv(strSpeed) + L"," + Csv(strResult) + L"," + Csv(strError);
    Push(stItem);
}
void CLogger::PopUi(std::vector<LogItem>& vecItems) { CAutoLock lockGuard(m_csLock); vecItems.assign(m_deqUiQueue.begin(), m_deqUiQueue.end()); m_deqUiQueue.clear(); }
unsigned __stdcall CLogger::ThreadProc(void* pContext) { ((CLogger*)pContext)->Run(); return 0; }
void CLogger::Run() {
    CString strLastClean; DWORD dwFailedTick = 0; bool bDiskFailed = false;
    for (;;) {
        WaitForSingleObject(m_hWake, 200);
        std::deque<LogItem> deqBatch; UINT unDropped;
        { CAutoLock lockGuard(m_csLock); deqBatch.swap(m_deqFileQueue); unDropped = m_unDropped; m_unDropped = 0; }
        for (size_t nIndex = 0; nIndex < deqBatch.size(); ++nIndex) {
            const LogItem& stItem = deqBatch[nIndex];
            if (bDiskFailed && !TickElapsed(dwFailedTick, 60000)) { ++unDropped; continue; }
            CString strDirectory = JoinPath(m_strRoot, stItem.m_strDate);
            CString strBase = stItem.m_strId.IsEmpty() ? CString(L"System") : SafeFileName(stItem.m_strName + L"_" + stItem.m_strId.Left(8));
            CString strPath = JoinPath(strDirectory, strBase + (stItem.m_bCsv ? L".csv" : L".log"));
            bool bOk = MakeDirs(strDirectory);
            WIN32_FILE_ATTRIBUTE_DATA stAttributes;
            if (bOk && GetFileAttributesExW(strPath, GetFileExInfoStandard, &stAttributes) && (stAttributes.nFileSizeHigh || stAttributes.nFileSizeLow > 20 * 1024 * 1024)) {
                CString strRotated; UINT unSuffix = 1;
                do { strRotated.Format(L"%s.%u", (LPCWSTR)strPath, unSuffix++); } while (PathFileExistsW(strRotated));
                bOk = MoveFileExW(strPath, strRotated, MOVEFILE_WRITE_THROUGH) != FALSE;
            }
            CAutoHandle hFile(bOk ? CreateFileW(strPath, FILE_APPEND_DATA | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE);
            bOk = bOk && hFile.Valid();
            if (bOk) {
                LARGE_INTEGER stSize; GetFileSizeEx(hFile.Get(), &stSize);
                if (!stSize.QuadPart) {
                    static const BYTE szBom[] = {0xEF, 0xBB, 0xBF}; bOk = WriteBytes(hFile.Get(), szBom, sizeof(szBom));
                    if (bOk && stItem.m_bCsv) { CStringA strAHeader = ToBytes(L"时间,任务,本地文件,远程目录,远程文件名,大小(字节),耗时(ms),速度(KB/s),结果,说明\r\n"); bOk = WriteBytes(hFile.Get(), (LPCSTR)strAHeader, strAHeader.GetLength()); }
                }
                if (bOk && unDropped && !stItem.m_bCsv) {
                    CString strLost; strLost.Format(L"%s [WARN] 丢弃了 %u 条日志\r\n", (LPCWSTR)stItem.m_strTime, unDropped);
                    CStringA strABytes = ToBytes(strLost); bOk = WriteBytes(hFile.Get(), (LPCSTR)strABytes, strABytes.GetLength()); unDropped = 0;
                }
                LPCWSTR arrLevels[] = {L"DEBUG", L"INFO", L"WARN", L"ERROR"};
                CString strText = stItem.m_bCsv ? stItem.m_strText : stItem.m_strTime + L" [" + arrLevels[max(0, min(3, stItem.m_nLevel))] + L"] " + stItem.m_strText;
                CStringA strABytes = ToBytes(strText + L"\r\n"); if (bOk) bOk = WriteBytes(hFile.Get(), (LPCSTR)strABytes, strABytes.GetLength());
                if (bOk && stItem.m_nLevel == 3) bOk = FlushFileBuffers(hFile.Get()) != FALSE;
            }
            if (!bOk) { bDiskFailed = true; dwFailedTick = GetTickCount(); ++unDropped; InterlockedExchange(&m_lDiskError, 1); }
            else { bDiskFailed = false; InterlockedExchange(&m_lDiskError, 0); }
            if (bOk && strLastClean != stItem.m_strDate) {
                strLastClean = stItem.m_strDate; WIN32_FIND_DATAW stFd; HANDLE hHandle = FindFirstFileW(JoinPath(m_strRoot, L"*"), &stFd); CFindHandle hFindFind(hHandle);
                if (hHandle != INVALID_HANDLE_VALUE) do {
                    if (!(stFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || stFd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                    int nY, nM, nD; if (wcslen(stFd.cFileName) != 10 || swscanf_s(stFd.cFileName, L"%d-%d-%d", &nY, &nM, &nD) != 3) continue;
                    SYSTEMTIME stTime = {0}; stTime.wYear = (WORD)nY; stTime.wMonth = (WORD)nM; stTime.wDay = (WORD)nD; FILETIME stFileTime;
                    if (!SystemTimeToFileTime(&stTime, &stFileTime)) continue;
                    FILETIME stNow; GetSystemTimeAsFileTime(&stNow); ULARGE_INTEGER stA, stB; stA.LowPart = stNow.dwLowDateTime; stA.HighPart = stNow.dwHighDateTime; stB.LowPart = stFileTime.dwLowDateTime; stB.HighPart = stFileTime.dwHighDateTime;
                    if (stA.QuadPart > stB.QuadPart && (stA.QuadPart - stB.QuadPart) / 864000000000ULL >= m_unKeepDays) DeleteTree(JoinPath(m_strRoot, stFd.cFileName), m_hStop);
                } while (FindNextFileW(hHandle, &stFd));
            }
        }
        if (unDropped) { CAutoLock lockGuard(m_csLock); m_unDropped += unDropped; }
        if (Stopped(m_hStop)) { CAutoLock lockGuard(m_csLock); if (m_deqFileQueue.empty()) break; }
    }
}
