#pragma once
#include "Worker.h"
#include "TaskEditDlg.h"
class CFtpUploaderToolDlg : public CDialog {
public:
    explicit CFtpUploaderToolDlg(CWnd* pParent = NULL);
    ~CFtpUploaderToolDlg();
protected:
    virtual BOOL OnInitDialog(); virtual void DoDataExchange(CDataExchange* pExchange);
    virtual void OnOK() {} virtual void OnCancel(); virtual BOOL PreTranslateMessage(MSG* pMessage);
    afx_msg void OnAction(UINT unId); afx_msg void OnTimer(UINT_PTR unId); afx_msg void OnSize(UINT unType, int nCx, int nCy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* pInfo); afx_msg void OnDoubleClick(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnLogInfo(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnLogDraw(NMHDR* pHeader, LRESULT* pResult); afx_msg void OnTaskDraw(NMHDR* pHeader, LRESULT* pResult);
    afx_msg void OnFilter(); afx_msg LRESULT OnTray(WPARAM wParam, LPARAM lParam); afx_msg LRESULT OnTaskbarCreated(WPARAM, LPARAM);
    afx_msg LRESULT OnShowInstance(WPARAM, LPARAM); afx_msg void OnTrayShow(); afx_msg void OnExit();
    afx_msg void OnSysCommand(UINT unId, LPARAM lParam); afx_msg BOOL OnQueryEndSession(); afx_msg void OnEndSession(BOOL bEnding);
    afx_msg void OnDestroy();
    DECLARE_MESSAGE_MAP()
private:
    int Selected() const; bool Save(); bool CheckConflict(const TaskConfig& stConfig, int nExcept);
    void RebuildTasks(); void RefreshStatus(); void RefreshLogs(); void RebuildLogView(); void Edit(int nIndex);
    void Tray(bool bAdd); void BeginExit(); void ApplyGlobal();
    void Layout(int nCx, int nCy);
    CString m_strRoot, m_strConfigPath; GlobalConfig m_stGlobal; CConfigStore m_storeConfig; CLogger m_loggerFile;
    std::vector<TaskConfig> m_vecConfigs; std::vector<CTaskWorker*> m_vecWorkers;
    CListCtrl m_listTasks, m_listLogs; CComboBox m_cmbLogTask, m_cmbLogLevel;
    std::deque<LogItem> m_deqUiLogs; std::vector<int> m_vecView; // Bounded by UiMaxLogLines.
    bool m_bClosing; int m_nEditAfterStop; DWORD m_dwExitTick, m_dwStartTick; HICON m_hIcon;
};
