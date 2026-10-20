#include "stdafx.h"
#include "Worker.h"
CTaskWorker::CTaskWorker(const TaskConfig& stConfig, const CString& strRoot, CLogger& loggerFile, UINT unBackupKeepDays) :
    m_stConfig(stConfig), m_strRoot(strRoot), m_loggerFile(loggerFile), m_unBackupKeepDays(unBackupKeepDays), m_hThread(NULL), m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)), m_lBeat(0) {}
CTaskWorker::~CTaskWorker() {
    RequestStop(); if (m_hThread) { WaitForSingleObject(m_hThread, INFINITE); CloseHandle(m_hThread); }
    CloseHandle(m_hStop);
}
bool CTaskWorker::Start() {
    if (m_hThread) return false; CString strError;
    if (!m_stConfig.Validate(strError)) { State(CS_CONFIG_ERROR, L"配置错误"); Error(strError); m_loggerFile.Write(&m_stConfig, 3, strError); return false; }
    ResetEvent(m_hStop); InterlockedExchange(&m_lBeat, (LONG)GetTickCount());
    m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL); return m_hThread != NULL;
}
void CTaskWorker::RequestStop() { SetEvent(m_hStop); if (m_hThread) State(CS_STOPPING, L"停止中…"); }
bool CTaskWorker::PollStopped() {
    if (!m_hThread) return true;
    if (WaitForSingleObject(m_hThread, 0) != WAIT_OBJECT_0) return false;
    CloseHandle(m_hThread); m_hThread = NULL; return true;
}
TaskStatus CTaskWorker::Status() { CAutoLock lockGuard(m_csStatusLock); return m_stStatus; }
void CTaskWorker::Progress(ULONGLONG ullSent) { InterlockedExchange(&m_lBeat, (LONG)GetTickCount()); CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_ullSent = ullSent; }
void CTaskWorker::State(int nState, const CString& strText) { CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_nState = nState; m_stStatus.m_strText = strText; }
void CTaskWorker::Error(const CString& strText) { CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_strLastError = strText; }
unsigned __stdcall CTaskWorker::ThreadProc(void* pContext) {
    CTaskWorker* pSelf = (CTaskWorker*)pContext;
    for (;;) {
        try { pSelf->Run(); break; }
        catch (CException* pException) { WCHAR szText[256] = {0}; pException->GetErrorMessage(szText, _countof(szText)); pException->Delete(); pSelf->m_loggerFile.Write(&pSelf->m_stConfig, 3, CString(L"线程异常：") + szText); }
        catch (const std::exception&) { pSelf->m_loggerFile.Write(&pSelf->m_stConfig, 3, L"线程发生 C++ 异常，5 秒后恢复"); }
        if (WaitForSingleObject(pSelf->m_hStop, 5000) == WAIT_OBJECT_0) break;
    }
    pSelf->State(CS_STOPPED, L"已停止"); return 0;
}
int CTaskWorker::UploadOne(CFtpClient& ftpClientSession, CUploadHistory& historyUpload, const PendingFile& stFile, CString& strRemoteDir, CString& strFinalName) {
    CAutoHandle hInput(CreateFileW(stFile.m_strFullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (!hInput.Valid()) return FE_LOCAL_OPEN;
    PendingFile stCurrent; if (!StatFile(m_stConfig.m_strLocalDir, stFile.m_strFullPath, stCurrent) || !stCurrent.Same(stFile)) return FE_LOCAL_OPEN;
    { CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_nState = CS_UPLOADING; m_stStatus.m_strText = L"正在上传"; m_stStatus.m_strCurrentFile = stFile.m_strName; m_stStatus.m_ullTotal = stFile.m_ullSize; m_stStatus.m_ullSent = 0; }
    strRemoteDir = RemoteDirectory(m_stConfig, stFile); strFinalName = stFile.m_strName;
    int nError = ftpClientSession.EnsureDirectory(strRemoteDir); if (nError) return nError;
    LONGLONG llSize = -1; bool bSupported; nError = ftpClientSession.Size(strFinalName, llSize, bSupported); if (nError) return nError;
    bool bSkip = llSize >= 0 && (ULONGLONG)llSize == stFile.m_ullSize;
    if (!bSkip && llSize >= 0) {
        if (m_stConfig.m_nRemoteExist == EXIST_SKIP) return FE_FILE_PERM;
        if (m_stConfig.m_nRemoteExist == EXIST_RENAME) {
            int nDot = stFile.m_strName.ReverseFind(L'.'); CString strSuffix; strSuffix.Format(L"_%016I64X", stFile.m_ullModified);
            strFinalName = nDot < 0 ? stFile.m_strName + strSuffix : stFile.m_strName.Left(nDot) + strSuffix + stFile.m_strName.Mid(nDot);
            LONGLONG llRenamed; nError = ftpClientSession.Size(strFinalName, llRenamed, bSupported); if (nError) return nError;
            bSkip = llRenamed >= 0 && (ULONGLONG)llRenamed == stFile.m_ullSize; if (llRenamed >= 0 && !bSkip) return FE_FILE_PERM;
        }
    }
    if (!bSkip) {
        CString strUploadName = m_stConfig.m_bUseTempName ? strFinalName + m_stConfig.m_strTempSuffix : strFinalName;
        nError = ftpClientSession.Upload(hInput.Get(), strUploadName, this); if (nError) return nError;
        if (m_stConfig.m_bVerifySize) {
            LONGLONG llUploaded; nError = ftpClientSession.Size(strUploadName, llUploaded, bSupported); if (nError) return nError;
            if (bSupported && (llUploaded < 0 || (ULONGLONG)llUploaded != stFile.m_ullSize)) { nError = ftpClientSession.Delete(strUploadName); return IsSessionError(nError) || nError == FE_STOPPED ? nError : FE_VERIFY; }
            if (!bSupported) m_loggerFile.Write(&m_stConfig, 2, L"服务器不支持 SIZE，跳过大小校验");
        }
        if (m_stConfig.m_bUseTempName) {
            nError = ftpClientSession.Rename(strUploadName, strFinalName);
            if (nError == FE_FILE_PERM && llSize >= 0 && m_stConfig.m_nRemoteExist == EXIST_OVERWRITE) { nError = ftpClientSession.Delete(strFinalName); if (!nError) nError = ftpClientSession.Rename(strUploadName, strFinalName); }
            if (nError) return nError;
        }
    }
    if (!historyUpload.Add(stFile)) return FE_LOCAL_READ;
    return bSkip ? 1 : FE_OK;
}
bool CTaskWorker::PostProcess(const PendingFile& stFile) {
    if (m_stConfig.m_nPostAction == POST_KEEP) {
        FILETIME stNow; GetSystemTimeAsFileTime(&stNow); ULONGLONG ullT = ((ULONGLONG)stNow.dwHighDateTime << 32) | stNow.dwLowDateTime;
        if (!m_stConfig.m_unKeepDays || ullT <= stFile.m_ullModified || (ullT - stFile.m_ullModified) / 864000000000ULL < m_stConfig.m_unKeepDays) return true;
    }
    for (int nAttempt = 0; nAttempt < 3 && !Stopped(m_hStop); ++nAttempt) {
        PendingFile stCurrent; if (!StatFile(m_stConfig.m_strLocalDir, stFile.m_strFullPath, stCurrent) || !stCurrent.Same(stFile)) return true;
        bool bSuccess = false;
        { // Hold a no-writer handle through destructive post-processing.
            CAutoHandle hInput(CreateFileW(stFile.m_strFullPath, GENERIC_READ, FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            if (hInput.Valid() && StatFile(m_stConfig.m_strLocalDir, stFile.m_strFullPath, stCurrent) && stCurrent.Same(stFile)) {
                if (m_stConfig.m_nPostAction != POST_MOVE) bSuccess = DeleteFileW(stFile.m_strFullPath) != FALSE;
                else {
                    CString strDir = JoinPath(JoinPath(m_stConfig.m_strBackupDir, FileDate(stFile.m_ullModified, true)), stFile.m_strSubDir);
                    if (MakeDirs(strDir)) {
                        CString strDestination = JoinPath(strDir, stFile.m_strName);
                        if (PathFileExistsW(strDestination)) strDestination += L"_" + NewId();
                        CString strTemp = strDestination + L".tmp"; bool bCopied = true;
                        CAutoHandle hOutput(CreateFileW(strTemp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
                        if (!hOutput.Valid()) bCopied = false;
                        std::vector<BYTE> vecBuffer(65536);
                        while (bCopied) {
                            if (Stopped(m_hStop)) { bCopied = false; break; }
                            InterlockedExchange(&m_lBeat, (LONG)GetTickCount()); DWORD dwRead = 0;
                            if (!ReadFile(hInput.Get(), &vecBuffer[0], (DWORD)vecBuffer.size(), &dwRead, NULL)) { bCopied = false; break; }
                            if (!dwRead) break; bCopied = WriteBytes(hOutput.Get(), &vecBuffer[0], dwRead);
                        }
                        if (bCopied) bCopied = FlushFileBuffers(hOutput.Get()) != FALSE; hOutput.Close();
                        if (bCopied && MoveFileExW(strTemp, strDestination, MOVEFILE_WRITE_THROUGH)) bSuccess = DeleteFileW(stFile.m_strFullPath) != FALSE;
                        else DeleteFileW(strTemp);
                    }
                }
            }
        }
        if (bSuccess) return true;
        if (WaitForSingleObject(m_hStop, 500) == WAIT_OBJECT_0) return false;
    }
    return false;
}
void CTaskWorker::CleanupBackup() {
    if (m_stConfig.m_nPostAction != POST_MOVE || !m_unBackupKeepDays) return;
    WIN32_FIND_DATAW stFd; HANDLE hHandle = FindFirstFileW(JoinPath(m_stConfig.m_strBackupDir, L"*"), &stFd); if (hHandle == INVALID_HANDLE_VALUE) return; CFindHandle hFindFind(hHandle);
    FILETIME stNow; GetSystemTimeAsFileTime(&stNow); ULONGLONG ullT = ((ULONGLONG)stNow.dwHighDateTime << 32) | stNow.dwLowDateTime;
    do {
        if (Stopped(m_hStop)) return;
        if (!(stFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || stFd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT || wcslen(stFd.cFileName) != 8) continue;
        unsigned unY, unM, unD; if (swscanf_s(stFd.cFileName, L"%4u%2u%2u", &unY, &unM, &unD) != 3) continue;
        SYSTEMTIME stTime = {0}; stTime.wYear = (WORD)unY; stTime.wMonth = (WORD)unM; stTime.wDay = (WORD)unD; FILETIME stFileTime;
        if (!SystemTimeToFileTime(&stTime, &stFileTime)) continue; ULONGLONG ullAge = ((ULONGLONG)stFileTime.dwHighDateTime << 32) | stFileTime.dwLowDateTime;
        if (ullT > ullAge && (ullT - ullAge) / 864000000000ULL >= m_unBackupKeepDays) DeleteTree(JoinPath(m_stConfig.m_strBackupDir, stFd.cFileName), m_hStop);
    } while (FindNextFileW(hHandle, &stFd));
}
void CTaskWorker::Run() {
    MakeDirs(JoinPath(m_strRoot, L"Data")); CUploadHistory historyUpload;
    if (!historyUpload.Load(JoinPath(JoinPath(m_strRoot, L"Data"), L"History_" + m_stConfig.m_strId + L".txt"))) { Error(L"上传记录无法读取，已停止以防重复上传"); return; }
    CFtpClient ftpClientSession(m_stConfig, m_hStop); CDirScanner scannerDirectory(m_stConfig);
    std::deque<PendingFile> deqQueue; // Scanner caps at 4096.
    std::map<CString, FailInfo> mapFails; // Scanner removes paths no longer present.
    std::map<CString, PendingFile> mapPost; // Removed after success or source generation change.
    UINT unWaitSec = 0, unFailures = 0; DWORD dwFailTick = 0, dwLastScan = GetTickCount() - m_stConfig.m_unScanInterval * 1000;
    DWORD dwLastIo = GetTickCount(), dwLastUpload = dwLastIo, dwLastPost = dwLastIo - 30000, dwLastLocalLog = dwLastIo - 600000, dwLastErrorLog = dwLastLocalLog;
    CString strLastError, strDay = DateText(), strMaintenanceDay;
    while (!Stopped(m_hStop)) {
        InterlockedExchange(&m_lBeat, (LONG)GetTickCount());
        CString strToday = DateText(); if (strToday > strDay) { strDay = strToday; CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_unTodayOk = m_stStatus.m_unTodayFail = 0; m_stStatus.m_ullTodayBytes = 0; }
        if (TickElapsed(dwLastScan, m_stConfig.m_unScanInterval * 1000)) {
            dwLastScan = GetTickCount();
            if (!scannerDirectory.Scan(deqQueue, historyUpload, mapFails, mapPost, m_hStop)) {
                Error(L"本地目录不可访问"); if (TickElapsed(dwLastLocalLog, 600000)) { m_loggerFile.Write(&m_stConfig, 3, L"本地目录不可访问：" + m_stConfig.m_strLocalDir); dwLastLocalLog = GetTickCount(); }
            } else if (strToday > strMaintenanceDay) { historyUpload.Compact(m_stConfig.m_strLocalDir); CleanupBackup(); strMaintenanceDay = strToday; }
        }
        if (TickElapsed(dwLastPost, 30000)) {
            dwLastPost = GetTickCount();
            for (std::map<CString, PendingFile>::iterator itI = mapPost.begin(); itI != mapPost.end();) { if (PostProcess(itI->second)) mapPost.erase(itI++); else ++itI; }
        }
        UINT unParked = 0; for (std::map<CString, FailInfo>::iterator itI = mapFails.begin(); itI != mapFails.end(); ++itI) if (itI->second.m_bParked) ++unParked;
        { CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_unPending = (UINT)(deqQueue.size() + mapPost.size()); m_stStatus.m_unParked = unParked; }
        int nError = FE_OK;
        if (!ftpClientSession.Connected()) {
            if ((m_stConfig.m_unIdleDisconn == 0 || !deqQueue.empty()) && TickElapsed(dwFailTick, unWaitSec * 1000)) {
                State(CS_CONNECTING, L"正在连接"); nError = ftpClientSession.Connect();
                if (!nError) { dwLastIo = dwLastUpload = GetTickCount(); State(CS_IDLE, L"已连接"); m_loggerFile.Write(&m_stConfig, 1, L"已连接 " + m_stConfig.m_strHost); }
            } else if (unFailures) { CString strText; DWORD dwElapsed = (GetTickCount() - dwFailTick) / 1000; strText.Format(L"第 %u 次重连，%u 秒后重试", unFailures, dwElapsed < unWaitSec ? unWaitSec - dwElapsed : 0); State(CS_RECONNECT, strText); }
        } else if (!deqQueue.empty()) {
            PendingFile stFile = deqQueue.front(); deqQueue.pop_front(); CString strRemote, strName; DWORD dwStart = GetTickCount();
            nError = UploadOne(ftpClientSession, historyUpload, stFile, strRemote, strName);
            if (nError == FE_OK || nError == 1) {
                mapPost[stFile.m_strFullPath] = stFile; scannerDirectory.Dequeued(stFile.m_strFullPath); mapFails.erase(stFile.m_strFullPath);
                dwLastIo = dwLastUpload = GetTickCount(); unWaitSec = unFailures = 0;
                m_loggerFile.Record(m_stConfig, stFile.m_strFullPath, strRemote, strName, stFile.m_ullSize, GetTickCount() - dwStart, nError == 1 ? L"SKIP_EXIST" : L"OK", L"");
                m_loggerFile.Write(&m_stConfig, 1, (nError == 1 ? CString(L"远程已存在：") : CString(L"上传成功：")) + stFile.m_strRelative);
                { CAutoLock lockGuard(m_csStatusLock); ++m_stStatus.m_unTodayOk; m_stStatus.m_ullTodayBytes += stFile.m_ullSize; m_stStatus.m_strLastOk = TimeText().Mid(11, 8); m_stStatus.m_strLastError.Empty(); }
                if (PostProcess(stFile)) mapPost.erase(stFile.m_strFullPath); else m_loggerFile.Write(&m_stConfig, 2, L"上传已完成，后处理稍后重试：" + stFile.m_strRelative);
                nError = FE_OK;
            } else if (IsSessionError(nError) || nError == FE_STOPPED) { deqQueue.push_front(stFile); m_loggerFile.Record(m_stConfig, stFile.m_strFullPath, strRemote, strName, stFile.m_ullSize, GetTickCount() - dwStart, L"RETRY", ftpClientSession.ErrorText(nError)); }
            else {
                scannerDirectory.Dequeued(stFile.m_strFullPath);
                if (nError != FE_LOCAL_OPEN) {
                    FailInfo& stF = mapFails[stFile.m_strFullPath]; ++stF.m_unAttempts; stF.m_dwLastTick = GetTickCount(); stF.m_bParked = stF.m_unAttempts >= m_stConfig.m_unMaxFileRetry || m_stConfig.m_nRemoteExist == EXIST_SKIP;
                    CString strError = ftpClientSession.ErrorText(nError), strText; strText.Format(L"%s 第 %u 次失败%s：%s", (LPCWSTR)stFile.m_strRelative, stF.m_unAttempts, stF.m_bParked ? L"，已搁置" : L"", (LPCWSTR)strError);
                    Error(strError); m_loggerFile.Write(&m_stConfig, stF.m_bParked ? 3 : 2, strText); m_loggerFile.Record(m_stConfig, stFile.m_strFullPath, strRemote, strName, stFile.m_ullSize, GetTickCount() - dwStart, stF.m_bParked ? L"FAIL" : L"RETRY", strError);
                    if (stF.m_bParked) { CAutoLock lockGuard(m_csStatusLock); ++m_stStatus.m_unTodayFail; }
                }
                nError = FE_OK;
            }
            { CAutoLock lockGuard(m_csStatusLock); m_stStatus.m_strCurrentFile.Empty(); }
            if (!nError) { State(CS_IDLE, L"已连接"); continue; }
        } else if (m_stConfig.m_unIdleDisconn && TickElapsed(dwLastUpload, m_stConfig.m_unIdleDisconn * 1000)) { ftpClientSession.Disconnect(); State(CS_IDLE, L"空闲，已断开"); }
        else if (TickElapsed(dwLastIo, m_stConfig.m_unHeartbeat * 1000)) { nError = ftpClientSession.Noop(); if (!nError) { dwLastIo = GetTickCount(); unFailures = unWaitSec = 0; } }
        if (nError == FE_STOPPED) break;
        if (nError != FE_OK) {
            CString strError = ftpClientSession.ErrorText(nError); ftpClientSession.Disconnect(); ++unFailures;
            unWaitSec = nError == FE_LOGIN ? m_stConfig.m_unReconnMax : unWaitSec == 0 ? m_stConfig.m_unReconnMin : min(unWaitSec * 2, m_stConfig.m_unReconnMax);
            dwFailTick = GetTickCount(); State(CS_RECONNECT, L"等待重连"); Error(strError);
            { CAutoLock lockGuard(m_csStatusLock); ++m_stStatus.m_unReconnects; }
            if (unFailures <= 3 || strLastError != strError || TickElapsed(dwLastErrorLog, 600000)) {
                CString strText; strText.Format(L"%s；第 %u 次失败，%u 秒后重连", (LPCWSTR)strError, unFailures, unWaitSec); m_loggerFile.Write(&m_stConfig, 3, strText); strLastError = strError; dwLastErrorLog = GetTickCount();
            }
        }
        WaitForSingleObject(m_hStop, 200);
    }
}
