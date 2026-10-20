#pragma once
#include "FtpClient.h"
#include "Files.h"
#include "resource.h"
struct TaskConnectionTest;
class CTaskEditDlg : public CDialog {
public:
    CTaskEditDlg(const TaskConfig& stConfig, CWnd* pParent);
    ~CTaskEditDlg();
    TaskConfig Config() const { return m_stConfig; }
protected:
    virtual BOOL OnInitDialog(); virtual void OnOK(); virtual void OnCancel();
    afx_msg void OnBrowseLocal(); afx_msg void OnBrowseBackup(); afx_msg void OnTest(); afx_msg void OnTimer(UINT_PTR unId);
    DECLARE_MESSAGE_MAP()
private:
    bool ReadConfig(TaskConfig& stConfig); void Browse(int nControl); void CancelTest();
    TaskConfig m_stConfig; TaskConnectionTest* m_pTest;
};
class CSettingsDlg : public CDialog {
public:
    GlobalConfig m_stConfig;
    CSettingsDlg(const GlobalConfig& stGlobal, CWnd* pParent) : CDialog(IDD_DLG_SETTINGS, pParent), m_stConfig(stGlobal) {}
protected:
    virtual BOOL OnInitDialog(); virtual void OnOK();
};
