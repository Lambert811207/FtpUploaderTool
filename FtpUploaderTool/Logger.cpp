#include "stdafx.h"
#include "Logger.h"

CLogger::CLogger()
    : m_hThread(NULL),
      m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)),
      m_hWake(CreateEvent(NULL, FALSE, FALSE, NULL)),
      m_bSettingsChanged(true),
      m_unDropped(0)
{
}

CLogger::~CLogger()
{
    Stop();
    CloseHandle(m_hStop);
    CloseHandle(m_hWake);
}

bool CLogger::Start(const CString& strRoot, const GlobalConfig& stConfig)
{
    if (m_hThread || !m_hStop || !m_hWake)
        return false;
    m_strRoot = strRoot;
    if (!MakeDirs(m_strRoot))
        return false;
    Configure(stConfig);
    ResetEvent(m_hStop);
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL);
    return m_hThread != NULL;
}

void CLogger::Configure(const GlobalConfig& stConfig)
{
    CAutoLock lockGuard(m_csLock);
    m_stConfig = stConfig;
    m_bSettingsChanged = true;
    SetEvent(m_hWake);
}

void CLogger::RequestStop()
{
    SetEvent(m_hStop);
    SetEvent(m_hWake);
}

bool CLogger::PollStopped()
{
    if (!m_hThread)
        return true;
    if (WaitForSingleObject(m_hThread, 0) != WAIT_OBJECT_0)
        return false;
    CloseHandle(m_hThread);
    m_hThread = NULL;
    return true;
}

void CLogger::Stop()
{
    RequestStop();
    if (m_hThread)
        WaitForSingleObject(m_hThread, INFINITE);
    PollStopped();
}

// 文件日志与界面日志分别限长，生产者只入队，不执行同步磁盘写入。
void CLogger::Push(const LogItem& stItem)
{
    CAutoLock lockGuard(m_csLock);
    if (m_deqFileQueue.size() >= 50000)
    {
        m_deqFileQueue.pop_front();
        ++m_unDropped;
    }
    m_deqFileQueue.push_back(stItem);
    if (m_deqUiQueue.size() >= 5000)
        m_deqUiQueue.pop_front();
    m_deqUiQueue.push_back(stItem);
    SetEvent(m_hWake);
}

void CLogger::Write(const TaskConfig* pConfig, int nLevel, const CString& strText)
{
    LogItem stItem;
    stItem.m_strTime = TimeText();
    stItem.m_nLevel = nLevel;
    stItem.m_strText = strText;
    if (pConfig)
    {
        stItem.m_strId = pConfig->m_strId;
        stItem.m_unTaskNumber = pConfig->m_unTaskNumber;
        stItem.m_strName = pConfig->m_strName;
    }
    else
        stItem.m_strName = L"系统";
    Push(stItem);
}

void CLogger::Operation(const CString& strAction, const CString& strResult, const TaskConfig* pConfig, int nLevel)
{
    CString strText = L"[操作] " + strAction;
    if (pConfig)
    {
        CString strTask;
        strTask.Format(L"；任务=Task%u/%s；ID=%s", pConfig->m_unTaskNumber,
                       (LPCWSTR)pConfig->m_strName, (LPCWSTR)pConfig->m_strId);
        strText += strTask;
    }
    strText += L"；结果=" + strResult;
    strText.Replace(L'\r', L' ');
    strText.Replace(L'\n', L' ');
    Write(NULL, nLevel, strText);
}

