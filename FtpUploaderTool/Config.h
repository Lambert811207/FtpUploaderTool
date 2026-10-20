#pragma once
#include "Common.h"
enum PostAction { POST_DELETE, POST_MOVE, POST_KEEP };
enum RemoteExist { EXIST_OVERWRITE, EXIST_RENAME, EXIST_SKIP };
enum FtpEncoding { ENC_AUTO, ENC_UTF8, ENC_GBK };
struct TaskConfig {
    CString m_strId, m_strName, m_strHost, m_strUser, m_strPassword, m_strEncryptedPassword, m_strLocalDir, m_strRemoteDir, m_strBackupDir;
    CString m_strIncludeMask, m_strExcludeMask, m_strTempSuffix, m_strError;
    bool m_bPassive, m_bIncludeSubDir, m_bUseTempName, m_bVerifySize;
    UINT m_unPort, m_unScanInterval, m_unStableSec, m_unMaxFileRetry, m_unMaxFileAgeDays, m_unKeepDays;
    UINT m_unConnTimeout, m_unIoTimeout, m_unHeartbeat, m_unReconnMin, m_unReconnMax, m_unIdleDisconn;
    int m_nEncoding, m_nPostAction, m_nRemoteExist;
    TaskConfig();
    bool Validate(CString& strMessage) const;
};
struct GlobalConfig {
    bool m_bAutoStart, m_bStartMinimized, m_bPreventSleep;
    UINT m_unLogKeepDays, m_unBackupKeepDays, m_unUiMaxLines;
    GlobalConfig() : m_bAutoStart(false), m_bStartMinimized(false), m_bPreventSleep(true), m_unLogKeepDays(30), m_unBackupKeepDays(30), m_unUiMaxLines(2000) {}
};
class CConfigStore {
public:
    bool Load(const CString& strPath, GlobalConfig& stGlobal, std::vector<TaskConfig>& vecConfigs, CString& strMessage);
    bool Save(const CString& strPath, const GlobalConfig& stGlobal, const std::vector<TaskConfig>& vecConfigs, CString& strMessage, bool bSanitizeBackup = false);
};
