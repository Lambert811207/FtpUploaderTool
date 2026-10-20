#include "stdafx.h"
#include "Config.h"
TaskConfig::TaskConfig() : m_strId(NewId()), m_strName(L"新任务"), m_strHost(L"127.0.0.1"), m_strUser(L"anonymous"), m_strRemoteDir(L"/"),
    m_strIncludeMask(L"*"), m_strExcludeMask(L"*.tmp;*.part;~*"), m_strTempSuffix(L".uploading"), m_bPassive(true),
    m_bIncludeSubDir(false), m_bUseTempName(true), m_bVerifySize(true), m_unPort(21), m_unScanInterval(2), m_unStableSec(3), m_unMaxFileRetry(5),
    m_unMaxFileAgeDays(0), m_unKeepDays(0), m_unConnTimeout(10), m_unIoTimeout(30), m_unHeartbeat(30), m_unReconnMin(3), m_unReconnMax(60), m_unIdleDisconn(0),
    m_nEncoding(ENC_AUTO), m_nPostAction(POST_KEEP), m_nRemoteExist(EXIST_OVERWRITE) {}
static bool Unsafe(const CString& strS) { return strS.FindOneOf(L"\r\n") >= 0; }
bool TaskConfig::Validate(CString& strMessage) const {
    strMessage.Empty(); GUID stGuid; CString strWrapped = L"{" + m_strId + L"}";
    if (FAILED(CLSIDFromString((LPWSTR)(LPCWSTR)strWrapped, &stGuid))) strMessage = L"任务 ID 无效";
    else if (m_strName.IsEmpty() || m_strName.FindOneOf(L"\\/:*?\"<>|\r\n") >= 0) strMessage = L"名称为空或包含非法字符";
    else if (m_strHost.IsEmpty() || m_strHost.FindOneOf(L"/\\\r\n ") >= 0 || Unsafe(m_strUser) || Unsafe(m_strPassword)) strMessage = L"服务器请填写 IP/域名，账号密码不能含换行";
    else if (m_unPort < 1 || m_unPort > 65535) strMessage = L"端口范围为 1–65535";
    else if (m_strLocalDir.IsEmpty() || PathIsRelativeW(m_strLocalDir)) strMessage = L"本地监控目录必须为绝对路径";
    else if (m_strRemoteDir.IsEmpty() || m_strRemoteDir[0] != L'/' || Unsafe(m_strRemoteDir) || m_strRemoteDir.Find(L"..") >= 0) strMessage = L"远程目录需以 / 开头，且不能包含 .. 或换行";
    else if (m_nEncoding < 0 || m_nEncoding > 2 || m_nPostAction < 0 || m_nPostAction > 2 || m_nRemoteExist < 0 || m_nRemoteExist > 2) strMessage = L"策略或编码值无效";
    else if (m_unScanInterval < 1 || m_unStableSec < 1 || m_unMaxFileRetry < 1 || m_unConnTimeout < 1 || m_unIoTimeout < 1 || m_unHeartbeat < 1 || m_unReconnMin < 1 || m_unReconnMin > m_unReconnMax || m_unReconnMax > 86400 || m_unScanInterval > 86400 || m_unStableSec > 86400 || m_unConnTimeout > 86400 || m_unIoTimeout > 86400 || m_unHeartbeat > 86400 || m_unIdleDisconn > 86400) strMessage = L"间隔需为 1–86400 秒，重连最小值不能大于最大值";
    else if (m_unMaxFileRetry > 1000 || m_unMaxFileAgeDays > 36500 || m_unKeepDays > 36500) strMessage = L"重试最多 1000 次；保留天数最多 36500 天";
    else if (m_bUseTempName && (m_strTempSuffix.IsEmpty() || m_strTempSuffix.FindOneOf(L"/\\\r\n") >= 0)) strMessage = L"临时文件后缀无效";
    else if (m_nPostAction == POST_MOVE && (m_strBackupDir.IsEmpty() || PathIsRelativeW(m_strBackupDir) || IsWithin(m_strLocalDir, m_strBackupDir))) strMessage = L"备份目录必须为绝对路径，且不能位于监控目录内";
    else if (!m_strError.IsEmpty()) strMessage = m_strError;
    return strMessage.IsEmpty();
}
static CString ReadString(const CString& strPath, LPCWSTR pszSection, LPCWSTR pszKey, const CString& strValue) {
    WCHAR szBuffer[32768] = {0}; GetPrivateProfileStringW(pszSection, pszKey, strValue, szBuffer, _countof(szBuffer), strPath); return CString(szBuffer);
}
static UINT ReadNumber(const CString& strPath, LPCWSTR pszSection, LPCWSTR pszKey, UINT unValue, UINT unMinValue, UINT unMaxValue) {
    CString strText = ReadString(strPath, pszSection, pszKey, L""); if (strText.IsEmpty()) return unValue;
    WCHAR* pEnd = NULL; unsigned long ulN = wcstoul(strText, &pEnd, 10);
    return pEnd && *pEnd == 0 && ulN >= unMinValue && ulN <= unMaxValue ? (UINT)ulN : unValue;
}
bool CConfigStore::Load(const CString& strPath, GlobalConfig& stGlobal, std::vector<TaskConfig>& vecConfigs, CString& strMessage) {
    vecConfigs.clear(); strMessage.Empty(); CString strSource(strPath);
    if (ReadString(strSource, L"Global", L"Version", L"") != L"1") {
        strSource += L".bak";
        if (ReadString(strSource, L"Global", L"Version", L"") != L"1") {
            if (!PathFileExistsW(strPath) && !PathFileExistsW(strSource)) return true;
            strMessage = L"配置及备份均无效，未加载任何任务"; return false;
        }
        strMessage = L"主配置无效，已读取 .bak 备份";
    }
    bool bLegacyConfig = ReadString(strSource, L"Global", L"TaskCount", L"").IsEmpty();
    int nCount = (int)ReadNumber(strSource, L"Global", bLegacyConfig ? L"ChannelCount" : L"TaskCount", 999, 0, 32);
    if (nCount > 32) { strMessage = L"任务数量必须为 0–32"; return false; }
    stGlobal.m_bAutoStart = ReadNumber(strSource, L"Global", L"AutoStart", 0, 0, 1) != 0;
    stGlobal.m_bStartMinimized = ReadNumber(strSource, L"Global", L"StartMinimized", 0, 0, 1) != 0;
    stGlobal.m_bPreventSleep = ReadNumber(strSource, L"Global", L"PreventSleep", 1, 0, 1) != 0;
    stGlobal.m_unLogKeepDays = ReadNumber(strSource, L"Global", L"LogKeepDays", 30, 1, 36500);
    stGlobal.m_unBackupKeepDays = ReadNumber(strSource, L"Global", L"BackupKeepDays", 30, 0, 36500);
    stGlobal.m_unUiMaxLines = ReadNumber(strSource, L"Global", L"UiMaxLogLines", 2000, 100, 10000);
    bool bMigrate = false; std::set<CString> setIds;
    for (int nIndex = 0; nIndex < nCount; ++nIndex) {
        CString strKey; strKey.Format(bLegacyConfig ? L"Channel%d" : L"Task%d", nIndex); TaskConfig stConfig;
        stConfig.m_strId = ReadString(strSource, L"Global", strKey, L""); CString strSection = (bLegacyConfig ? L"Ch_" : L"Task_") + stConfig.m_strId;
        if (stConfig.m_strId.IsEmpty() || setIds.count(stConfig.m_strId)) { strMessage = L"任务 ID 缺失或重复"; return false; } setIds.insert(stConfig.m_strId);
#define STR(field, keyName) stConfig.field = ReadString(strSource, strSection, L##keyName, stConfig.field)
#define NUM(field, keyName, low, high) stConfig.field = ReadNumber(strSource, strSection, L##keyName, stConfig.field, low, high)
#define FLAG(field, keyName) stConfig.field = ReadNumber(strSource, strSection, L##keyName, stConfig.field ? 1 : 0, 0, 1) != 0
        STR(m_strName, "Name"); STR(m_strHost, "Host"); STR(m_strUser, "User"); STR(m_strLocalDir, "LocalDir"); STR(m_strRemoteDir, "RemoteDir"); STR(m_strBackupDir, "BackupDir");
        STR(m_strIncludeMask, "IncludeMask"); STR(m_strExcludeMask, "ExcludeMask"); STR(m_strTempSuffix, "TempSuffix"); STR(m_strEncryptedPassword, "Password");
        FLAG(m_bPassive, "Passive"); FLAG(m_bIncludeSubDir, "IncludeSubDir"); FLAG(m_bUseTempName, "UseTempName"); FLAG(m_bVerifySize, "VerifySize");
        NUM(m_unPort, "Port", 1, 65535); NUM(m_nEncoding, "Encoding", 0, 2); NUM(m_nPostAction, "PostAction", 0, 2); NUM(m_nRemoteExist, "RemoteExist", 0, 2);
        NUM(m_unScanInterval, "ScanInterval", 1, 86400); NUM(m_unStableSec, "StableSec", 1, 86400); NUM(m_unMaxFileRetry, "MaxFileRetry", 1, 1000);
        NUM(m_unMaxFileAgeDays, "MaxFileAgeDays", 0, 36500); NUM(m_unKeepDays, "KeepDays", 0, 36500); NUM(m_unConnTimeout, "ConnTimeout", 1, 86400);
        NUM(m_unIoTimeout, "IoTimeout", 1, 86400); NUM(m_unHeartbeat, "Heartbeat", 1, 86400); NUM(m_unReconnMin, "ReconnMin", 1, 86400); NUM(m_unReconnMax, "ReconnMax", 1, 86400); NUM(m_unIdleDisconn, "IdleDisconn", 0, 86400);
#undef STR
#undef NUM
#undef FLAG
        CString strPlain = ReadString(strSource, strSection, L"PasswordPlain", L"");
        if (!strPlain.IsEmpty()) { stConfig.m_strPassword = strPlain; stConfig.m_strEncryptedPassword = EncryptPassword(strPlain); if (stConfig.m_strEncryptedPassword.IsEmpty()) stConfig.m_strError = L"密码加密失败"; else bMigrate = true; }
        else if (!DecryptPassword(stConfig.m_strEncryptedPassword, stConfig.m_strPassword)) stConfig.m_strError = L"密码无效，请重新输入";
        vecConfigs.push_back(stConfig);
    }
    if (bMigrate) { CString strFailure; if (!Save(strPath, stGlobal, vecConfigs, strFailure, true)) { strMessage = strFailure; return false; } }
    return true;
}
bool CConfigStore::Save(const CString& strPath, const GlobalConfig& stGlobal, const std::vector<TaskConfig>& vecConfigs, CString& strMessage, bool bSanitizeBackup) {
    strMessage.Empty(); if (vecConfigs.size() > 32) { strMessage = L"任务最多 32 个"; return false; }
    CString strText(L"[Global]\r\nVersion=1\r\n"), strLine;
#define WNUM(key, val) strLine.Format(L##key L"=%u\r\n", (UINT)(val)); strText += strLine
#define WSTR(key, val) strText += CString(L##key L"=") + (val) + L"\r\n"
    WNUM("AutoStart", stGlobal.m_bAutoStart); WNUM("StartMinimized", stGlobal.m_bStartMinimized); WNUM("PreventSleep", stGlobal.m_bPreventSleep);
    WNUM("LogKeepDays", stGlobal.m_unLogKeepDays); WNUM("BackupKeepDays", stGlobal.m_unBackupKeepDays); WNUM("UiMaxLogLines", stGlobal.m_unUiMaxLines); WNUM("TaskCount", vecConfigs.size());
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex) { strLine.Format(L"Task%u=%s\r\n", (UINT)nIndex, (LPCWSTR)vecConfigs[nIndex].m_strId); strText += strLine; }
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex) {
        const TaskConfig& stConfig = vecConfigs[nIndex]; strText += L"\r\n[Task_" + stConfig.m_strId + L"]\r\n";
        CString strEncrypted = stConfig.m_strPassword.IsEmpty() ? stConfig.m_strEncryptedPassword : EncryptPassword(stConfig.m_strPassword);
        if (!stConfig.m_strPassword.IsEmpty() && strEncrypted.IsEmpty()) { strMessage = L"DPAPI 密码加密失败"; return false; }
        WSTR("Name", stConfig.m_strName); WSTR("Host", stConfig.m_strHost); WNUM("Port", stConfig.m_unPort); WSTR("User", stConfig.m_strUser); WSTR("Password", strEncrypted); WSTR("PasswordPlain", L"");
        WNUM("Passive", stConfig.m_bPassive); WNUM("Encoding", stConfig.m_nEncoding); WSTR("LocalDir", stConfig.m_strLocalDir); WSTR("RemoteDir", stConfig.m_strRemoteDir); WNUM("IncludeSubDir", stConfig.m_bIncludeSubDir);
        WSTR("IncludeMask", stConfig.m_strIncludeMask); WSTR("ExcludeMask", stConfig.m_strExcludeMask); WNUM("ScanInterval", stConfig.m_unScanInterval); WNUM("StableSec", stConfig.m_unStableSec);
        WNUM("PostAction", stConfig.m_nPostAction); WSTR("BackupDir", stConfig.m_strBackupDir); WNUM("RemoteExist", stConfig.m_nRemoteExist); WNUM("UseTempName", stConfig.m_bUseTempName); WSTR("TempSuffix", stConfig.m_strTempSuffix);
        WNUM("VerifySize", stConfig.m_bVerifySize); WNUM("MaxFileRetry", stConfig.m_unMaxFileRetry); WNUM("MaxFileAgeDays", stConfig.m_unMaxFileAgeDays); WNUM("KeepDays", stConfig.m_unKeepDays);
        WNUM("ConnTimeout", stConfig.m_unConnTimeout); WNUM("IoTimeout", stConfig.m_unIoTimeout); WNUM("Heartbeat", stConfig.m_unHeartbeat); WNUM("ReconnMin", stConfig.m_unReconnMin); WNUM("ReconnMax", stConfig.m_unReconnMax); WNUM("IdleDisconn", stConfig.m_unIdleDisconn);
    }
