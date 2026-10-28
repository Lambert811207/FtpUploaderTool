#pragma once
#include "Config.h"

// 连接测试的一次快照，供窗口显示进度并区分成功、失败和取消。
struct ConnectionTestResult
{
    TaskConfig m_stConfig;
    CString m_strText;
    bool m_bStarted;
    bool m_bDone;
    bool m_bCancelled;
    bool m_bSucceeded;

    ConnectionTestResult()
        : m_bStarted(false),
          m_bDone(false),
          m_bCancelled(false),
          m_bSucceeded(false)
    {
    }
};

// 窗口拥有测试入口，引用计数状态允许后台线程在窗口关闭后安全收尾。
class CConnectionTest
{
public:
    CConnectionTest();
    ~CConnectionTest();
    // 启动一次连接、目录、测试上传及删除流程，不替换仍在运行的测试。
    bool Start(const TaskConfig& stConfig);
    // 通知当前测试取消，网络等待会分片检查停止事件。
    void RequestStop();
    // 请求取消并释放窗口持有的引用，不同步等待测试线程。
    void Reset();
    // 复制当前测试信息，未启动时 m_bStarted 为 false。
    ConnectionTestResult Snapshot() const;

private:
    struct State;
    static unsigned __stdcall ThreadProc(void* pContext);
    State* m_pState;
    CConnectionTest(const CConnectionTest&);
    CConnectionTest& operator=(const CConnectionTest&);
};
