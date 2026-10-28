#pragma once
#include "Logger.h"
#include "resource.h"

// 编辑全局设置；确认后由主窗口负责保存及应用系统选项。
class CSettingsDlg : public CDialog
{
public:
    // 本次编辑的设置副本，取消不会覆盖调用方配置。
    GlobalConfig m_stConfig;

    CSettingsDlg(const GlobalConfig& stGlobal, CWnd* pParent, CLogger* pLogger = NULL)
        : CDialog(IDD_DLG_SETTINGS, pParent),
          m_stConfig(stGlobal),
          m_pLogger(pLogger)
    {
    }

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual BOOL PreTranslateMessage(MSG* pMessage);

    DECLARE_MESSAGE_MAP()
private:
    CLogger* m_pLogger;
    CToolTipCtrl m_toolTip;
};