#undef WNUM
#undef WSTR
    CString strDir(strPath); strDir = strDir.Left(strDir.ReverseFind(L'\\'));
    if (!MakeDirs(strDir)) { strMessage = L"无法创建配置目录"; return false; }
    CString strTemp = strPath + L".tmp", strBackup = strPath + L".bak";
    CAutoHandle hFile(CreateFileW(strTemp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    WORD wBom = 0xFEFF;
    if (!hFile.Valid() || !WriteBytes(hFile.Get(), &wBom, 2) || !WriteBytes(hFile.Get(), (LPCWSTR)strText, strText.GetLength() * sizeof(WCHAR)) || !FlushFileBuffers(hFile.Get())) { strMessage = L"写入配置失败"; return false; }
    hFile.Close();
    if (bSanitizeBackup) { if (!CopyFileW(strTemp, strBackup, FALSE)) { strMessage = L"清理明文备份失败"; return false; } }
    else if (PathFileExistsW(strPath) && !CopyFileW(strPath, strBackup, FALSE)) { strMessage = L"备份配置失败"; return false; }
    if (!MoveFileExW(strTemp, strPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { strMessage = L"替换配置失败：" + WinError(GetLastError()); return false; }
    if (!PathFileExistsW(strBackup)) CopyFileW(strPath, strBackup, FALSE);
    return true;
}
