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

    FailInfo()
        : m_unAttempts(0),
          m_dwLastTick(0),
          m_bParked(false)
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
    explicit CDirScanner(const TaskConfig& stConfig)
        : m_stConfig(stConfig)
    {
    }

    // 更新候选文件及有界队列；已上传但未完成后处理的文件放入 mapPost。
    bool Scan(std::deque<PendingFile>& deqQueue, const CUploadHistory& historyUpload,
              std::map<CString, FailInfo>& mapFails, std::map<CString, PendingFile>& mapPost, HANDLE hStop);
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

    void Walk(const CString& strDirectory, int nDepth, std::vector<PendingFile>& vecFound, HANDLE hStop);
    void CleanupDirectory(const CString& strDirectory, int nDepth, ULONGLONG ullCutoff,
                          CLogger& loggerFile, HANDLE hStop, UINT& unDeleted, UINT& unFailed);
    bool Match(const CString& strName, const CString& strMasks, bool bEmpty) const;
    TaskConfig m_stConfig;
    std::map<CString, Candidate> m_mapCandidates;
    // 防止扫描期间重复入队，文件出队时同步移除。
    std::set<CString> m_setQueued;
};
