#include "stdafx.h"
#include "Config.h"

TaskConfig::TaskConfig()
    : m_strId(NewId()),
      m_strName(L"新任务"),
      m_strHost(L"127.0.0.1"),
      m_strUser(L"anonymous"),
      m_strRemoteDir(L"/"),
      m_strIncludeMask(L"*"),
      m_strExcludeMask(L""),
      m_strTempSuffix(L".uploading"),
      m_bPassive(true),
      m_bSelected(true),
      m_bIncludeSubDir(true),
      m_bDeleteOldEmptyDirs(false),
      m_bUseTempName(true),
      m_bVerifySize(true),
      m_unTaskNumber(0),
      m_unPort(21),
      m_unScanInterval(2),
      m_unUploadIntervalMs(0),
      m_unStableSec(3),
      m_unMaxFileRetry(5),
      m_unEmptyDirAgeDays(7),
      m_unConnTimeout(10),
      m_unIoTimeout(30),
      m_unHeartbeat(30),
      m_unReconnMin(3),
      m_unReconnMax(60),
      m_unIdleDisconn(0),
      m_nEncoding(ENC_AUTO),
      m_nPostAction(POST_DELETE),
      m_nRemoteExist(EXIST_OVERWRITE)
{
}

static bool Unsafe(const CString& strS)
{
    return strS.FindOneOf(L"\r\n") >= 0;
}

// 按界面参数顺序校验，只返回第一项错误，避免混入其他参数警告。
bool TaskConfig::Validate(CString& strMessage) const
{
    strMessage.Empty();
    GUID stGuid;
    CString strWrapped = L"{" + m_strId + L"}";
    if (FAILED(CLSIDFromString((LPWSTR)(LPCWSTR)strWrapped, &stGuid)))
        strMessage = L"任务 ID 无效";
    else if (m_strName.IsEmpty() || m_strName.FindOneOf(L"\\/:*?\"<>|\r\n") >= 0)
        strMessage = L"任务名称为空或包含非法字符";
    else if (m_strHost.IsEmpty() || m_strHost.FindOneOf(L"/\\\r\n ") >= 0 || Unsafe(m_strUser) || Unsafe(m_strPassword))
        strMessage = L"服务器请填写 IP/域名，账号密码不能含换行";
    else if (m_unPort < ConfigLimits::m_unPortMin || m_unPort > ConfigLimits::m_unPortMax)
        strMessage = L"FTP 端口必须为 1–65535";
    else if (m_strLocalDir.IsEmpty() || PathIsRelativeW(m_strLocalDir))
        strMessage = L"本地监控目录必须为绝对路径";
    else if (m_strRemoteDir.IsEmpty() || Unsafe(m_strRemoteDir) ||
             m_strRemoteDir.Find(L"..") >= 0)
        strMessage = L"服务器上传目录不能为空，且不能包含 .. 或换行；不以 / 开头表示相对于 FTP 登录目录";
    else if (m_nEncoding < 0 || m_nEncoding > 2)
        strMessage = L"文件名编码无效，请重新选择";
    else if (m_nPostAction < 0 || m_nPostAction > 2)
        strMessage = L"上传成功后的本地文件处理无效，请重新选择";
    else if (m_nRemoteExist < 0 || m_nRemoteExist > 2)
        strMessage = L"服务器同名文件处理无效，请重新选择";
    else if (m_unScanInterval < ConfigLimits::m_unIntervalMin || m_unScanInterval > ConfigLimits::m_unIntervalMax)
        strMessage = L"本地文件扫描间隔必须为 1–86400 秒";
    else if (m_unUploadIntervalMs > ConfigLimits::m_unUploadIntervalMsMax)
        strMessage = L"每个文件上传间隔必须为 0–86400000 毫秒（0 表示连续上传）";
    else if (m_unStableSec < ConfigLimits::m_unIntervalMin || m_unStableSec > ConfigLimits::m_unIntervalMax)
        strMessage = L"上传前文件不变时间必须为 1–86400 秒";
    else if (m_unMaxFileRetry < ConfigLimits::m_unRetryMin || m_unMaxFileRetry > ConfigLimits::m_unRetryMax)
        strMessage = L"单文件失败上限必须为 1–1000 次";
    else if (m_bDeleteOldEmptyDirs &&
             (m_unEmptyDirAgeDays < ConfigLimits::m_unEmptyDirDaysMin || m_unEmptyDirAgeDays > ConfigLimits::m_unEmptyDirDaysMax))
        strMessage = L"空文件夹删除天数必须为 1–36500 天";
    else if (m_unConnTimeout < ConfigLimits::m_unIntervalMin || m_unConnTimeout > ConfigLimits::m_unIntervalMax)
        strMessage = L"连接建立超时必须为 1–86400 秒";
    else if (m_unIoTimeout < ConfigLimits::m_unIntervalMin || m_unIoTimeout > ConfigLimits::m_unIntervalMax)
        strMessage = L"数据收发超时必须为 1–86400 秒";
    else if (m_unHeartbeat < ConfigLimits::m_unIntervalMin || m_unHeartbeat > ConfigLimits::m_unIntervalMax)
        strMessage = L"连接状态检查间隔必须为 1–86400 秒";
    else if (m_unReconnMin < ConfigLimits::m_unIntervalMin || m_unReconnMin > ConfigLimits::m_unIntervalMax)
        strMessage = L"初始重连间隔必须为 1–86400 秒";
    else if (m_unReconnMax < ConfigLimits::m_unIntervalMin || m_unReconnMax > ConfigLimits::m_unIntervalMax)
        strMessage = L"最大重连间隔必须为 1–86400 秒";
    else if (m_unReconnMin > m_unReconnMax)
        strMessage = L"初始重连间隔不能大于最大重连间隔";
    else if (m_unIdleDisconn > ConfigLimits::m_unIntervalMax)
        strMessage = L"空闲断开时间必须为 0–86400 秒（0 表示不主动断开）";
    else if (m_bUseTempName && (m_strTempSuffix.IsEmpty() || m_strTempSuffix.FindOneOf(L"/\\\r\n") >= 0))
        strMessage = L"临时文件名后缀无效";
    else if (m_nPostAction == POST_MOVE &&
             (m_strBackupDir.IsEmpty() || PathIsRelativeW(m_strBackupDir) || IsWithin(m_strLocalDir, m_strBackupDir)))
        strMessage = L"本地备份目录必须为绝对路径，且不能位于监控目录内";
    else if (!m_strError.IsEmpty())
        strMessage = m_strError;
    return strMessage.IsEmpty();
}

