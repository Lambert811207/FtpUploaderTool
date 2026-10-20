#include "stdafx.h"
#include "Files.h"
bool StatFile(const CString& strRoot, const CString& strPath, PendingFile& stFile) {
    WIN32_FILE_ATTRIBUTE_DATA stData; if (!GetFileAttributesExW(strPath, GetFileExInfoStandard, &stData) || stData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    stFile.m_strFullPath = strPath; stFile.m_strRelative = strPath.Mid(strRoot.GetLength()); stFile.m_strRelative.TrimLeft(L"\\/");
    int nSlash = stFile.m_strRelative.ReverseFind(L'\\'); stFile.m_strName = nSlash < 0 ? stFile.m_strRelative : stFile.m_strRelative.Mid(nSlash + 1); stFile.m_strSubDir = nSlash < 0 ? CString() : stFile.m_strRelative.Left(nSlash);
    stFile.m_ullSize = ((ULONGLONG)stData.nFileSizeHigh << 32) | stData.nFileSizeLow;
    stFile.m_ullModified = ((ULONGLONG)stData.ftLastWriteTime.dwHighDateTime << 32) | stData.ftLastWriteTime.dwLowDateTime; return true;
}
static SYSTEMTIME LocalModified(ULONGLONG ullModified) {
    ULARGE_INTEGER stValue; stValue.QuadPart = ullModified; FILETIME stFileTime = {stValue.LowPart, stValue.HighPart}, stLocal; SYSTEMTIME stTime = {0};
    FileTimeToLocalFileTime(&stFileTime, &stLocal); FileTimeToSystemTime(&stLocal, &stTime); return stTime;
}
CString FileDate(ULONGLONG ullModified, bool bCompact) {
    SYSTEMTIME stTime = LocalModified(ullModified); CString strText;
    strText.Format(bCompact ? L"%04u%02u%02u" : L"%04u-%02u-%02u", stTime.wYear, stTime.wMonth, stTime.wDay); return strText;
}
CString RemoteDirectory(const TaskConfig& stConfig, const PendingFile& stFile) {
    CString strResult(stConfig.m_strRemoteDir), strValue, strSub(stFile.m_strSubDir); strSub.Replace(L'\\', L'/'); SYSTEMTIME stTime = LocalModified(stFile.m_ullModified);
    strValue.Format(L"%04u", stTime.wYear); strResult.Replace(L"{yyyy}", strValue); strValue.Format(L"%02u", stTime.wMonth); strResult.Replace(L"{MM}", strValue);
    strValue.Format(L"%02u", stTime.wDay); strResult.Replace(L"{dd}", strValue); strValue.Format(L"%02u", stTime.wHour); strResult.Replace(L"{HH}", strValue);
    WCHAR szPc[MAX_COMPUTERNAME_LENGTH + 1]; DWORD dwN = _countof(szPc); GetComputerNameW(szPc, &dwN); strResult.Replace(L"{pc}", szPc); strResult.Replace(L"{sub}", strSub);
    if (stConfig.m_bIncludeSubDir && stConfig.m_strRemoteDir.Find(L"{sub}") < 0 && !strSub.IsEmpty()) { strResult.TrimRight(L"/"); strResult += L"/" + strSub; }
    return strResult;
}
bool CUploadHistory::Load(const CString& strPath) {
    m_strPath = strPath; m_mapRecords.clear(); if (!PathFileExistsW(strPath)) return true;
    CStdioFile fileFile; CFileException stError;
    if (!fileFile.Open(strPath, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, &stError)) return false;
    CStringA strALine; char szBuffer[4096]; UINT unCount;
    while ((unCount = fileFile.Read(szBuffer, sizeof(szBuffer))) > 0) {
        strALine.Append(szBuffer, unCount); int nNewline;
        while ((nNewline = strALine.Find('\n')) >= 0) {
            CString strText = FromBytes(strALine.Left(nNewline)); strALine.Delete(0, nNewline + 1); int nA = strText.Find(L'|'), nB = strText.Find(L'|', nA + 1);
            if (nA < 1 || nB < nA + 2) continue;
            ULONGLONG ullSize = 0, ullModified = 0; if (swscanf_s(strText.Mid(nA + 1, nB - nA - 1), L"%I64u", &ullSize) == 1 && swscanf_s(strText.Mid(nB + 1), L"%I64x", &ullModified) == 1)
                m_mapRecords[strText.Left(nA)] = std::make_pair(ullSize, ullModified);
        }
        if (strALine.GetLength() > 65536) strALine.Empty();
    }
    return true;
}
bool CUploadHistory::Contains(const PendingFile& stFile) const {
    std::map<CString, std::pair<ULONGLONG, ULONGLONG> >::const_iterator itI = m_mapRecords.find(stFile.m_strRelative);
    return itI != m_mapRecords.end() && itI->second.first == stFile.m_ullSize && itI->second.second == stFile.m_ullModified;
}
bool CUploadHistory::Add(const PendingFile& stFile) {
    CString strLine; strLine.Format(L"\n%s|%I64u|%016I64X|%s\n", (LPCWSTR)stFile.m_strRelative, stFile.m_ullSize, stFile.m_ullModified, (LPCWSTR)TimeText());
    CStringA strABytes = ToBytes(strLine); CAutoHandle hOutput(CreateFileW(m_strPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (!hOutput.Valid() || !WriteBytes(hOutput.Get(), (LPCSTR)strABytes, strABytes.GetLength()) || !FlushFileBuffers(hOutput.Get())) return false;
    m_mapRecords[stFile.m_strRelative] = std::make_pair(stFile.m_ullSize, stFile.m_ullModified); return true;
}
bool CUploadHistory::Compact(const CString& strRoot) {
    CStringA strAText;
    for (std::map<CString, std::pair<ULONGLONG, ULONGLONG> >::iterator itI = m_mapRecords.begin(); itI != m_mapRecords.end();) {
        CString strPath = JoinPath(strRoot, itI->first);
        if (!IsWithin(strRoot, strPath) || !PathFileExistsW(strPath)) { m_mapRecords.erase(itI++); continue; }
        CString strLine; strLine.Format(L"%s|%I64u|%016I64X|%s\n", (LPCWSTR)itI->first, itI->second.first, itI->second.second, (LPCWSTR)TimeText()); strAText += ToBytes(strLine); ++itI;
    }
    return AtomicWrite(m_strPath, strAText);
}
bool CDirScanner::Match(const CString& strName, const CString& strMasks, bool bEmpty) const {
    if (strMasks.IsEmpty()) return bEmpty;
    int nStart = 0; CString strMask; while (!(strMask = strMasks.Tokenize(L";", nStart)).IsEmpty()) { strMask.Trim(); if (PathMatchSpecW(strName, strMask)) return true; }
    return false;
}
void CDirScanner::Walk(const CString& strDirectory, int nDepth, std::vector<PendingFile>& vecFound, HANDLE hStop) {
    WIN32_FIND_DATAW stFd; HANDLE hHandle = FindFirstFileW(JoinPath(strDirectory, L"*"), &stFd); if (hHandle == INVALID_HANDLE_VALUE) return; CFindHandle hFindFind(hHandle);
    do {
        if (Stopped(hStop)) return;
        if (stFd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (wcscmp(stFd.cFileName, L".") == 0 || wcscmp(stFd.cFileName, L"..") == 0) continue;
        CString strPath = JoinPath(strDirectory, stFd.cFileName);
        if (stFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (m_stConfig.m_bIncludeSubDir && nDepth < 10) Walk(strPath, nDepth + 1, vecFound, hStop); }
        else if (Match(stFd.cFileName, m_stConfig.m_strIncludeMask, true) && !Match(stFd.cFileName, m_stConfig.m_strExcludeMask, false) &&
            (!m_stConfig.m_bUseTempName || CString(stFd.cFileName).Right(m_stConfig.m_strTempSuffix.GetLength()).CompareNoCase(m_stConfig.m_strTempSuffix) != 0)) {
            PendingFile stFile; if (StatFile(m_stConfig.m_strLocalDir, strPath, stFile)) vecFound.push_back(stFile);
        }
    } while (FindNextFileW(hHandle, &stFd));
}
static bool FileOlder(const PendingFile& stA, const PendingFile& stB) { return stA.m_ullModified < stB.m_ullModified || (stA.m_ullModified == stB.m_ullModified && stA.m_strRelative < stB.m_strRelative); }
bool CDirScanner::Scan(std::deque<PendingFile>& deqQueue, const CUploadHistory& historyUpload, std::map<CString, FailInfo>& mapFails, std::map<CString, PendingFile>& mapPost, HANDLE hStop) {
    DWORD dwAttrs = GetFileAttributesW(m_stConfig.m_strLocalDir); if (dwAttrs == INVALID_FILE_ATTRIBUTES || !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    std::vector<PendingFile> vecFound, vecReady; Walk(m_stConfig.m_strLocalDir, 0, vecFound, hStop); std::set<CString> setSeen;
    for (std::map<CString, Candidate>::iterator itI = m_mapCandidates.begin(); itI != m_mapCandidates.end(); ++itI) itI->second.m_bSeen = false;
    FILETIME stFileTime; GetSystemTimeAsFileTime(&stFileTime); ULONGLONG ullNow = ((ULONGLONG)stFileTime.dwHighDateTime << 32) | stFileTime.dwLowDateTime;
    for (size_t nResult = 0; nResult < vecFound.size(); ++nResult) {
        if (Stopped(hStop)) return true; const PendingFile& stF = vecFound[nResult]; setSeen.insert(stF.m_strFullPath);
        if (historyUpload.Contains(stF)) { if (m_stConfig.m_nPostAction != POST_KEEP || (m_stConfig.m_unKeepDays && ullNow > stF.m_ullModified && (ullNow - stF.m_ullModified) / 864000000000ULL >= m_stConfig.m_unKeepDays)) mapPost[stF.m_strFullPath] = stF; m_mapCandidates.erase(stF.m_strFullPath); continue; }
        if (m_stConfig.m_unMaxFileAgeDays && ullNow > stF.m_ullModified && (ullNow - stF.m_ullModified) / 864000000000ULL > m_stConfig.m_unMaxFileAgeDays) continue;
        if (mapPost.count(stF.m_strFullPath) || m_setQueued.count(stF.m_strFullPath)) continue;
        std::map<CString, FailInfo>::iterator itFailure = mapFails.find(stF.m_strFullPath);
        if (itFailure != mapFails.end() && (itFailure->second.m_bParked || !TickElapsed(itFailure->second.m_dwLastTick, 30000))) continue;
        std::map<CString, Candidate>::iterator itOld = m_mapCandidates.find(stF.m_strFullPath);
        if (itOld == m_mapCandidates.end() || !itOld->second.m_stFile.Same(stF)) { Candidate stConfig; stConfig.m_stFile = stF; stConfig.m_dwSince = GetTickCount(); stConfig.m_bSeen = true; m_mapCandidates[stF.m_strFullPath] = stConfig; continue; }
        itOld->second.m_bSeen = true;
        if (!TickElapsed(itOld->second.m_dwSince, m_stConfig.m_unStableSec * 1000) || deqQueue.size() + vecReady.size() >= 4096) continue;
        CAutoHandle hFile(CreateFileW(stF.m_strFullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
        if (hFile.Valid()) vecReady.push_back(stF);
    }
    for (std::map<CString, Candidate>::iterator itI = m_mapCandidates.begin(); itI != m_mapCandidates.end();) { if (!itI->second.m_bSeen) m_mapCandidates.erase(itI++); else ++itI; }
    for (std::map<CString, FailInfo>::iterator itI = mapFails.begin(); itI != mapFails.end();) { if (!setSeen.count(itI->first)) mapFails.erase(itI++); else ++itI; }
    std::sort(vecReady.begin(), vecReady.end(), FileOlder);
    for (size_t nResult = 0; nResult < vecReady.size(); ++nResult) { deqQueue.push_back(vecReady[nResult]); m_setQueued.insert(vecReady[nResult].m_strFullPath); }
    return true;
}
