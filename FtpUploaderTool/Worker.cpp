#include "stdafx.h"
#include "Worker.h"
#include "RemoteTemp.h"

CTaskWorker::CTaskWorker(const TaskConfig& stConfig, const CString& strRoot, CLogger& loggerFile)
    : m_stConfig(stConfig),
      m_strRoot(strRoot),
      m_loggerFile(loggerFile),
      m_hThread(NULL),
      m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)),
      m_lBeat(0)
{
}

// 最终回收保证线程不再引用配置、状态锁或日志器后才释放对象。
CTaskWorker::~CTaskWorker()
{
    RequestStop();
    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
    }
    CloseHandle(m_hStop);
}

bool CTaskWorker::Start()
{
    if (m_hThread)
        return false;
    CString strError;
    if (!m_stConfig.Validate(strError))
    {
        State(CS_CONFIG_ERROR, L"配置错误");
        Error(strError);
        m_loggerFile.Write(&m_stConfig, 3, strError);
        return false;
    }
    ResetEvent(m_hStop);
    InterlockedExchange(&m_lBeat, (LONG)GetTickCount());
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL);
    return m_hThread != NULL;
}

void CTaskWorker::RequestStop()
{
    SetEvent(m_hStop);
    if (m_hThread)
        State(CS_STOPPING, L"停止中…");
}

bool CTaskWorker::PollStopped()
{
    if (!m_hThread)
        return true;
    if (WaitForSingleObject(m_hThread, 0) != WAIT_OBJECT_0)
        return false;
    CloseHandle(m_hThread);
    m_hThread = NULL;
    return true;
}

TaskStatus CTaskWorker::Status()
{
    CAutoLock lockGuard(m_csStatusLock);
    return m_stStatus;
}

void CTaskWorker::Progress(ULONGLONG ullSent)
{
    InterlockedExchange(&m_lBeat, (LONG)GetTickCount());
    CAutoLock lockGuard(m_csStatusLock);
    m_stStatus.m_ullSent = ullSent;
}

void CTaskWorker::State(int nState, const CString& strText)
{
    CAutoLock lockGuard(m_csStatusLock);
    m_stStatus.m_nState = nState;
    m_stStatus.m_strText = strText;
}

void CTaskWorker::Error(const CString& strText)
{
    CAutoLock lockGuard(m_csStatusLock);
    m_stStatus.m_strLastError = strText;
}

// 后台使用最低线程优先级，发生可恢复异常后等待再进入工作循环。
unsigned __stdcall CTaskWorker::ThreadProc(void* pContext)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    CTaskWorker* pSelf = (CTaskWorker*)pContext;
    for (;;)
    {
        try
        {
            pSelf->Run();
            break;
        }
        catch (CException* pException)
        {
            WCHAR szText[256] = {0};
            pException->GetErrorMessage(szText, _countof(szText));
            pException->Delete();
            pSelf->m_loggerFile.Write(&pSelf->m_stConfig, 3, CString(L"线程异常：") + szText);
        }
        catch (const std::exception&)
        {
            pSelf->m_loggerFile.Write(&pSelf->m_stConfig, 3, L"线程发生 C++ 异常，5 秒后恢复");
        }
        if (WaitForSingleObject(pSelf->m_hStop, 5000) == WAIT_OBJECT_0)
            break;
    }
    pSelf->State(CS_STOPPED, L"已停止");
    return 0;
}