void CLogger::Record(const TaskConfig& stConfig, const CString& strLocal, const CString& strRemoteDir,
                     const CString& strRemoteName, ULONGLONG ullBytes, DWORD dwMs, const CString& strResult,
                     const CString& strError)
{
    LPCWSTR pszResult = strResult == L"OK" ? L"成功" : strResult == L"RETRY" ? L"重试" : L"失败";
    CString strText;
    strText.Format(L"[上传记录] 结果=%s（%s）；本地文件=\"%s\"；服务器目录=\"%s\"；服务器文件名=\"%s\"；文件大小=%I64u 字节；耗时=%lu 毫秒",
                   pszResult, (LPCWSTR)strResult, (LPCWSTR)strLocal, (LPCWSTR)strRemoteDir,
                   (LPCWSTR)strRemoteName, ullBytes, dwMs);
    if (strResult == L"OK")
    {
        CString strSpeed;
        strSpeed.Format(L"；平均速度=%.1f KB/s", dwMs ? ullBytes / 1024.0 / (dwMs / 1000.0) : 0.0);
        strText += strSpeed;
    }
    if (!strError.IsEmpty())
        strText += L"；原因=" + strError;
    // 一次上传尝试保持为一行，错误应答或路径中的换行不会拆散日志。
    strText.Replace(L'\r', L' ');
    strText.Replace(L'\n', L' ');
    Write(&stConfig, strResult == L"OK" ? 1 : strResult == L"RETRY" ? 2 : 3, strText);
}

// 队列交换后由界面在锁外消费，缩短占用日志锁的时间。
void CLogger::PopUi(std::deque<LogItem>& deqItems)
{
    deqItems.clear();
    CAutoLock lockGuard(m_csLock);
    deqItems.swap(m_deqUiQueue);
}

unsigned __stdcall CLogger::ThreadProc(void* pContext)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    ((CLogger*)pContext)->Run();
    return 0;
}

// 保留原扩展名，生成 Task1Log.backup1.txt 形式的归档路径。
static CString ArchivePath(const CString& strPath, UINT unNumber)
{
    int nExtension = strPath.ReverseFind(L'.');
    CString strArchive;
    strArchive.Format(L"%s.backup%u%s", (LPCWSTR)strPath.Left(nExtension), unNumber,
                      (LPCWSTR)strPath.Mid(nExtension));
    return strArchive;
}

