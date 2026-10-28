#pragma once
#include "TaskManager.h"
#include "TaskEditDlg.h"
#include "SettingsDlg.h"
#include "RemoteTemp.h"

// 主窗口负责交互及快照展示，任务生命周期交由管理器处理。
class CFtpUploaderToolDlg : public CDialog
{
public:
    explicit CFtpUploaderToolDlg(CWnd* pParent = NULL);

protected:
    virtual BOOL OnInitDialog();
    virtual void DoDataExchange(CDataExchange* pExchange);

    // 主窗口不以 Enter 结束对话框。
    virtual void OnOK()
    {
    }

    virtual void OnCancel();
    virtual BOOL PreTranslateMessage(MSG* pMessage);
    afx_msg void OnAction(UINT unId);
    afx_msg void OnTimer(UINT_PTR unId);
    afx_msg void OnDoubleClick(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnTaskItemChanging(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnTaskCheckChanged(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnLogInfo(NMHDR* pHeader, LRESULT* pResult);
    afx_msg BOOL OnLogTip(UINT unId, NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnLogScroll(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnLogDraw(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnTaskDraw(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnFilter();
    afx_msg void OnAutoScroll();
    afx_msg LRESULT OnTray(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTaskbarCreated(WPARAM, LPARAM);
    afx_msg LRESULT OnShowInstance(WPARAM, LPARAM);
    afx_msg void OnTrayShow();
    afx_msg void OnExit();
    afx_msg void OnSysCommand(UINT unId, LPARAM lParam);
    afx_msg BOOL OnQueryEndSession();
    afx_msg void OnEndSession(BOOL bEnding);
    afx_msg void OnDestroy();
    DECLARE_MESSAGE_MAP()
private:
    int Selected() const;
    bool Save();
    bool CheckConflict(const TaskConfig& stConfig, int nExcept);
    void RebuildTasks();
    void RefreshStatus();
    void RefreshLogs();
    void RebuildLogView();
    CString LogCellText(int nRow, int nColumn) const;
    void UpdateLogTip();
    void Edit(int nIndex);
    void Tray(bool bAdd);
    void BeginExit(const CString& strReason);
    void ApplyGlobal();
    CString m_strRoot;
    GlobalConfig m_stGlobal;
    // 这些后台成员按声明逆序析构，保证工作对象先于日志器销毁。
    CLogger m_loggerFile;
    CRemoteTempCleanup m_tempCleanup;
    CTaskManager m_managerTasks;
    CListCtrl m_listTasks;
    CListCtrl m_listLogs;
    CComboBox m_cmbLogTask;
    CComboBox m_cmbLogLevel;
    CToolTipCtrl m_toolTip;
    CToolTipCtrl m_toolTipLog;
    CString m_strLogTipText;
    UINT_PTR m_unLogTipId;
    std::deque<LogItem> m_deqUiLogs;
    // 虚拟列表保存筛选后的日志索引，数量受界面日志上限限制。
    std::vector<int> m_vecView;
    // 退出状态锁定界面命令，防止停止期间创建新任务。
    bool m_bClosing;
    // 确保退出日志只记录一次，并在其他后台工作结束后排空日志。
    bool m_bLoggerStopping;
    // 程序重建列表时抑制复选框通知，避免无意保存配置。
    bool m_bUpdatingChecks;
    // 运行期间拦截列表输入，保留正常启用外观及状态绘制。
    bool m_bTaskListLocked;
    // 批量停止请求已发出，完成前禁止重复点击停止按钮。
    bool m_bStoppingTasks;
    DWORD m_dwExitTick;
    HICON m_hIcon;
};