bool GlobalConfig::Validate(CString& strMessage) const
{
    strMessage.Empty();
    if (m_unLogMaxSizeMb < ConfigLimits::m_unLogSizeMin || m_unLogMaxSizeMb > ConfigLimits::m_unLogSizeMax)
        strMessage = L"单个日志大小上限必须为 1–1024 MB";
    else if (m_unLogArchiveCount < ConfigLimits::m_unArchiveMin || m_unLogArchiveCount > ConfigLimits::m_unArchiveMax)
        strMessage = L"归档文件上限必须为 1–1000 个";
    else if (m_unUiMaxLines < ConfigLimits::m_unUiLinesMin || m_unUiMaxLines > ConfigLimits::m_unUiLinesMax)
        strMessage = L"界面日志行数上限必须为 100–10000 行";
    return strMessage.IsEmpty();
}

static CString ReadString(const CString& strPath, LPCWSTR pszSection, LPCWSTR pszKey, const CString& strValue)
{
    WCHAR szBuffer[32768] = {0};
    GetPrivateProfileStringW(pszSection, pszKey, strValue, szBuffer, _countof(szBuffer), strPath);
    return CString(szBuffer);
}

// INI 中的缺失或越界值回退默认值，保留旧配置的兼容加载行为。
static UINT ReadNumber(const CString& strPath, LPCWSTR pszSection, LPCWSTR pszKey, UINT unValue, UINT unMinValue,
                       UINT unMaxValue)
{
    CString strText = ReadString(strPath, pszSection, pszKey, L"");
    if (strText.IsEmpty())
        return unValue;
    WCHAR* pEnd = NULL;
    unsigned long ulN = wcstoul(strText, &pEnd, 10);
    return pEnd && *pEnd == 0 && ulN >= unMinValue && ulN <= unMaxValue ? (UINT)ulN : unValue;
}

