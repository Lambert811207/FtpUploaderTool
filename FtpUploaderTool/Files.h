#pragma once
#include "Config.h"

class CLogger;

// 扫描时的文件快照，用大小和修改时间识别文件是否发生变化。
struct PendingFile
{
    CString m_strFullPath;
    CString m_strRelative;
    CString m_strName;
    CString m_strSubDir;
    ULONGLONG m_ullSize;
    ULONGLONG m_ullModified;

    PendingFile()
        : m_ullSize(0),
          m_ullModified(0)
    {
    }

    // 比较文件版本，不比较路径；调用方负责确认是同一文件。
    bool Same(const PendingFile& stOther) const
    {
        return m_ullSize == stOther.m_ullSize && m_ullModified == stOther.m_ullModified;
    }
};

bool StatFile(const CString& strRoot, const CString& strPath, PendingFile& stFile);
// 根据源文件时间、相对子目录及占位符生成服务器目标目录。
CString RemoteDirectory(const TaskConfig& stConfig, const PendingFile& stFile);
CString FileDate(ULONGLONG ullModified, bool bCompact);

// 当前运行周期内的单文件失败与暂停信息，网络重连单独处理。
struct FailInfo
{
    UINT m_unAttempts;
    DWORD m_dwLastTick;
    bool m_bParked;
    // 扫描期间标记本轮仍存在，扫描结束后移除未出现的路径。
    bool m_bSeen;

    FailInfo()
        : m_unAttempts(0),
          m_dwLastTick(0),
          m_bParked(false),
          m_bSeen(false)
    {
    }
};

// 已上传但尚未完成删除或备份的文件，失败后按次数退避重试。
struct PostInfo
{
    PendingFile m_stFile;
    // 连续失败次数，源文件版本变化后重新计数。
    UINT m_unFailures;
    DWORD m_dwLastTick;
    // 扫描期间标记本轮仍存在，扫描结束后移除未出现的路径。
    bool m_bSeen;

    PostInfo()
        : m_unFailures(0),
          m_dwLastTick(0),
          m_bSeen(true)
    {
    }

    explicit PostInfo(const PendingFile& stFile)
        : m_stFile(stFile),
          m_unFailures(0),
          m_dwLastTick(0),
          m_bSeen(true)
    {
    }
};

// 持久化已上传记录，重启后跳过路径、大小和修改时间均相同的文件。
class CUploadHistory
{
public:
    bool Load(const CString& strPath);
    bool Contains(const PendingFile& stFile) const;
    // 追加记录并刷新到磁盘，成功后才更新内存索引。
    bool Add(const PendingFile& stFile);
    // 按现有源文件整理记录，保留每条路径最新的文件版本。
    bool Compact(const CString& strRoot);

private:
    CString m_strPath;
    // 按相对路径保存最新的大小及修改时间，每日整理时移除已不存在的文件。
    std::map<CString, std::pair<ULONGLONG, ULONGLONG> >
        m_mapRecords;
};

// 扫描本地目录，应用过滤条件并等待文件稳定后加入待上传队列。
class CDirScanner
{
public:
    explicit CDirScanner(const TaskConfig& stConfig);

    // 更新候选文件及有界队列；已上传但未完成后处理的文件放入 mapPost。
    bool Scan(std::deque<PendingFile>& deqQueue, const CUploadHistory& historyUpload,
              std::map<CString, FailInfo>& mapFails, std::map<CString, PostInfo>& mapPost, HANDLE hStop);
    // 按目录最后修改时间清理旧空子文件夹，始终保留监控根目录。
    void CleanupEmptyDirs(CLogger& loggerFile, HANDLE hStop);

    // 释放去重标记，使失败后允许重新观察同一文件。
    void Dequeued(const CString& strPath)
    {
        m_setQueued.erase(strPath);
        m_mapCandidates.erase(strPath);
    }

private:
    struct Candidate
    {
        PendingFile m_stFile;
        DWORD m_dwSince;
        bool m_bSeen;
    };

    // 与上传历史一致的文件放入 vecUploaded，其余文件读取精确元数据后放入 vecFound。
    void Walk(const CString& strDirectory, int nDepth, const CUploadHistory& historyUpload,
              std::vector<PendingFile>& vecUploaded, std::vector<PendingFile>& vecFound, HANDLE hStop);
    // 已上传的文件只补做删除或备份；已有记录且版本不同时按新版本重新计数。
    void KeepUploaded(const PendingFile& stFile, std::map<CString, PostInfo>& mapPost);
    void CleanupDirectory(const CString& strDirectory, int nDepth, ULONGLONG ullCutoff,
                          CLogger& loggerFile, HANDLE hStop, UINT& unDeleted, UINT& unFailed);
    bool Match(LPCWSTR pszName, const std::vector<CString>& vecMasks) const;
    TaskConfig m_stConfig;
    // 构造时切分的文件名规则，避免每个文件重复切分规则串。
    std::vector<CString> m_vecIncludeMasks;
    std::vector<CString> m_vecExcludeMasks;
    // 包含规则串为空时匹配全部文件名；非空但没有有效分段时不匹配任何文件，与原逐段匹配一致。
    bool m_bIncludeAll;
    std::map<CString, Candidate> m_mapCandidates;
    // 防止扫描期间重复入队，文件出队时同步移除。
    std::set<CString> m_setQueued;
};