// 重新核对源文件版本，处理远端同名、临时上传、大小核对及成功历史。
int CTaskWorker::UploadOne(CFtpClient& ftpClientSession, CUploadHistory& historyUpload, const PendingFile& stFile,
                           CString& strRemoteDir, CString& strFinalName)
{
    CAutoHandle hInput(CreateFileW(stFile.m_strFullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (!hInput.Valid())
        return FE_LOCAL_OPEN;
    PendingFile stCurrent;
    if (!StatFile(m_stConfig.m_strLocalDir, stFile.m_strFullPath, stCurrent) || !stCurrent.Same(stFile))
        return FE_LOCAL_OPEN;
    {
        CAutoLock lockGuard(m_csStatusLock);
        m_stStatus.m_nState = CS_UPLOADING;
        m_stStatus.m_strText = L"正在上传";
        m_stStatus.m_strCurrentFile = stFile.m_strName;
        m_stStatus.m_ullTotal = stFile.m_ullSize;
        m_stStatus.m_ullSent = 0;
    }
    strRemoteDir = RemoteDirectory(m_stConfig, stFile);
    strFinalName = stFile.m_strName;
    int nError = ftpClientSession.EnsureDirectory(strRemoteDir);
    if (nError)
        return nError;
    LONGLONG llSize = -1;
    bool bSupported;
    nError = ftpClientSession.Size(strFinalName, llSize, bSupported);
    if (nError)
        return nError;
    // 无法检查同名状态时，不冒险执行可能覆盖文件的非覆盖策略。
    if (!bSupported && m_stConfig.m_nRemoteExist != EXIST_OVERWRITE)
        return FE_REMOTE_STATE_UNKNOWN;
    // 同名时始终按选定策略处理，大小相同不能证明内容相同。
    if (llSize >= 0)
    {
        if (m_stConfig.m_nRemoteExist == EXIST_SKIP)
            return FE_REMOTE_EXISTS;
        if (m_stConfig.m_nRemoteExist == EXIST_RENAME)
        {
            int nDot = stFile.m_strName.ReverseFind(L'.');
            CString strSuffix;
            strSuffix.Format(L"_%016I64X", stFile.m_ullModified);
            strFinalName = nDot < 0 ? stFile.m_strName + strSuffix
                                    : stFile.m_strName.Left(nDot) + strSuffix + stFile.m_strName.Mid(nDot);
            LONGLONG llRenamed;
            nError = ftpClientSession.Size(strFinalName, llRenamed, bSupported);
            if (nError)
                return nError;
            if (!bSupported)
                return FE_REMOTE_STATE_UNKNOWN;
            // 改名目标也已存在时保留源文件，不将它误记为成功。
            if (llRenamed >= 0)
                return FE_REMOTE_EXISTS;
        }
    }
    // 只在实际上传、校验及必要的改名完成后写入成功历史。
    {
        CString strUploadName = m_stConfig.m_bUseTempName
                                    ? L"_ftpupload_" + NewId() + m_stConfig.m_strTempSuffix
                                    : strFinalName;
        CRemoteTempFile tempRemote(m_strRoot, ftpClientSession, &m_loggerFile);
        // 临时路径必须先有持久化清理记录，才能开始向服务器写入。
        if (m_stConfig.m_bUseTempName && !tempRemote.Begin(m_stConfig, strRemoteDir, strUploadName))
            return FE_LOCAL_READ;
        nError = ftpClientSession.Upload(hInput.Get(), strUploadName, this);
        if (nError)
            return nError;
        if (m_stConfig.m_bVerifySize)
        {
            LONGLONG llUploaded;
            nError = ftpClientSession.Size(strUploadName, llUploaded, bSupported);
            if (nError)
                return nError;
            if (bSupported && (llUploaded < 0 || (ULONGLONG)llUploaded != stFile.m_ullSize))
            {
                nError = ftpClientSession.Delete(strUploadName);
                return IsSessionError(nError) || nError == FE_STOPPED ? nError : FE_VERIFY;
            }
            if (!bSupported)
                m_loggerFile.Write(&m_stConfig, 2, L"服务器不支持 SIZE，跳过大小校验");
        }
        if (m_stConfig.m_bUseTempName)
        {
            nError = ftpClientSession.Rename(strUploadName, strFinalName);
            if (nError == FE_FILE_PERM && llSize >= 0 && m_stConfig.m_nRemoteExist == EXIST_OVERWRITE)
            {
                nError = ftpClientSession.Delete(strFinalName);
                if (!nError)
                    nError = ftpClientSession.Rename(strUploadName, strFinalName);
            }
            if (nError)
                return nError;
            tempRemote.Commit();
        }
    }
    // 历史落盘失败不确认成功，避免执行本地删除后失去成功凭据。
    if (!historyUpload.Add(stFile))
        return FE_LOCAL_READ;
    return FE_OK;
}

// 只处理仍与上传快照相同的源文件，保留策略无需移动或删除。
bool CTaskWorker::PostProcess(const PendingFile& stFile)
{
    if (m_stConfig.m_nPostAction == POST_KEEP)
        return true;
    for (int nAttempt = 0; nAttempt < 3 && !Stopped(m_hStop); ++nAttempt)
    {
        PendingFile stCurrent;
        if (!StatFile(m_stConfig.m_strLocalDir, stFile.m_strFullPath, stCurrent) || !stCurrent.Same(stFile))
            return true;
        bool bSuccess = false;
        // 删除或移动前保持禁止写入的源文件句柄，避免处理期间文件被修改。
        {
            CAutoHandle hInput(CreateFileW(stFile.m_strFullPath, GENERIC_READ, FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                           FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            if (hInput.Valid() && StatFile(m_stConfig.m_strLocalDir, stFile.m_strFullPath, stCurrent) &&
                stCurrent.Same(stFile))
            {
                if (m_stConfig.m_nPostAction != POST_MOVE)
                    bSuccess = DeleteFileW(stFile.m_strFullPath) != FALSE;
                else
                {
                    CString strDir = JoinPath(JoinPath(m_stConfig.m_strBackupDir, FileDate(stFile.m_ullModified, true)),
                                              stFile.m_strSubDir);
                    if (MakeDirs(strDir))
                    {
                        CString strDestination = JoinPath(strDir, stFile.m_strName);
                        if (PathFileExistsW(strDestination))
                            strDestination += L"_" + NewId();
                        CString strTemp = strDestination + L".tmp";
                        bool bCopied = true;
                        CAutoHandle hOutput(
                            CreateFileW(strTemp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
                        if (!hOutput.Valid())
                            bCopied = false;
                        std::vector<BYTE> vecBuffer(65536);
                        while (bCopied)
                        {
                            if (Stopped(m_hStop))
                            {
                                bCopied = false;
                                break;
                            }
                            InterlockedExchange(&m_lBeat, (LONG)GetTickCount());
                            DWORD dwRead = 0;
                            if (!ReadFile(hInput.Get(), &vecBuffer[0], (DWORD)vecBuffer.size(), &dwRead, NULL))
                            {
                                bCopied = false;
                                break;
                            }
                            if (!dwRead)
                                break;
                            bCopied = WriteBytes(hOutput.Get(), &vecBuffer[0], dwRead);
                        }
                        if (bCopied)
                            bCopied = FlushFileBuffers(hOutput.Get()) != FALSE;
                        hOutput.Close();
                        // 备份复制并落盘、改名完成后，最后才删除本地源文件。
                        if (bCopied && MoveFileExW(strTemp, strDestination, MOVEFILE_WRITE_THROUGH))
                            bSuccess = DeleteFileW(stFile.m_strFullPath) != FALSE;
                        else
                            DeleteFileW(strTemp);
                    }
                }
            }
        }
        if (bSuccess)
            return true;
        if (WaitForSingleObject(m_hStop, 500) == WAIT_OBJECT_0)
            return false;
    }
    return false;
}

// 每次启动重新读取历史；扫描、上传及后处理在本任务线程内串行执行。
void CTaskWorker::Run()
{
    MakeDirs(JoinPath(m_strRoot, L"Data"));
    CUploadHistory historyUpload;
    if (!historyUpload.Load(JoinPath(JoinPath(m_strRoot, L"Data"), L"History_" + m_stConfig.m_strId + L".txt")))
    {
        Error(L"上传记录无法读取，已停止以防重复上传");
        return;
    }
    CFtpClient ftpClientSession(m_stConfig, m_hStop);
    CDirScanner scannerDirectory(m_stConfig);
    // 扫描器将待上传队列限制为最多 4096 个文件。
    std::deque<PendingFile> deqQueue;
    // 记录单文件失败次数和暂停状态，扫描时移除已不存在的路径。
    std::map<CString, FailInfo> mapFails;
    // 已完成上传但仍需删除或备份的文件，处理成功或源文件变化后移除。
    std::map<CString, PendingFile> mapPost;
    UINT unWaitSec = 0;
    UINT unFailures = 0;
    DWORD dwFailTick = 0;
    DWORD dwLastScan = GetTickCount() - m_stConfig.m_unScanInterval * 1000;
    DWORD dwLastIo = GetTickCount();
    DWORD dwLastUpload = dwLastIo;
    DWORD dwLastPost = dwLastIo - 30000;
    DWORD dwLastLocalLog = dwLastIo - 600000;
    // 启动后立即检查一次，此后每小时清理，避免每次扫描都重复遍历删除。
    DWORD dwLastEmptyDirCleanup = dwLastIo - 3600000;
    CString strDay = DateText();
    CString strMaintenanceDay;
    while (!Stopped(m_hStop))
    {
        InterlockedExchange(&m_lBeat, (LONG)GetTickCount());
        CString strToday = DateText();
        if (strToday > strDay)
        {
            strDay = strToday;
            CAutoLock lockGuard(m_csStatusLock);
            m_stStatus.m_unTodayOk = m_stStatus.m_unTodayFail = 0;
            m_stStatus.m_ullTodayBytes = 0;
        }
        if (TickElapsed(dwLastScan, m_stConfig.m_unScanInterval * 1000))
        {
            dwLastScan = GetTickCount();
            if (!scannerDirectory.Scan(deqQueue, historyUpload, mapFails, mapPost, m_hStop))
            {
                Error(L"本地目录不可访问");
                if (TickElapsed(dwLastLocalLog, 600000))
                {
                    m_loggerFile.Write(&m_stConfig, 3, L"本地目录不可访问：" + m_stConfig.m_strLocalDir);
                    dwLastLocalLog = GetTickCount();
                }
            }
            else if (strToday > strMaintenanceDay)
            {
                historyUpload.Compact(m_stConfig.m_strLocalDir);
                strMaintenanceDay = strToday;
            }
        }
        if (TickElapsed(dwLastPost, 30000))
        {
            dwLastPost = GetTickCount();
            for (std::map<CString, PendingFile>::iterator itI = mapPost.begin(); itI != mapPost.end();)
            {
                if (PostProcess(itI->second))
                    mapPost.erase(itI++);
                else
                    ++itI;
            }
        }
        if (m_stConfig.m_bDeleteOldEmptyDirs && TickElapsed(dwLastEmptyDirCleanup, 3600000))
        {
            dwLastEmptyDirCleanup = GetTickCount();
            scannerDirectory.CleanupEmptyDirs(m_loggerFile, m_hStop);
        }
        UINT unParked = 0;
        for (std::map<CString, FailInfo>::iterator itI = mapFails.begin(); itI != mapFails.end(); ++itI)
            if (itI->second.m_bParked)
                ++unParked;
        {
            CAutoLock lockGuard(m_csStatusLock);
            m_stStatus.m_unPending = (UINT)(deqQueue.size() + mapPost.size());
            m_stStatus.m_unParked = unParked;
        }
        int nError = FE_OK;
        if (!ftpClientSession.Connected())
        {
            if ((m_stConfig.m_unIdleDisconn == 0 || !deqQueue.empty()) && TickElapsed(dwFailTick, unWaitSec * 1000))
            {
                State(CS_CONNECTING, L"正在连接");
                nError = ftpClientSession.Connect();
                if (!nError)
                {
                    dwLastIo = dwLastUpload = GetTickCount();
                    State(CS_IDLE, L"已连接");
                    m_loggerFile.Write(&m_stConfig, 1, L"已连接 " + m_stConfig.m_strHost);
                }
            }
            else if (unFailures)
            {
                CString strText;
                DWORD dwElapsed = (GetTickCount() - dwFailTick) / 1000;
                strText.Format(L"第 %u 次重连，%u 秒后重试", unFailures,
                               dwElapsed < unWaitSec ? unWaitSec - dwElapsed : 0);
                State(CS_RECONNECT, strText);
            }
        }
        else if (!deqQueue.empty())
        {
            PendingFile stFile = deqQueue.front();
            deqQueue.pop_front();
            CString strRemote;
            CString strName;
            DWORD dwStart = GetTickCount();
            nError = UploadOne(ftpClientSession, historyUpload, stFile, strRemote, strName);
            if (nError == FE_OK)
            {
                mapPost[stFile.m_strFullPath] = stFile;
                scannerDirectory.Dequeued(stFile.m_strFullPath);
                mapFails.erase(stFile.m_strFullPath);
                dwLastIo = dwLastUpload = GetTickCount();
                unWaitSec = unFailures = 0;
                m_loggerFile.Record(m_stConfig, stFile.m_strFullPath, strRemote, strName, stFile.m_ullSize,
                                    GetTickCount() - dwStart, L"OK", L"");
                {
                    CAutoLock lockGuard(m_csStatusLock);
                    ++m_stStatus.m_unTodayOk;
                    m_stStatus.m_ullTodayBytes += stFile.m_ullSize;
                    m_stStatus.m_strLastOk = TimeText().Mid(11, 8);
                    m_stStatus.m_strLastError.Empty();
                }
                if (PostProcess(stFile))
                    mapPost.erase(stFile.m_strFullPath);
                else
                    m_loggerFile.Write(&m_stConfig, 2, L"上传已完成，后处理稍后重试：" + stFile.m_strRelative);
                nError = FE_OK;
            }
            // 网络故障将当前文件放回队首，进入重连流程，不增加单文件失败次数。
            else if (IsSessionError(nError) || nError == FE_STOPPED)
            {
                deqQueue.push_front(stFile);
                m_loggerFile.Record(m_stConfig, stFile.m_strFullPath, strRemote, strName, stFile.m_ullSize,
                                    GetTickCount() - dwStart, L"RETRY", ftpClientSession.ErrorText(nError));
            }
            else
            {
                scannerDirectory.Dequeued(stFile.m_strFullPath);
                if (nError != FE_LOCAL_OPEN)
                {
                    FailInfo& stF = mapFails[stFile.m_strFullPath];
                    ++stF.m_unAttempts;
                    stF.m_dwLastTick = GetTickCount();
                    stF.m_bParked =
                        stF.m_unAttempts >= m_stConfig.m_unMaxFileRetry || nError == FE_REMOTE_EXISTS ||
                        m_stConfig.m_nRemoteExist == EXIST_SKIP;
                    CString strError = ftpClientSession.ErrorText(nError);
                    CString strText;
                    strText.Format(L"%s 第 %u 次失败%s：%s", (LPCWSTR)stFile.m_strRelative, stF.m_unAttempts,
                                   stF.m_bParked ? L"，已搁置" : L"", (LPCWSTR)strError);
                    Error(strError);
                    m_loggerFile.Record(m_stConfig, stFile.m_strFullPath, strRemote, strName, stFile.m_ullSize,
                                        GetTickCount() - dwStart, stF.m_bParked ? L"FAIL" : L"RETRY", strText);
                    if (stF.m_bParked)
                    {
                        CAutoLock lockGuard(m_csStatusLock);
                        ++m_stStatus.m_unTodayFail;
                    }
                }
                nError = FE_OK;
            }
            {
                CAutoLock lockGuard(m_csStatusLock);
                m_stStatus.m_strCurrentFile.Empty();
            }
            if (!nError)
            {
                State(CS_IDLE, L"已连接");
                continue;
            }
        }
        else if (m_stConfig.m_unIdleDisconn && TickElapsed(dwLastUpload, m_stConfig.m_unIdleDisconn * 1000))
        {
            ftpClientSession.Disconnect();
            State(CS_IDLE, L"空闲，已断开");
        }
        else if (TickElapsed(dwLastIo, m_stConfig.m_unHeartbeat * 1000))
        {
            nError = ftpClientSession.Noop();
            if (!nError)
            {
                dwLastIo = GetTickCount();
                unFailures = unWaitSec = 0;
            }
        }
        if (nError == FE_STOPPED)
            break;
        if (nError != FE_OK)
        {
            CString strError = ftpClientSession.ErrorText(nError);
            ftpClientSession.Disconnect();
            ++unFailures;
            // 网络错误逐次加倍等待至上限，登录失败直接使用上限，均不限制重连次数。
            unWaitSec = nError == FE_LOGIN ? m_stConfig.m_unReconnMax
                        : unWaitSec == 0   ? m_stConfig.m_unReconnMin
                                           : min(unWaitSec * 2, m_stConfig.m_unReconnMax);
            dwFailTick = GetTickCount();
            State(CS_RECONNECT, L"等待重连");
            Error(strError);
            {
                CAutoLock lockGuard(m_csStatusLock);
                ++m_stStatus.m_unReconnects;
            }
            CString strText;
            strText.Format(L"%s；第 %u 次失败，%u 秒后重连", (LPCWSTR)strError, unFailures, unWaitSec);
            m_loggerFile.Write(&m_stConfig, 3, strText);
        }
        WaitForSingleObject(m_hStop, 200);
    }
}
