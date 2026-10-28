#include "stdafx.h"
#include "TaskManager.h"

// 条目同时拥有配置和工作对象，统一生命周期，禁止分开调整下标。
struct CTaskManager::Entry
{
    TaskConfig m_stConfig;
    CTaskWorker m_workerTask;

    Entry(const TaskConfig& stConfig, const CString& strRoot, CLogger& loggerFile)
        : m_stConfig(stConfig),
          m_workerTask(stConfig, strRoot, loggerFile)
    {
    }
};

CTaskManager::CTaskManager(const CString& strRoot, const CString& strConfigPath, CLogger& loggerFile)
    : m_strRoot(strRoot),
      m_strConfigPath(strConfigPath),
      m_loggerFile(loggerFile)
{
}

// 先通知全部线程停止，再逐个等待析构回收，避免串行发停止请求。
CTaskManager::~CTaskManager()
{
    RequestStopAll(L"回收任务");
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
        delete m_vecEntries[nIndex];
}

bool CTaskManager::Load(GlobalConfig& stGlobal, CString& strMessage)
{
    std::vector<TaskConfig> vecConfigs;
    bool bLoaded = m_storeConfig.Load(m_strConfigPath, stGlobal, vecConfigs, strMessage);
    m_vecEntries.reserve(vecConfigs.size());
    for (size_t nIndex = 0; nIndex < vecConfigs.size(); ++nIndex)
        m_vecEntries.push_back(new Entry(vecConfigs[nIndex], m_strRoot, m_loggerFile));
    return bLoaded;
}

std::vector<TaskConfig> CTaskManager::Configs() const
{
    std::vector<TaskConfig> vecConfigs;
    vecConfigs.reserve(Count());
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
        vecConfigs.push_back(Config(nIndex));
    return vecConfigs;
}

bool CTaskManager::Save(const GlobalConfig& stGlobal, CString& strMessage)
{
    return m_storeConfig.Save(m_strConfigPath, stGlobal, Configs(), strMessage);
}

bool CTaskManager::CheckConflict(const TaskConfig& stConfig, int nExcept, CString& strMessage) const
{
    strMessage.Empty();
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
    {
        if ((int)nIndex == nExcept)
            continue;
        if (stConfig.m_strName.CompareNoCase(Config(nIndex).m_strName) == 0)
            strMessage = L"任务名称不能重复";
        else if (IsWithin(stConfig.m_strLocalDir, Config(nIndex).m_strLocalDir) ||
                 IsWithin(Config(nIndex).m_strLocalDir, stConfig.m_strLocalDir))
            strMessage = L"监控目录不能相同或互相包含，以免重复上传/删除";
        if (!strMessage.IsEmpty())
            return false;
    }
    return true;
}

bool CTaskManager::CanModify(CString& strMessage) const
{
    strMessage.Empty();
    if (AnyRunning())
        strMessage = L"请先停止全部任务";
    return strMessage.IsEmpty();
}

// 提交顺序为准备对象和容量、保存配置、发布内存条目及新编号。
bool CTaskManager::Add(TaskConfig& stConfig, GlobalConfig& stGlobal, CString& strMessage)
{
    if (!CanModify(strMessage))
        return false;
    if (Count() >= m_unMaxTasks)
    {
        strMessage = L"最多支持 32 个任务";
        return false;
    }
    if (!stConfig.Validate(strMessage) || !CheckConflict(stConfig, -1, strMessage))
        return false;
    GlobalConfig stNewGlobal = stGlobal;
    stConfig.m_unTaskNumber = stNewGlobal.m_unNextTaskNumber++;
    std::vector<TaskConfig> vecConfigs = Configs();
    vecConfigs.push_back(stConfig);
    std::auto_ptr<Entry> pEntry(new Entry(stConfig, m_strRoot, m_loggerFile));
    m_vecEntries.reserve(Count() + 1);
    if (!m_storeConfig.Save(m_strConfigPath, stNewGlobal, vecConfigs, strMessage))
        return false;
    m_vecEntries.push_back(pEntry.release());
    stGlobal = stNewGlobal;
    return true;
}

