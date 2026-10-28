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

// 删除最旧归档并从大编号向后移动，backup1 始终是最新归档。
bool CLogger::Archive(const CString& strPath, UINT unArchiveCount)
{
    CString strOldest = ArchivePath(strPath, unArchiveCount);
    if (PathFileExistsW(strOldest) && !DeleteFileW(strOldest))
        return false;
    for (UINT unIndex = unArchiveCount; unIndex > 1; --unIndex)
    {
        CString strSource = ArchivePath(strPath, unIndex - 1);
        CString strTarget = ArchivePath(strPath, unIndex);
        if (PathFileExistsW(strSource) &&
            !MoveFileExW(strSource, strTarget, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return false;
    }
    CString strNewest = ArchivePath(strPath, 1);
    return MoveFileExW(strPath, strNewest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
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

// 系统与各任务的 TXT 分别按大小轮转，任务文件名采用持久化编号。
bool CLogger::WriteItem(const LogItem& stItem, UINT& unDropped, const GlobalConfig& stConfig)
{
    if (!MakeDirs(m_strRoot))
        return false;
    CString strBase(L"SystemLog");
    if (!stItem.m_strId.IsEmpty())
        strBase.Format(L"Task%uLog", stItem.m_unTaskNumber);
    CString strPath = JoinPath(m_strRoot, strBase + L".txt");
    CStringA strALost;
    if (unDropped)
    {
        CString strLost;
        strLost.Format(L"%s [WARN] 丢弃了 %u 条日志\r\n", (LPCWSTR)stItem.m_strTime, unDropped);
        strALost = ToBytes(strLost);
    }
    LPCWSTR arrLevels[] = {L"DEBUG", L"INFO", L"WARN", L"ERROR"};
    CString strText = stItem.m_strTime + L" [" + arrLevels[max(0, min(3, stItem.m_nLevel))] + L"] " +
                      (stItem.m_strId.IsEmpty() ? CString() : L"[" + stItem.m_strName + L"] ") + stItem.m_strText;
    CStringA strABytes = ToBytes(strText + L"\r\n");
    WIN32_FILE_ATTRIBUTE_DATA stAttributes;
    bool bExists = GetFileAttributesExW(strPath, GetFileExInfoStandard, &stAttributes) != FALSE;
    ULONGLONG ullSize = bExists ? (((ULONGLONG)stAttributes.nFileSizeHigh << 32) | stAttributes.nFileSizeLow) : 0;
    ULONGLONG ullLimit = (ULONGLONG)stConfig.m_unLogMaxSizeMb * 1024 * 1024;
    ULONGLONG ullNext = strALost.GetLength() + strABytes.GetLength();
    if (!bExists || !ullSize)
        ullNext += 3;
    if (bExists && ullSize && ullSize + ullNext > ullLimit)
    {
        if (!Archive(strPath, stConfig.m_unLogArchiveCount))
            return false;
    }
    CAutoHandle hFile(CreateFileW(strPath, FILE_APPEND_DATA | GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (!hFile.Valid())
        return false;
    LARGE_INTEGER stSize;
    if (!GetFileSizeEx(hFile.Get(), &stSize))
        return false;
    bool bOk = true;
    if (!stSize.QuadPart)
    {
        static const BYTE szBom[] = {0xEF, 0xBB, 0xBF};
        bOk = WriteBytes(hFile.Get(), szBom, sizeof(szBom));
    }
    if (bOk && !strALost.IsEmpty())
    {
        bOk = WriteBytes(hFile.Get(), (LPCSTR)strALost, strALost.GetLength());
        if (bOk)
            unDropped = 0;
    }
    if (bOk)
        bOk = WriteBytes(hFile.Get(), (LPCSTR)strABytes, strABytes.GetLength());
    if (bOk && stItem.m_nLevel == 3)
        bOk = FlushFileBuffers(hFile.Get()) != FALSE;
    return bOk;
}

// 批量取队列后在锁外写盘，收到停止请求后继续排空已入队记录。
void CLogger::Run()
{
    DWORD dwFailedTick = 0;
    bool bDiskFailed = false;
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
        for (size_t nIndex = 0; nIndex < deqBatch.size(); ++nIndex)
        {
            const LogItem& stItem = deqBatch[nIndex];
            // 磁盘失败后一段时间内不反复尝试写入，累计丢失数量供恢复后提示。
            if (bDiskFailed && !TickElapsed(dwFailedTick, 60000))
            {
                ++unDropped;
                continue;
            }
            bool bOk = WriteItem(stItem, unDropped, stConfig);
            if (!bOk)
            {
                bDiskFailed = true;
                dwFailedTick = GetTickCount();
                ++unDropped;
            }
            else
            {
                bDiskFailed = false;
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
