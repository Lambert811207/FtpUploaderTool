#include "stdafx.h"
#include "RemoteTemp.h"

static CCritSec m_csTempLock;
// 进程内正在使用的精确临时路径，防止清理线程删除正在上传的文件。
static std::set<CString> m_setActiveTemps;

static CString TempKey(const TaskConfig& stConfig, const CString& strRemotePath)
{
    CString strHost(stConfig.m_strHost);
    strHost.MakeLower();
    CString strKey;
    strKey.Format(L"%s\n%u\n%s\n%s", (LPCWSTR)strHost, stConfig.m_unPort,
                  (LPCWSTR)stConfig.m_strUser, (LPCWSTR)strRemotePath);
    return strKey;
}

static bool ClaimTemp(const CString& strKey)
{
    CAutoLock lockGuard(m_csTempLock);
    return m_setActiveTemps.insert(strKey).second;
}

static void ReleaseTemp(const CString& strKey)
{
    CAutoLock lockGuard(m_csTempLock);
    m_setActiveTemps.erase(strKey);
}

static void CALLBACK StopCleanup(void* pContext, BOOLEAN)
{
    SetEvent((HANDLE)pContext);
}

// 使用独立 FTP 会话及 2 秒停止预算清理，失败时保留持久化记录。
static bool DeleteTemp(const TaskConfig& stConfig, const CString& strRemotePath,
                       const CString& strJournal, CLogger* pLogger)
{
    CAutoHandle hStop(CreateEvent(NULL, TRUE, FALSE, NULL));
    HANDLE hTimer = NULL;
    if (!hStop.Valid() || !CreateTimerQueueTimer(&hTimer, NULL, StopCleanup, hStop.Get(), 2000, 0,
                                                WT_EXECUTEONLYONCE | WT_EXECUTEINTIMERTHREAD))
        return false;
    int nError = FE_SESSION;
    try
    {
        CFtpClient ftpCleanup(stConfig, hStop.Get());
        nError = ftpCleanup.Connect();
        if (!nError)
            nError = ftpCleanup.DeleteTemporary(strRemotePath);
    }
    catch (CException* pException)
    {
        pException->Delete();
        nError = FE_SESSION;
    }
    catch (const std::exception&)
    {
        nError = FE_SESSION;
    }
    DeleteTimerQueueTimer(NULL, hTimer, INVALID_HANDLE_VALUE);
    bool bRemoved = !nError && DeleteFileW(strJournal) != FALSE;
    if (pLogger)
        pLogger->Operation(L"清理远程临时文件", strRemotePath + (bRemoved ? L"；已清理" : L"；未完成，保留记录等待重试"),
                           &stConfig, bRemoved ? 1 : 2);
    return bRemoved;
}

CRemoteTempFile::CRemoteTempFile(const CString& strRoot, CFtpClient& ftpSession, CLogger* pLogger)
    : m_strRoot(JoinPath(JoinPath(strRoot, L"Data"), L"RemoteTemp")),
      m_ftpSession(ftpSession),
      m_pLogger(pLogger)
{
}

// 失败或取消路径先断开原会话，再尝试清理；失败记录留给后台重试。
CRemoteTempFile::~CRemoteTempFile()
{
    if (!m_strJournal.IsEmpty())
    {
        m_ftpSession.Disconnect();
        DeleteTemp(m_stConfig, m_strRemotePath, m_strJournal, m_pLogger);
    }
    if (!m_strKey.IsEmpty())
        ReleaseTemp(m_strKey);
}

// 记录服务器、凭据和完整临时路径，后续清理不依赖源文件或原任务。
bool CRemoteTempFile::Begin(const TaskConfig& stConfig, const CString& strDirectory, const CString& strName)
{
    m_stConfig = stConfig;
    m_strRemotePath = strDirectory + (strDirectory.Right(1) == L"/" ? L"" : L"/") + strName;
    CString strKey = TempKey(stConfig, m_strRemotePath);
    if (!ClaimTemp(strKey))
        return false;
    m_strKey = strKey;
    CString strText;
    strText.Format(L"\xFEFF[RemoteTemp]\r\nVersion=1\r\nHost=%s\r\nPort=%u\r\nUser=%s\r\n"
                   L"PasswordPlain=\"%s\"\r\nEncoding=%d\r\nPassive=%d\r\nTaskName=%s\r\nTaskId=%s\r\nTaskNumber=%u\r\nPath=%s\r\n",
                   (LPCWSTR)stConfig.m_strHost, stConfig.m_unPort, (LPCWSTR)stConfig.m_strUser,
                   (LPCWSTR)stConfig.m_strPassword, stConfig.m_nEncoding, stConfig.m_bPassive ? 1 : 0,
                   (LPCWSTR)stConfig.m_strName,
                   (LPCWSTR)stConfig.m_strId, stConfig.m_unTaskNumber, (LPCWSTR)m_strRemotePath);
    CString strJournal = JoinPath(m_strRoot, NewId() + L".ini");
    CStringA strABytes((const char*)(LPCWSTR)strText, strText.GetLength() * sizeof(WCHAR));
    if (!MakeDirs(m_strRoot) || !AtomicWrite(strJournal, strABytes))
    {
        if (m_pLogger)
            m_pLogger->Operation(L"记录远程临时文件", L"失败，已取消本次上传", &stConfig, 3);
        return false;
    }
    m_strJournal = strJournal;
    return true;
}