// 先构造替代对象并持久化，保存失败时原工作对象仍保持不变。
bool CTaskManager::Update(size_t nIndex, const TaskConfig& stConfig, const GlobalConfig& stGlobal, CString& strMessage)
{
    if (!CanModify(strMessage) || !stConfig.Validate(strMessage) ||
        !CheckConflict(stConfig, (int)nIndex, strMessage))
        return false;
    std::vector<TaskConfig> vecConfigs = Configs();
    vecConfigs[nIndex] = stConfig;
    std::auto_ptr<Entry> pEntry(new Entry(stConfig, m_strRoot, m_loggerFile));
    if (!m_storeConfig.Save(m_strConfigPath, stGlobal, vecConfigs, strMessage))
        return false;
    delete m_vecEntries[nIndex];
    m_vecEntries[nIndex] = pEntry.release();
    return true;
}

// 先保存删除后的配置快照，保存成功后才释放条目。
bool CTaskManager::Remove(size_t nIndex, const GlobalConfig& stGlobal, CString& strMessage)
{
    if (!CanModify(strMessage))
        return false;
    std::vector<TaskConfig> vecConfigs = Configs();
    vecConfigs.erase(vecConfigs.begin() + nIndex);
    if (!m_storeConfig.Save(m_strConfigPath, stGlobal, vecConfigs, strMessage))
        return false;
    delete m_vecEntries[nIndex];
    m_vecEntries.erase(m_vecEntries.begin() + nIndex);
    return true;
}

// 仅在磁盘保存成功后改变勾选值，供主窗口安全回滚复选框。
bool CTaskManager::SetSelected(size_t nIndex, bool bSelected, const GlobalConfig& stGlobal, CString& strMessage)
{
    if (!CanModify(strMessage))
        return false;
    std::vector<TaskConfig> vecConfigs = Configs();
    vecConfigs[nIndex].m_bSelected = bSelected;
    if (!m_storeConfig.Save(m_strConfigPath, stGlobal, vecConfigs, strMessage))
        return false;
    m_vecEntries[nIndex]->m_stConfig.m_bSelected = bSelected;
    return true;
}

size_t CTaskManager::Count() const
{
    return m_vecEntries.size();
}

const TaskConfig& CTaskManager::Config(size_t nIndex) const
{
    return m_vecEntries[nIndex]->m_stConfig;
}

TaskStatus CTaskManager::Status(size_t nIndex)
{
    return m_vecEntries[nIndex]->m_workerTask.Status();
}

bool CTaskManager::Running(size_t nIndex) const
{
    return m_vecEntries[nIndex]->m_workerTask.Running();
}

bool CTaskManager::AnyRunning() const
{
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
        if (Running(nIndex))
            return true;
    return false;
}

bool CTaskManager::Start(size_t nIndex, const CString& strReason)
{
    bool bStarted = m_vecEntries[nIndex]->m_workerTask.Start();
    CString strResult = strReason + (bStarted ? L"；已启动" : L"；启动失败");
    if (!bStarted)
        strResult += L"：" + Status(nIndex).m_strLastError;
    m_loggerFile.Operation(L"启动任务", strResult, &Config(nIndex), bStarted ? 1 : 3);
    return bStarted;
}

UINT CTaskManager::StartSelected(const CString& strReason)
{
    UINT unCount = 0;
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
        if (Config(nIndex).m_bSelected && !Running(nIndex) && Start(nIndex, strReason))
            ++unCount;
    return unCount;
}

UINT CTaskManager::RequestStopAll(const CString& strReason)
{
    UINT unCount = 0;
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
    {
        if (Running(nIndex))
        {
            ++unCount;
            m_loggerFile.Operation(L"停止任务", strReason, &Config(nIndex));
        }
        m_vecEntries[nIndex]->m_workerTask.RequestStop();
    }
    return unCount;
}

bool CTaskManager::PollStopped()
{
    bool bAllStopped = true;
    for (size_t nIndex = 0; nIndex < Count(); ++nIndex)
    {
        bool bWasRunning = Running(nIndex);
        if (!m_vecEntries[nIndex]->m_workerTask.PollStopped())
            bAllStopped = false;
        else if (bWasRunning)
            m_loggerFile.Operation(L"停止任务", L"已停止", &Config(nIndex));
    }
    return bAllStopped;
}