// 读取主配置或备份，兼容旧通道字段并迁移任务编号及旧密码。
bool CConfigStore::Load(const CString& strPath, GlobalConfig& stGlobal, std::vector<TaskConfig>& vecConfigs,
                        CString& strMessage)
{
    vecConfigs.clear();
    strMessage.Empty();
    CString strSource(strPath);
    if (ReadString(strSource, L"Global", L"Version", L"") != L"1")
    {
        strSource += L".bak";
        if (ReadString(strSource, L"Global", L"Version", L"") != L"1")
        {
            if (!PathFileExistsW(strPath) && !PathFileExistsW(strSource))
                return true;
            strMessage = L"配置及备份均无效，未加载任何任务";
            return false;
        }
        strMessage = L"主配置无效，已读取 .bak 备份";
    }
    // 仅检测存储格式版本，界面和内存仍统一使用任务概念。
    bool bLegacyConfig = ReadString(strSource, L"Global", L"TaskCount", L"").IsEmpty();
    int nCount = (int)ReadNumber(strSource, L"Global", bLegacyConfig ? L"ChannelCount" : L"TaskCount", 999, 0, m_unMaxTasks);
    if (nCount > (int)m_unMaxTasks)
    {
        strMessage = L"任务数量必须为 0–32";
        return false;
    }
    stGlobal.m_bAutoStart = ReadNumber(strSource, L"Global", L"AutoStart", 0, 0, 1) != 0;
    stGlobal.m_bStartMinimized = ReadNumber(strSource, L"Global", L"StartMinimized", 0, 0, 1) != 0;
    stGlobal.m_bPreventSleep = ReadNumber(strSource, L"Global", L"PreventSleep", 1, 0, 1) != 0;
    stGlobal.m_unLogMaxSizeMb = ReadNumber(strSource, L"Global", L"LogMaxSizeMB", 20, ConfigLimits::m_unLogSizeMin, ConfigLimits::m_unLogSizeMax);
    stGlobal.m_unLogArchiveCount = ReadNumber(strSource, L"Global", L"LogArchiveCount", 10, ConfigLimits::m_unArchiveMin, ConfigLimits::m_unArchiveMax);
    stGlobal.m_unUiMaxLines = ReadNumber(strSource, L"Global", L"UiMaxLogLines", 2000, ConfigLimits::m_unUiLinesMin, ConfigLimits::m_unUiLinesMax);
    stGlobal.m_unNextTaskNumber = ReadNumber(strSource, L"Global", L"NextTaskNumber", 1, 1, 1000000000);
    bool bMigrate = false;
    bool bNumberMigration = false;
    std::set<CString> setIds;
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
    {
        CString strKey;
        strKey.Format(bLegacyConfig ? L"Channel%d" : L"Task%d", nIndex);
        TaskConfig stConfig;
        stConfig.m_strId = ReadString(strSource, L"Global", strKey, L"");
        CString strSection = (bLegacyConfig ? L"Ch_" : L"Task_") + stConfig.m_strId;
        if (stConfig.m_strId.IsEmpty() || setIds.count(stConfig.m_strId))
        {
            strMessage = L"任务 ID 缺失或重复";
            return false;
        }
        setIds.insert(stConfig.m_strId);
#define STR(field, keyName) stConfig.field = ReadString(strSource, strSection, L##keyName, stConfig.field)
#define NUM(field, keyName, low, high)                                                                                 \
    stConfig.field = ReadNumber(strSource, strSection, L##keyName, stConfig.field, low, high)
#define FLAG(field, keyName)                                                                                           \
    stConfig.field = ReadNumber(strSource, strSection, L##keyName, stConfig.field ? 1 : 0, 0, 1) != 0
        STR(m_strName, "Name");
        STR(m_strHost, "Host");
        STR(m_strUser, "User");
        STR(m_strLocalDir, "LocalDir");
        STR(m_strRemoteDir, "RemoteDir");
        STR(m_strBackupDir, "BackupDir");
        STR(m_strIncludeMask, "IncludeMask");
        STR(m_strExcludeMask, "ExcludeMask");
        STR(m_strTempSuffix, "TempSuffix");
        STR(m_strPassword, "PasswordPlain");
        FLAG(m_bPassive, "Passive");
        FLAG(m_bSelected, "Selected");
        FLAG(m_bIncludeSubDir, "IncludeSubDir");
        FLAG(m_bDeleteOldEmptyDirs, "DeleteOldEmptyDirs");
        FLAG(m_bUseTempName, "UseTempName");
        FLAG(m_bVerifySize, "VerifySize");
        NUM(m_unPort, "Port", ConfigLimits::m_unPortMin, ConfigLimits::m_unPortMax);
        NUM(m_unTaskNumber, "TaskNumber", 0, 999999999);
        NUM(m_nEncoding, "Encoding", 0, 2);
        NUM(m_nPostAction, "PostAction", 0, 2);
        NUM(m_nRemoteExist, "RemoteExist", 0, 2);
        NUM(m_unScanInterval, "ScanInterval", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        // 新键按毫秒读取；旧键按秒换算，保留已有配置的实际等待时长。
        if (ReadString(strSource, strSection, L"UploadIntervalMs", L"").IsEmpty())
            stConfig.m_unUploadIntervalMs =
                ReadNumber(strSource, strSection, L"UploadInterval", 0, 0, ConfigLimits::m_unIntervalMax) * 1000;
        else
            NUM(m_unUploadIntervalMs, "UploadIntervalMs", 0, ConfigLimits::m_unUploadIntervalMsMax);
        NUM(m_unStableSec, "StableSec", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        NUM(m_unMaxFileRetry, "MaxFileRetry", ConfigLimits::m_unRetryMin, ConfigLimits::m_unRetryMax);
        NUM(m_unEmptyDirAgeDays, "EmptyDirAgeDays", ConfigLimits::m_unEmptyDirDaysMin, ConfigLimits::m_unEmptyDirDaysMax);
        NUM(m_unConnTimeout, "ConnTimeout", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        NUM(m_unIoTimeout, "IoTimeout", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        NUM(m_unHeartbeat, "Heartbeat", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        NUM(m_unReconnMin, "ReconnMin", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        NUM(m_unReconnMax, "ReconnMax", ConfigLimits::m_unIntervalMin, ConfigLimits::m_unIntervalMax);
        NUM(m_unIdleDisconn, "IdleDisconn", 0, ConfigLimits::m_unIntervalMax);
#undef STR
#undef NUM
#undef FLAG
        // 旧密码只在明文密码未提供时读取，成功迁移后由保存流程改写配置。
        CString strLegacyPassword = ReadString(strSource, strSection, L"Password", L"");
        if (!strLegacyPassword.IsEmpty())
        {
            if (stConfig.m_strPassword.IsEmpty())
            {
                if (strLegacyPassword.Left(4) == L"ENC:")
                {
                    if (!DecryptPassword(strLegacyPassword, stConfig.m_strPassword))
                    {
                        strMessage = L"旧版 FTP 密码解密失败，未迁移配置，请重新配置密码：" + stConfig.m_strName;
                        vecConfigs.clear();
                        return false;
                    }
                }
                else
                    stConfig.m_strPassword = strLegacyPassword;
            }
            bMigrate = true;
        }
        vecConfigs.push_back(stConfig);
    }
    std::set<UINT> setTaskNumbers;
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex)
    {
        UINT unNumber = vecConfigs[nIndex].m_unTaskNumber;
        if (!unNumber)
            continue;
        if (!setTaskNumbers.insert(unNumber).second)
        {
            strMessage = L"任务编号重复";
            return false;
        }
        stGlobal.m_unNextTaskNumber = max(stGlobal.m_unNextTaskNumber, unNumber + 1);
    }
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex)
    {
        if (!vecConfigs[nIndex].m_unTaskNumber)
        {
            vecConfigs[nIndex].m_unTaskNumber = stGlobal.m_unNextTaskNumber++;
            bNumberMigration = true;
        }
    }
    // 迁移失败时向调用方报告，不静默丢失原密码或任务身份。
    if (bMigrate || bNumberMigration)
    {
        CString strFailure;
        if (!Save(strPath, stGlobal, vecConfigs, strFailure, bMigrate))
        {
            strMessage = strFailure;
            return false;
        }
    }
    return true;
}

// 生成完整 UTF-16 配置，写入临时文件后再维护备份并替换主文件。
bool CConfigStore::Save(const CString& strPath, const GlobalConfig& stGlobal, const std::vector<TaskConfig>& vecConfigs,
                        CString& strMessage, bool bUpdateBackup)
{
    strMessage.Empty();
    if (vecConfigs.size() > m_unMaxTasks)
    {
        strMessage = L"任务最多 32 个";
        return false;
    }
    CString strText(L"[Global]\r\nVersion=1\r\n");
    if (!stGlobal.Validate(strMessage))
        return false;
    CString strLine;
#define WNUM(key, val)                                                                                                 \
    strLine.Format(L##key L"=%u\r\n", (UINT)(val));                                                                    \
    strText += strLine
#define WSTR(key, val) strText += CString(L##key L"=") + (val) + L"\r\n"
    WNUM("AutoStart", stGlobal.m_bAutoStart);
    WNUM("StartMinimized", stGlobal.m_bStartMinimized);
    WNUM("PreventSleep", stGlobal.m_bPreventSleep);
    WNUM("LogMaxSizeMB", stGlobal.m_unLogMaxSizeMb);
    WNUM("LogArchiveCount", stGlobal.m_unLogArchiveCount);
    WNUM("UiMaxLogLines", stGlobal.m_unUiMaxLines);
    WNUM("NextTaskNumber", stGlobal.m_unNextTaskNumber);
    WNUM("TaskCount", vecConfigs.size());
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex)
    {
        strLine.Format(L"Task%u=%s\r\n", (UINT)nIndex, (LPCWSTR)vecConfigs[nIndex].m_strId);
        strText += strLine;
    }
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex)
    {
        const TaskConfig& stConfig = vecConfigs[nIndex];
        strText += L"\r\n[Task_" + stConfig.m_strId + L"]\r\n";
        WSTR("Name", stConfig.m_strName);
        WNUM("TaskNumber", stConfig.m_unTaskNumber);
        WSTR("Host", stConfig.m_strHost);
        WNUM("Port", stConfig.m_unPort);
        WSTR("User", stConfig.m_strUser);
        WSTR("PasswordPlain", L"\"" + stConfig.m_strPassword + L"\"");
        WNUM("Passive", stConfig.m_bPassive);
        WNUM("Selected", stConfig.m_bSelected);
        WNUM("Encoding", stConfig.m_nEncoding);
        WSTR("LocalDir", stConfig.m_strLocalDir);
        WSTR("RemoteDir", stConfig.m_strRemoteDir);
        WNUM("IncludeSubDir", stConfig.m_bIncludeSubDir);
        WNUM("DeleteOldEmptyDirs", stConfig.m_bDeleteOldEmptyDirs);
        WSTR("IncludeMask", stConfig.m_strIncludeMask);
        WSTR("ExcludeMask", stConfig.m_strExcludeMask);
        WNUM("ScanInterval", stConfig.m_unScanInterval);
        WNUM("UploadIntervalMs", stConfig.m_unUploadIntervalMs);
        WNUM("StableSec", stConfig.m_unStableSec);
        WNUM("PostAction", stConfig.m_nPostAction);
        WSTR("BackupDir", stConfig.m_strBackupDir);
        WNUM("RemoteExist", stConfig.m_nRemoteExist);
        WNUM("UseTempName", stConfig.m_bUseTempName);
        WSTR("TempSuffix", stConfig.m_strTempSuffix);
        WNUM("VerifySize", stConfig.m_bVerifySize);
        WNUM("MaxFileRetry", stConfig.m_unMaxFileRetry);
        WNUM("EmptyDirAgeDays", stConfig.m_unEmptyDirAgeDays);
        WNUM("ConnTimeout", stConfig.m_unConnTimeout);
        WNUM("IoTimeout", stConfig.m_unIoTimeout);
        WNUM("Heartbeat", stConfig.m_unHeartbeat);
        WNUM("ReconnMin", stConfig.m_unReconnMin);
        WNUM("ReconnMax", stConfig.m_unReconnMax);
        WNUM("IdleDisconn", stConfig.m_unIdleDisconn);
    }
#undef WNUM
#undef WSTR
    CString strDir(strPath);
    strDir = strDir.Left(strDir.ReverseFind(L'\\'));
    if (!MakeDirs(strDir))
    {
        strMessage = L"无法创建配置目录";
        return false;
    }
    CString strTemp = strPath + L".tmp";
    CString strBackup = strPath + L".bak";
    CAutoHandle hFile(CreateFileW(strTemp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    WORD wBom = 0xFEFF;
    if (!hFile.Valid() || !WriteBytes(hFile.Get(), &wBom, 2) ||
        !WriteBytes(hFile.Get(), (LPCWSTR)strText, strText.GetLength() * sizeof(WCHAR)) ||
        !FlushFileBuffers(hFile.Get()))
    {
        strMessage = L"写入配置失败";
        return false;
    }
    hFile.Close();
    if (bUpdateBackup)
    {
        if (!CopyFileW(strTemp, strBackup, FALSE))
        {
            strMessage = L"更新配置备份失败";
            return false;
        }
    }
    else if (PathFileExistsW(strPath) && !CopyFileW(strPath, strBackup, FALSE))
    {
        strMessage = L"备份配置失败";
        return false;
    }
    if (!MoveFileExW(strTemp, strPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        strMessage = L"替换配置失败：" + WinError(GetLastError());
        return false;
    }
    if (!PathFileExistsW(strBackup))
        CopyFileW(strPath, strBackup, FALSE);
    return true;
}