// 从 backup1 起寻找第一个空位，只把空位之前的归档后移一格；全部占满时才删除最旧归档。
// 中途改名失败会留下空位，下次从空位继续，避免重复删除尚未轮到的归档。
bool CLogger::Archive(const CString& strPath, UINT unArchiveCount)
{
    UINT unGap = 1;
    while (unGap <= unArchiveCount && PathFileExistsW(ArchivePath(strPath, unGap)))
        ++unGap;
    if (unGap > unArchiveCount)
    {
        if (!DeleteFileW(ArchivePath(strPath, unArchiveCount)))
            return false;
        unGap = unArchiveCount;
    }
    for (UINT unIndex = unGap; unIndex > 1; --unIndex)
    {
        if (!MoveFileExW(ArchivePath(strPath, unIndex - 1), ArchivePath(strPath, unIndex),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return false;
    }
    return MoveFileExW(strPath, ArchivePath(strPath, 1), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

// 设置减少归档数量后清除超额文件，只处理约定格式的日志归档。
bool CLogger::CleanupArchives(const GlobalConfig& stConfig)
{
    WIN32_FIND_DATAW stFile;
    HANDLE hFind = FindFirstFileW(JoinPath(m_strRoot, L"*Log.backup*.txt"), &stFile);
    if (hFind == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    CFindHandle hHandle(hFind);
    bool bOk = true;
    do
    {
        if (stFile.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        CString strName(stFile.cFileName);
        int nExtension = strName.ReverseFind(L'.');
        int nSuffix = strName.Find(L".backup");
        if (nSuffix < 0 || nExtension <= nSuffix + 7 ||
            strName.Mid(nExtension) != L".txt")
            continue;
        CString strNumber = strName.Mid(nSuffix + 7, nExtension - nSuffix - 7);
        WCHAR* pEnd = NULL;
        unsigned long ulIndex = wcstoul(strNumber, &pEnd, 10);
        if (*pEnd || ulIndex == 0)
            continue;
        if (ulIndex > stConfig.m_unLogArchiveCount && !DeleteFileW(JoinPath(m_strRoot, strName)))
            bOk = false;
    } while (FindNextFileW(hFind, &stFile));
    return bOk;
}

// 打开当前日志用于追加，空文件先写 UTF-8 BOM；目录缺失时才创建目录。
bool CLogger::OpenLog(const CString& strPath, CAutoHandle& hFile, ULONGLONG& ullSize)
{
    for (int nAttempt = 0; nAttempt < 2; ++nAttempt)
    {
        HANDLE hHandle = CreateFileW(strPath, FILE_APPEND_DATA | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD dwError = GetLastError();
        hFile.Attach(hHandle);
        if (hFile.Valid())
            break;
        if (nAttempt || dwError != ERROR_PATH_NOT_FOUND || !MakeDirs(m_strRoot))
            return false;
    }
    LARGE_INTEGER stSize;
    if (!GetFileSizeEx(hFile.Get(), &stSize))
        return false;
    ullSize = (ULONGLONG)stSize.QuadPart;
    if (!ullSize)
    {
        static const BYTE szBom[] = {0xEF, 0xBB, 0xBF};
        if (!WriteBytes(hFile.Get(), szBom, sizeof(szBom)))
            return false;
        ullSize = sizeof(szBom);
    }
    return true;
}

// 一个文件每批只打开一次；归档失败时继续追加到当前文件（暂时超过大小上限），每分钟最多重试一次归档。
bool CLogger::WriteLines(const CString& strPath, const std::vector<CStringA>& vecLines, bool bFlush,
                         const GlobalConfig& stConfig, FileState& stState, UINT& unDropped)
{
    CAutoHandle hFile;
    ULONGLONG ullSize = 0;
    if (!OpenLog(strPath, hFile, ullSize))
        return false;
    ULONGLONG ullLimit = (ULONGLONG)stConfig.m_unLogMaxSizeMb * 1024 * 1024;
    CStringA strAPending;
    if (unDropped)
    {
        CString strLost;
        strLost.Format(L"%s [WARN] 丢弃了 %u 条日志\r\n", (LPCWSTR)TimeText(), unDropped);
        strAPending = ToBytes(strLost);
    }
    for (size_t nIndex = 0; nIndex < vecLines.size(); ++nIndex)
    {
        const CStringA& strALine = vecLines[nIndex];
        ULONGLONG ullPending = (ULONGLONG)strAPending.GetLength();
        // 只含 BOM 的文件不归档，避免产生空的 backup 文件。
        if (ullSize + ullPending > 3 && ullSize + ullPending + strALine.GetLength() > ullLimit &&
            (!stState.m_bArchiveFailed || TickElapsed(stState.m_dwArchiveFailTick, 60000)))
        {
            if (!strAPending.IsEmpty())
            {
                if (!WriteBytes(hFile.Get(), (LPCSTR)strAPending, strAPending.GetLength()))
                    return false;
                ullSize += ullPending;
                strAPending.Empty();
                unDropped = 0;
            }
            if (bFlush && !FlushFileBuffers(hFile.Get()))
                return false;
            hFile.Close();
            if (Archive(strPath, stConfig.m_unLogArchiveCount))
                stState.m_bArchiveFailed = false;
            else
            {
                DWORD dwError = GetLastError();
                stState.m_bArchiveFailed = true;
                stState.m_dwArchiveFailTick = GetTickCount();
                CString strWarn;
                strWarn.Format(L"%s [WARN] 日志归档失败，继续写入当前文件，1 分钟后重试：%s\r\n",
                               (LPCWSTR)TimeText(), (LPCWSTR)WinError(dwError));
                strAPending = ToBytes(strWarn);
            }
            if (!OpenLog(strPath, hFile, ullSize))
                return false;
        }
        strAPending += strALine;
    }
    if (!strAPending.IsEmpty() && !WriteBytes(hFile.Get(), (LPCSTR)strAPending, strAPending.GetLength()))
        return false;
    unDropped = 0;
    return !bFlush || FlushFileBuffers(hFile.Get()) != FALSE;
}

// 系统日志使用 SystemLog.txt，任务日志文件名采用持久化编号。
static CString LogFileName(const LogItem& stItem)
{
    CString strBase(L"SystemLog");
    if (!stItem.m_strId.IsEmpty())
        strBase.Format(L"Task%uLog", stItem.m_unTaskNumber);
    return strBase + L".txt";
}

static CStringA LogLine(const LogItem& stItem)
{
    LPCWSTR arrLevels[] = {L"DEBUG", L"INFO", L"WARN", L"ERROR"};
    CString strText = stItem.m_strTime + L" [" + arrLevels[max(0, min(3, stItem.m_nLevel))] + L"] " +
                      (stItem.m_strId.IsEmpty() ? CString() : L"[" + stItem.m_strName + L"] ") + stItem.m_strText;
    return ToBytes(strText + L"\r\n");
}

// 同一文件的一批日志，保持入队顺序。
struct LogChunk
{
    std::vector<CStringA> m_vecLines;
    bool m_bFlush;

    LogChunk()
        : m_bFlush(false)
    {
    }
};

// 批量取队列后在锁外按文件合并写盘，收到停止请求后继续排空已入队记录。
void CLogger::Run()
{
    // 故障按文件区分，一个文件不可写不影响其他任务和系统日志。
    std::map<CString, FileState> mapStates;
    for (;;)
    {
        WaitForSingleObject(m_hWake, 200);
        std::deque<LogItem> deqBatch;
        UINT unDropped;
        GlobalConfig stConfig;
        bool bSettingsChanged;
        {
            CAutoLock lockGuard(m_csLock);
            deqBatch.swap(m_deqFileQueue);
            unDropped = m_unDropped;
            m_unDropped = 0;
            stConfig = m_stConfig;
            bSettingsChanged = m_bSettingsChanged;
            m_bSettingsChanged = false;
        }
        if (bSettingsChanged)
            CleanupArchives(stConfig);
        std::map<CString, LogChunk> mapChunks;
        for (size_t nIndex = 0; nIndex < deqBatch.size(); ++nIndex)
        {
            LogChunk& stChunk = mapChunks[JoinPath(m_strRoot, LogFileName(deqBatch[nIndex]))];
            stChunk.m_vecLines.push_back(LogLine(deqBatch[nIndex]));
            if (deqBatch[nIndex].m_nLevel == 3)
                stChunk.m_bFlush = true;
        }
        // 队列溢出及写入失败的丢弃数量统一在系统日志中提示，提示写入成功后才清零。
        CString strSystemPath = JoinPath(m_strRoot, L"SystemLog.txt");
        if (unDropped)
            mapChunks[strSystemPath];
        for (std::map<CString, LogChunk>::iterator itI = mapChunks.begin(); itI != mapChunks.end(); ++itI)
        {
            FileState& stState = mapStates[itI->first];
            UINT unLines = (UINT)itI->second.m_vecLines.size();
            // 写入失败后该文件 60 秒内不再尝试，其他文件照常写入。
            if (stState.m_bWriteFailed && !TickElapsed(stState.m_dwWriteFailTick, 60000))
            {
                unDropped += unLines;
                continue;
            }
            UINT unNotice = itI->first == strSystemPath ? unDropped : 0;
            if (WriteLines(itI->first, itI->second.m_vecLines, itI->second.m_bFlush, stConfig, stState, unNotice))
            {
                stState.m_bWriteFailed = false;
                if (itI->first == strSystemPath)
                    unDropped = 0;
            }
            else
            {
                stState.m_bWriteFailed = true;
                stState.m_dwWriteFailTick = GetTickCount();
                if (itI->first == strSystemPath)
                    unDropped = unNotice;
                unDropped += unLines;
            }
        }
        if (unDropped)
        {
            CAutoLock lockGuard(m_csLock);
            m_unDropped += unDropped;
        }
        if (Stopped(m_hStop))
        {
            CAutoLock lockGuard(m_csLock);
            if (m_deqFileQueue.empty())
                break;
        }
    }
}
