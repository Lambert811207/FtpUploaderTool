#pragma once
#include "Worker.h"

// 由界面线程调用，统一拥有任务配置及工作对象，避免平行数组错位。
class CTaskManager
{
public:
    CTaskManager(const CString& strRoot, const CString& strConfigPath, CLogger& loggerFile);
    ~CTaskManager();
    bool Load(GlobalConfig& stGlobal, CString& strMessage);
    bool Save(const GlobalConfig& stGlobal, CString& strMessage);
    // 检查重名与监控目录重叠；nExcept 用于排除正在编辑的原任务。
    bool CheckConflict(const TaskConfig& stConfig, int nExcept, CString& strMessage) const;
    // 保存成功后才加入任务列表并推进全局编号，新任务保持停止。
    bool Add(TaskConfig& stConfig, GlobalConfig& stGlobal, CString& strMessage);
    // 先保存新配置，再替换对应工作对象；运行期间禁止编辑。
    bool Update(size_t nIndex, const TaskConfig& stConfig, const GlobalConfig& stGlobal, CString& strMessage);
    // 只删除任务配置和工作对象，不删除用户的本地文件。
    bool Remove(size_t nIndex, const GlobalConfig& stGlobal, CString& strMessage);
    // 持久化参与上传的勾选状态，保存失败时保留原值。
    bool SetSelected(size_t nIndex, bool bSelected, const GlobalConfig& stGlobal, CString& strMessage);
    size_t Count() const;
    const TaskConfig& Config(size_t nIndex) const;
    TaskStatus Status(size_t nIndex);
    bool Running(size_t nIndex) const;
    bool AnyRunning() const;
    bool Start(size_t nIndex, const CString& strReason);
    // 仅启动已勾选且尚未运行的任务，返回实际启动数量。
    UINT StartSelected(const CString& strReason);
    // 同时通知所有工作线程停止，返回发出请求时的运行任务数。
    UINT RequestStopAll(const CString& strReason);
    // 轮询并回收已结束线程，记录停止结果；全部结束时返回 true。
    bool PollStopped();

private:
    struct Entry;
    std::vector<TaskConfig> Configs() const;
    bool CanModify(CString& strMessage) const;
    CString m_strRoot;
    CString m_strConfigPath;
    CLogger& m_loggerFile;
    CConfigStore m_storeConfig;
    // 每个条目独占一个配置与工作对象，由管理器析构统一释放。
    std::vector<Entry*> m_vecEntries;
    CTaskManager(const CTaskManager&);
    CTaskManager& operator=(const CTaskManager&);
};
