#pragma once
#include "ConnectionTest.h"
#include "Logger.h"
#include "resource.h"

// 编辑任务参数、展示说明及连接测试结果，不直接启动正式上传任务。
class CTaskEditDlg : public CDialog
{
public:
    CTaskEditDlg(const TaskConfig& stConfig, CWnd* pParent, CLogger* pLogger = NULL);
    ~CTaskEditDlg();

    TaskConfig Config() const
    {
        return m_stConfig;
    }

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();
    virtual BOOL PreTranslateMessage(MSG* pMessage);
    afx_msg void OnBrowseLocal();
    afx_msg void OnBrowseBackup();
    afx_msg void OnPolicyChanged();
    afx_msg void OnTest();
    afx_msg void OnTimer(UINT_PTR unId);
    DECLARE_MESSAGE_MAP()
private:
    // 读取控件并做字段校验，不在此处写入任务配置文件。
    bool ReadConfig(TaskConfig& stConfig);
    void Browse(int nControl);
    // 关闭或保存配置窗口时取消测试并释放窗口引用。
    void CancelTest();
    // 测试完成后只记录一次最终结果，定时刷新不会重复写日志。
    void LogTestResult();
    void LogOperation(const CString& strAction, const CString& strResult, int nLevel = 1);
    TaskConfig m_stConfig;
    CConnectionTest m_testConnection;
    CLogger* m_pLogger;
    bool m_bTestResultLogged;
    CToolTipCtrl m_toolTip;
};
