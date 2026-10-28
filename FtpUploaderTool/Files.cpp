#include "stdafx.h"
#include "Files.h"
#include "Logger.h"

bool StatFile(const CString& strRoot, const CString& strPath, PendingFile& stFile)
{
    WIN32_FILE_ATTRIBUTE_DATA stData;
    if (!GetFileAttributesExW(strPath, GetFileExInfoStandard, &stData) ||
        stData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return false;
    stFile.m_strFullPath = strPath;
    stFile.m_strRelative = strPath.Mid(strRoot.GetLength());
    stFile.m_strRelative.TrimLeft(L"\\/");
    int nSlash = stFile.m_strRelative.ReverseFind(L'\\');
    stFile.m_strName = nSlash < 0 ? stFile.m_strRelative : stFile.m_strRelative.Mid(nSlash + 1);
    stFile.m_strSubDir = nSlash < 0 ? CString() : stFile.m_strRelative.Left(nSlash);
    stFile.m_ullSize = ((ULONGLONG)stData.nFileSizeHigh << 32) | stData.nFileSizeLow;
    stFile.m_ullModified =
        ((ULONGLONG)stData.ftLastWriteTime.dwHighDateTime << 32) | stData.ftLastWriteTime.dwLowDateTime;
    return true;
}

static SYSTEMTIME LocalModified(ULONGLONG ullModified)
{
    ULARGE_INTEGER stValue;
    stValue.QuadPart = ullModified;
    FILETIME stFileTime = {stValue.LowPart, stValue.HighPart};
    FILETIME stLocal;
    SYSTEMTIME stTime = {0};
    FileTimeToLocalFileTime(&stFileTime, &stLocal);
    FileTimeToSystemTime(&stLocal, &stTime);
    return stTime;
}

CString FileDate(ULONGLONG ullModified, bool bCompact)
{
    SYSTEMTIME stTime = LocalModified(ullModified);
    CString strText;
    strText.Format(bCompact ? L"%04u%02u%02u" : L"%04u-%02u-%02u", stTime.wYear, stTime.wMonth, stTime.wDay);
    return strText;
}

// 使用源文件的本地修改时间展开日期占位符，并保留所需的相对子目录。
CString RemoteDirectory(const TaskConfig& stConfig, const PendingFile& stFile)
{
    CString strResult(stConfig.m_strRemoteDir);
    CString strValue;
    CString strSub(stFile.m_strSubDir);
    strSub.Replace(L'\\', L'/');
    SYSTEMTIME stTime = LocalModified(stFile.m_ullModified);
    strValue.Format(L"%04u", stTime.wYear);
    strResult.Replace(L"{yyyy}", strValue);
    strValue.Format(L"%02u", stTime.wMonth);
    strResult.Replace(L"{MM}", strValue);
    strValue.Format(L"%02u", stTime.wDay);
    strResult.Replace(L"{dd}", strValue);
    strValue.Format(L"%02u", stTime.wHour);
    strResult.Replace(L"{HH}", strValue);
    WCHAR szPc[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD dwN = _countof(szPc);
    GetComputerNameW(szPc, &dwN);
    strResult.Replace(L"{pc}", szPc);
    strResult.Replace(L"{sub}", strSub);
    if (stConfig.m_bIncludeSubDir && stConfig.m_strRemoteDir.Find(L"{sub}") < 0 && !strSub.IsEmpty())
    {
        strResult.TrimRight(L"/");
        strResult += L"/" + strSub;
    }
    return strResult;
}

// 从磁盘恢复相对路径、文件大小及修改时间签名，不依赖上次进程内存。
bool CUploadHistory::Load(const CString& strPath)
{
    m_strPath = strPath;
    m_mapRecords.clear();
    if (!PathFileExistsW(strPath))
        return true;
    CStdioFile fileFile;
    CFileException stError;
    if (!fileFile.Open(strPath, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, &stError))
        return false;
    CStringA strALine;
    char szBuffer[4096];
    UINT unCount;
    while ((unCount = fileFile.Read(szBuffer, sizeof(szBuffer))) > 0)
    {
        strALine.Append(szBuffer, unCount);
        int nNewline;
        while ((nNewline = strALine.Find('\n')) >= 0)
        {
            CString strText = FromBytes(strALine.Left(nNewline));
            strALine.Delete(0, nNewline + 1);
            int nA = strText.Find(L'|');
            int nB = strText.Find(L'|', nA + 1);
            if (nA < 1 || nB < nA + 2)
                continue;
            ULONGLONG ullSize = 0;
            ULONGLONG ullModified = 0;
            if (swscanf_s(strText.Mid(nA + 1, nB - nA - 1), L"%I64u", &ullSize) == 1 &&
                swscanf_s(strText.Mid(nB + 1), L"%I64x", &ullModified) == 1)
                m_mapRecords[strText.Left(nA)] = std::make_pair(ullSize, ullModified);
        }
        if (strALine.GetLength() > 65536)
            strALine.Empty();
    }
    return true;
}

bool CUploadHistory::Contains(const PendingFile& stFile) const
{
    std::map<CString, std::pair<ULONGLONG, ULONGLONG> >::const_iterator itI = m_mapRecords.find(stFile.m_strRelative);
    return itI != m_mapRecords.end() && itI->second.first == stFile.m_ullSize &&
           itI->second.second == stFile.m_ullModified;
}

// 先持久化成功记录，再更新内存，防止重启后重复上传已确认的文件。
bool CUploadHistory::Add(const PendingFile& stFile)
{
    CString strLine;
    strLine.Format(L"\n%s|%I64u|%016I64X|%s\n", (LPCWSTR)stFile.m_strRelative, stFile.m_ullSize, stFile.m_ullModified,
                   (LPCWSTR)TimeText());
    CStringA strABytes = ToBytes(strLine);
    CAutoHandle hOutput(
        CreateFileW(m_strPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (!hOutput.Valid() || !WriteBytes(hOutput.Get(), (LPCSTR)strABytes, strABytes.GetLength()) ||
        !FlushFileBuffers(hOutput.Get()))
        return false;
    m_mapRecords[stFile.m_strRelative] = std::make_pair(stFile.m_ullSize, stFile.m_ullModified);
    return true;
}

// 清除已不存在的源文件记录，避免历史表长期积累失效路径。
bool CUploadHistory::Compact(const CString& strRoot)
{
    CStringA strAText;
    for (std::map<CString, std::pair<ULONGLONG, ULONGLONG> >::iterator itI = m_mapRecords.begin();
         itI != m_mapRecords.end();)
    {
        CString strPath = JoinPath(strRoot, itI->first);
        if (!IsWithin(strRoot, strPath) || !PathFileExistsW(strPath))
        {
            m_mapRecords.erase(itI++);
            continue;
        }
        CString strLine;
        strLine.Format(L"%s|%I64u|%016I64X|%s\n", (LPCWSTR)itI->first, itI->second.first, itI->second.second,
                       (LPCWSTR)TimeText());
        strAText += ToBytes(strLine);
        ++itI;
    }
    return AtomicWrite(m_strPath, strAText);
}

bool CDirScanner::Match(const CString& strName, const CString& strMasks, bool bEmpty) const
{
    if (strMasks.IsEmpty())
        return bEmpty;
    int nStart = 0;
    CString strMask;
    while (!(strMask = strMasks.Tokenize(L";", nStart)).IsEmpty())
    {
        strMask.Trim();
        if (PathMatchSpecW(strName, strMask))
            return true;
    }
    return false;
}

// 只遍历本地目录，跳过隐藏、系统和重解析项，子目录深度最多 10 层。
void CDirScanner::Walk(const CString& strDirectory, int nDepth, std::vector<PendingFile>& vecFound, HANDLE hStop)
{
    WIN32_FIND_DATAW stFd;
    HANDLE hHandle = FindFirstFileW(JoinPath(strDirectory, L"*"), &stFd);
    if (hHandle == INVALID_HANDLE_VALUE)
        return;
    CFindHandle hFindFind(hHandle);
    do
    {
        if (Stopped(hStop))
            return;
        if (stFd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (wcscmp(stFd.cFileName, L".") == 0 || wcscmp(stFd.cFileName, L"..") == 0)
            continue;
        CString strPath = JoinPath(strDirectory, stFd.cFileName);
        if (stFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (m_stConfig.m_bIncludeSubDir && nDepth < 10)
                Walk(strPath, nDepth + 1, vecFound, hStop);
        }
        else if (Match(stFd.cFileName, m_stConfig.m_strIncludeMask, true) &&
                 !Match(stFd.cFileName, m_stConfig.m_strExcludeMask, false) &&
                 (!m_stConfig.m_bUseTempName || CString(stFd.cFileName)
                                                        .Right(m_stConfig.m_strTempSuffix.GetLength())
                                                        .CompareNoCase(m_stConfig.m_strTempSuffix) != 0))
        {
            PendingFile stFile;
            if (StatFile(m_stConfig.m_strLocalDir, strPath, stFile))
                vecFound.push_back(stFile);
        }
    } while (FindNextFileW(hHandle, &stFd));
}

static bool FileOlder(const PendingFile& stA, const PendingFile& stB)
{
    return stA.m_ullModified < stB.m_ullModified ||
           (stA.m_ullModified == stB.m_ullModified && stA.m_strRelative < stB.m_strRelative);
}

// 只遍历普通目录；RemoveDirectory 会拒绝删除在检查后又写入文件的目录。
void CDirScanner::CleanupDirectory(const CString& strDirectory, int nDepth, ULONGLONG ullCutoff,
                                  CLogger& loggerFile, HANDLE hStop, UINT& unDeleted, UINT& unFailed)
{
    if (Stopped(hStop))
        return;
    WIN32_FILE_ATTRIBUTE_DATA stDirectory;
    if (!GetFileAttributesExW(strDirectory, GetFileExInfoStandard, &stDirectory))
    {
        DWORD dwError = GetLastError();
        ++unFailed;
        loggerFile.Operation(L"清理旧空文件夹", L"无法读取目录：" + strDirectory + L"；" + WinError(dwError), &m_stConfig, 2);
        return;
    }
    if (!(stDirectory.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (stDirectory.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
        return;
    // 遍历期间不允许当前目录被换成链接；文件读写及子项创建仍然允许。
    CAutoHandle hDirectory(CreateFileW(strDirectory, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL));
    BY_HANDLE_FILE_INFORMATION stOpened;
    if (!hDirectory.Valid() || !GetFileInformationByHandle(hDirectory.Get(), &stOpened))
    {
        DWORD dwError = GetLastError();
        ++unFailed;
        loggerFile.Operation(L"清理旧空文件夹", L"无法打开目录：" + strDirectory + L"；" + WinError(dwError), &m_stConfig, 2);
        return;
    }
    if (!(stOpened.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (stOpened.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
        return;
    {
        WIN32_FIND_DATAW stEntry;
        HANDLE hSearch = FindFirstFileW(JoinPath(strDirectory, L"*"), &stEntry);
        if (hSearch == INVALID_HANDLE_VALUE)
        {
            DWORD dwError = GetLastError();
            if (dwError != ERROR_FILE_NOT_FOUND)
            {
                ++unFailed;
                loggerFile.Operation(L"清理旧空文件夹", L"无法检查目录：" + strDirectory + L"；" + WinError(dwError), &m_stConfig, 2);
                return;
            }
        }
        else
        {
            CFindHandle hFind(hSearch);
            do
            {
                if (Stopped(hStop))
                    return;
                if (!(stEntry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (stEntry.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ||
                    wcscmp(stEntry.cFileName, L".") == 0 || wcscmp(stEntry.cFileName, L"..") == 0)
                    continue;
                if (nDepth < 10)
                    CleanupDirectory(JoinPath(strDirectory, stEntry.cFileName), nDepth + 1, ullCutoff,
                                     loggerFile, hStop, unDeleted, unFailed);
            } while (FindNextFileW(hSearch, &stEntry));
            DWORD dwError = GetLastError();
            if (dwError != ERROR_NO_MORE_FILES)
            {
                ++unFailed;
                loggerFile.Operation(L"清理旧空文件夹", L"目录枚举失败：" + strDirectory + L"；" + WinError(dwError), &m_stConfig, 2);
                return;
            }
        }
    }
    // 子目录删除会改变父目录的修改时间，所以在完成遍历后重新取时间。
    if (nDepth == 0 || Stopped(hStop) ||
        !GetFileAttributesExW(strDirectory, GetFileExInfoStandard, &stDirectory) ||
        !(stDirectory.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (stDirectory.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
        return;
    ULONGLONG ullModified = ((ULONGLONG)stDirectory.ftLastWriteTime.dwHighDateTime << 32) |
                           stDirectory.ftLastWriteTime.dwLowDateTime;
    if (ullModified >= ullCutoff || !PathIsDirectoryEmptyW(strDirectory))
        return;
    hDirectory.Close();
    if (RemoveDirectoryW(strDirectory))
    {
        ++unDeleted;
        loggerFile.Operation(L"删除旧空文件夹", L"已删除：" + strDirectory + L"；最后修改日期=" + FileDate(ullModified, false), &m_stConfig);
    }
    else
    {
        DWORD dwError = GetLastError();
        ++unFailed;
        loggerFile.Operation(L"删除旧空文件夹", L"已跳过：" + strDirectory + L"；" + WinError(dwError), &m_stConfig, 2);
    }
}

// 每次只依据最后修改时间和当前空目录状态判断，不记录连续空置时间。
void CDirScanner::CleanupEmptyDirs(CLogger& loggerFile, HANDLE hStop)
{
    if (!m_stConfig.m_bDeleteOldEmptyDirs || Stopped(hStop))
        return;
    CString strError;
    if (!m_stConfig.Validate(strError))
    {
        loggerFile.Operation(L"清理旧空文件夹", L"参数无效，已跳过：" + strError, &m_stConfig, 2);
        return;
    }
    // 拒绝 C: 这类依赖当前工作目录的驱动器相对路径。
    if (m_stConfig.m_strLocalDir.GetLength() < 3 ||
        (m_stConfig.m_strLocalDir[1] == L':' &&
         m_stConfig.m_strLocalDir[2] != L'\\' && m_stConfig.m_strLocalDir[2] != L'/'))
    {
        loggerFile.Operation(L"清理旧空文件夹", L"监控目录不是完整绝对路径，已跳过", &m_stConfig, 2);
        return;
    }
    CString strStart;
    strStart.Format(L"开始；监控目录=%s；超过=%u 天；依据=文件夹最后修改时间；保留监控根目录",
                    (LPCWSTR)m_stConfig.m_strLocalDir, m_stConfig.m_unEmptyDirAgeDays);
    loggerFile.Operation(L"清理旧空文件夹", strStart, &m_stConfig);
    FILETIME stNow;
    GetSystemTimeAsFileTime(&stNow);
    ULONGLONG ullNow = ((ULONGLONG)stNow.dwHighDateTime << 32) | stNow.dwLowDateTime;
    ULONGLONG ullCutoff = ullNow - (ULONGLONG)m_stConfig.m_unEmptyDirAgeDays * 864000000000ULL;
    UINT unDeleted = 0;
    UINT unFailed = 0;
    CleanupDirectory(m_stConfig.m_strLocalDir, 0, ullCutoff, loggerFile, hStop, unDeleted, unFailed);
    CString strResult;
    strResult.Format(L"%s；删除=%u 个；失败或目录变化跳过=%u 个", Stopped(hStop) ? L"已取消" : L"完成", unDeleted, unFailed);
    loggerFile.Operation(L"清理旧空文件夹", strResult, &m_stConfig, unFailed ? 2 : 1);
}

// 依据稳定时间及历史记录生成有界队列，不在扫描线程中执行网络上传。
bool CDirScanner::Scan(std::deque<PendingFile>& deqQueue, const CUploadHistory& historyUpload,
                       std::map<CString, FailInfo>& mapFails, std::map<CString, PendingFile>& mapPost, HANDLE hStop)
{
    DWORD dwAttrs = GetFileAttributesW(m_stConfig.m_strLocalDir);
    if (dwAttrs == INVALID_FILE_ATTRIBUTES || !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    std::vector<PendingFile> vecFound;
    std::vector<PendingFile> vecReady;
    Walk(m_stConfig.m_strLocalDir, 0, vecFound, hStop);
    std::set<CString> setSeen;
    for (std::map<CString, Candidate>::iterator itI = m_mapCandidates.begin(); itI != m_mapCandidates.end(); ++itI)
        itI->second.m_bSeen = false;
    for (size_t nResult = 0; nResult < vecFound.size(); ++nResult)
    {
        if (Stopped(hStop))
            return true;
        const PendingFile& stF = vecFound[nResult];
        setSeen.insert(stF.m_strFullPath);
        // 已上传文件只补做未完成的本地删除/备份，保留策略直接跳过。
        if (historyUpload.Contains(stF))
        {
            if (m_stConfig.m_nPostAction != POST_KEEP)
                mapPost[stF.m_strFullPath] = stF;
            m_mapCandidates.erase(stF.m_strFullPath);
            continue;
        }
        if (mapPost.count(stF.m_strFullPath) || m_setQueued.count(stF.m_strFullPath))
            continue;
        std::map<CString, FailInfo>::iterator itFailure = mapFails.find(stF.m_strFullPath);
        if (itFailure != mapFails.end() &&
            (itFailure->second.m_bParked || !TickElapsed(itFailure->second.m_dwLastTick, 30000)))
            continue;
        std::map<CString, Candidate>::iterator itOld = m_mapCandidates.find(stF.m_strFullPath);
        // 首次看到或文件版本变化时重新计时，等待连续稳定后再上传。
        if (itOld == m_mapCandidates.end() || !itOld->second.m_stFile.Same(stF))
        {
            Candidate stConfig;
            stConfig.m_stFile = stF;
            stConfig.m_dwSince = GetTickCount();
            stConfig.m_bSeen = true;
            m_mapCandidates[stF.m_strFullPath] = stConfig;
            continue;
        }
        itOld->second.m_bSeen = true;
        if (!TickElapsed(itOld->second.m_dwSince, m_stConfig.m_unStableSec * 1000) ||
            deqQueue.size() + vecReady.size() >= 4096)
            continue;
        CAutoHandle hFile(CreateFileW(stF.m_strFullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                      FILE_FLAG_SEQUENTIAL_SCAN, NULL));
        if (hFile.Valid())
            vecReady.push_back(stF);
    }
    for (std::map<CString, Candidate>::iterator itI = m_mapCandidates.begin(); itI != m_mapCandidates.end();)
    {
        if (!itI->second.m_bSeen)
            m_mapCandidates.erase(itI++);
        else
            ++itI;
    }
    for (std::map<CString, FailInfo>::iterator itI = mapFails.begin(); itI != mapFails.end();)
    {
        if (!setSeen.count(itI->first))
            mapFails.erase(itI++);
        else
            ++itI;
    }
    // 优先安排修改较早的文件，时间相同时按相对路径保持稳定顺序。
    std::sort(vecReady.begin(), vecReady.end(), FileOlder);
    for (size_t nResult = 0; nResult < vecReady.size(); ++nResult)
    {
        deqQueue.push_back(vecReady[nResult]);
        m_setQueued.insert(vecReady[nResult].m_strFullPath);
    }
    return true;
}
