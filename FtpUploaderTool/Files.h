#pragma once
#include "Config.h"
struct PendingFile {
    CString m_strFullPath, m_strRelative, m_strName, m_strSubDir; ULONGLONG m_ullSize, m_ullModified;
    PendingFile() : m_ullSize(0), m_ullModified(0) {}
    bool Same(const PendingFile& stOther) const { return m_ullSize == stOther.m_ullSize && m_ullModified == stOther.m_ullModified; }
};
bool StatFile(const CString& strRoot, const CString& strPath, PendingFile& stFile);
CString RemoteDirectory(const TaskConfig& stConfig, const PendingFile& stFile);
CString FileDate(ULONGLONG ullModified, bool bCompact);
struct FailInfo { UINT m_unAttempts; DWORD m_dwLastTick; bool m_bParked; FailInfo() : m_unAttempts(0), m_dwLastTick(0), m_bParked(false) {} };
class CUploadHistory {
public:
    bool Load(const CString& strPath);
    bool Contains(const PendingFile& stFile) const;
    bool Add(const PendingFile& stFile);
    bool Compact(const CString& strRoot);
private:
    CString m_strPath; std::map<CString, std::pair<ULONGLONG, ULONGLONG> > m_mapRecords; // One signature per existing path, compacted daily.
};
class CDirScanner {
public:
    explicit CDirScanner(const TaskConfig& stConfig) : m_stConfig(stConfig) {}
    bool Scan(std::deque<PendingFile>& deqQueue, const CUploadHistory& historyUpload, std::map<CString, FailInfo>& mapFails,
        std::map<CString, PendingFile>& mapPost, HANDLE hStop);
    void Dequeued(const CString& strPath) { m_setQueued.erase(strPath); m_mapCandidates.erase(strPath); }
private:
    struct Candidate { PendingFile m_stFile; DWORD m_dwSince; bool m_bSeen; };
    void Walk(const CString& strDirectory, int nDepth, std::vector<PendingFile>& vecFound, HANDLE hStop);
    bool Match(const CString& strName, const CString& strMasks, bool bEmpty) const;
    TaskConfig m_stConfig; std::map<CString, Candidate> m_mapCandidates; std::set<CString> m_setQueued; // Pruned each scan / dequeue.
};