void CRemoteTempFile::Commit()
{
    if (!m_strJournal.IsEmpty())
    {
        DeleteFileW(m_strJournal);
        m_strJournal.Empty();
    }
}

static CString ReadTempValue(const CString& strJournal, LPCWSTR pszKey)
{
    WCHAR szBuffer[32768] = {0};
    GetPrivateProfileStringW(L"RemoteTemp", pszKey, L"", szBuffer, _countof(szBuffer), strJournal);
    return CString(szBuffer);
}

CRemoteTempCleanup::CRemoteTempCleanup()
    : m_pLogger(NULL),
      m_hThread(NULL),
      m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL))
{
}

CRemoteTempCleanup::~CRemoteTempCleanup()
{
    Stop();
    CloseHandle(m_hStop);
}

bool CRemoteTempCleanup::Start(const CString& strRoot, CLogger& loggerFile)
{
    if (m_hThread || !m_hStop)
        return false;
    m_strRoot = JoinPath(JoinPath(strRoot, L"Data"), L"RemoteTemp");
    m_pLogger = &loggerFile;
    ResetEvent(m_hStop);
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL);
    return m_hThread != NULL;
}

void CRemoteTempCleanup::RequestStop()
{
    SetEvent(m_hStop);
}

bool CRemoteTempCleanup::PollStopped()
{
    if (!m_hThread)
        return true;
    if (WaitForSingleObject(m_hThread, 0) != WAIT_OBJECT_0)
        return false;
    CloseHandle(m_hThread);
    m_hThread = NULL;
    return true;
}

void CRemoteTempCleanup::Stop()
{
    RequestStop();
    if (m_hThread)
        WaitForSingleObject(m_hThread, INFINITE);
    PollStopped();
}

// 每轮结束等待 10 秒，停止事件可提前唤醒等待。
unsigned __stdcall CRemoteTempCleanup::ThreadProc(void* pContext)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    CRemoteTempCleanup* pSelf = (CRemoteTempCleanup*)pContext;
    do
    {
        try
        {
            pSelf->Run();
        }
        catch (CException* pException)
        {
            pException->Delete();
        }
        catch (const std::exception&)
        {
        }
    } while (WaitForSingleObject(pSelf->m_hStop, 10000) == WAIT_TIMEOUT);
    return 0;
}

// 读取待清理记录并校验精确路径，只处理没有被上传流程占用的项目。
void CRemoteTempCleanup::Run()
{
    WIN32_FIND_DATAW stFile;
    HANDLE hSearch = FindFirstFileW(JoinPath(m_strRoot, L"*.ini"), &stFile);
    if (hSearch == INVALID_HANDLE_VALUE)
        return;
    CFindHandle hFind(hSearch);
    do
    {
        if (Stopped(m_hStop))
            break;
        if (stFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        CString strJournal = JoinPath(m_strRoot, stFile.cFileName);
        if (ReadTempValue(strJournal, L"Version") != L"1")
            continue;
        TaskConfig stConfig;
        stConfig.m_strHost = ReadTempValue(strJournal, L"Host");
        stConfig.m_unPort = (UINT)_wtoi(ReadTempValue(strJournal, L"Port"));
        stConfig.m_strUser = ReadTempValue(strJournal, L"User");
        stConfig.m_strPassword = ReadTempValue(strJournal, L"PasswordPlain");
        stConfig.m_nEncoding = _wtoi(ReadTempValue(strJournal, L"Encoding"));
        stConfig.m_bPassive = ReadTempValue(strJournal, L"Passive") != L"0";
        stConfig.m_strName = ReadTempValue(strJournal, L"TaskName");
        stConfig.m_strId = ReadTempValue(strJournal, L"TaskId");
        stConfig.m_unTaskNumber = (UINT)_wtoi(ReadTempValue(strJournal, L"TaskNumber"));
        stConfig.m_strLocalDir = ExeDir();
        CString strRemotePath = ReadTempValue(strJournal, L"Path");
        CString strError;
        if (!stConfig.Validate(strError) || strRemotePath.IsEmpty() || strRemotePath[0] != L'/' ||
            strRemotePath.FindOneOf(L"\r\n") >= 0)
            continue;
        CString strKey = TempKey(stConfig, strRemotePath);
        if (!ClaimTemp(strKey))
            continue;
        DeleteTemp(stConfig, strRemotePath, strJournal, m_pLogger);
        ReleaseTemp(strKey);
    } while (FindNextFileW(hSearch, &stFile));
}
