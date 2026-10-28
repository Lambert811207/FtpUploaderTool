#include "stdafx.h"
#include "SettingsDlg.h"
#include "DialogHelpers.h"

BEGIN_MESSAGE_MAP(CSettingsDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_SETTINGS_OK, OnOK)
    ON_BN_CLICKED(IDC_BTN_SETTINGS_CANCEL, OnCancel)
END_MESSAGE_MAP()

// 显示全局设置副本，控件布局保持由资源编辑器定义。
BOOL CSettingsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    HICON hIcon = AfxGetApp()->LoadIcon(IDI_ICON_APP);
    SetIcon(hIcon, TRUE);
    SetIcon(hIcon, FALSE);
    CheckDlgButton(IDC_CHK_AUTORUN_AT_LOGIN, m_stConfig.m_bAutoStart);
    CheckDlgButton(IDC_CHK_START_HIDDEN_TO_TRAY, m_stConfig.m_bStartMinimized);
    CheckDlgButton(IDC_CHK_PREVENT_AUTO_SLEEP, m_stConfig.m_bPreventSleep);
    SetDlgItemInt(IDC_EDIT_LOG_FILE_MAX_SIZE_MB, m_stConfig.m_unLogMaxSizeMb);
    SetDlgItemInt(IDC_EDIT_LOG_ARCHIVE_FILE_LIMIT, m_stConfig.m_unLogArchiveCount);
    SetDlgItemInt(IDC_EDIT_UI_LOG_LINE_LIMIT, m_stConfig.m_unUiMaxLines);
    m_toolTip.Create(this, TTS_ALWAYSTIP);
    m_toolTip.SetMaxTipWidth(480);
    m_toolTip.SetDelayTime(TTDT_INITIAL, 200);
    m_toolTip.SetDelayTime(TTDT_RESHOW, 100);
    AddParameterTip(m_toolTip, this, IDC_CHK_AUTORUN_AT_LOGIN, L"当前 Windows 用户登录后自动运行上传工具。");
    AddParameterTip(m_toolTip, this, IDC_CHK_START_HIDDEN_TO_TRAY, L"程序启动时自动隐藏主窗口，仅在系统托盘显示；可双击托盘图标恢复窗口。");
    AddParameterTip(m_toolTip, this, IDC_CHK_PREVENT_AUTO_SLEEP, L"程序运行期间阻止系统因空闲而自动睡眠；不拦截用户手动选择睡眠或关机。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_LOG_FILE_MAX_SIZE_MB, L"单个日志文件的大小上限，单位为 MB，范围为 1–1024 MB。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_LOG_ARCHIVE_FILE_LIMIT, L"系统 TXT 日志和每个任务 TXT 日志分别保留的归档数量，范围为 1–1000 个，超出删除最旧归档；上传明细与任务日志保存在一起。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_UI_LOG_LINE_LIMIT, L"界面最多保留的日志行数，范围为 100–10000 行。");
    AddParameterTip(m_toolTip, this, IDC_BTN_SETTINGS_OK, L"保存系统设置。");
    AddParameterTip(m_toolTip, this, IDC_BTN_SETTINGS_CANCEL, L"取消本次修改。");
    m_toolTip.Activate(TRUE);
    return TRUE;
}

BOOL CSettingsDlg::PreTranslateMessage(MSG* pMessage)
{
    if (m_toolTip.GetSafeHwnd())
        m_toolTip.RelayEvent(pMessage);
    return CDialog::PreTranslateMessage(pMessage);
}

// 逐项转换并复用全局校验，非法输入只提示第一项对应参数。
void CSettingsDlg::OnOK()
{
    BOOL bUiLines;
    BOOL bMaxSize;
    BOOL bArchiveCount;
    m_stConfig.m_unLogMaxSizeMb = GetDlgItemInt(IDC_EDIT_LOG_FILE_MAX_SIZE_MB, &bMaxSize, FALSE);
    m_stConfig.m_unLogArchiveCount = GetDlgItemInt(IDC_EDIT_LOG_ARCHIVE_FILE_LIMIT, &bArchiveCount, FALSE);
    m_stConfig.m_unUiMaxLines = GetDlgItemInt(IDC_EDIT_UI_LOG_LINE_LIMIT, &bUiLines, FALSE);
    if (!bMaxSize)
        m_stConfig.m_unLogMaxSizeMb = UINT(-1);
    if (!bArchiveCount)
        m_stConfig.m_unLogArchiveCount = UINT(-1);
    if (!bUiLines)
        m_stConfig.m_unUiMaxLines = UINT(-1);
    CString strMessage;
    if (!m_stConfig.Validate(strMessage))
    {
        if (m_pLogger)
            m_pLogger->Operation(L"校验系统设置", L"失败：" + strMessage, NULL, 2);
        AfxMessageBox(strMessage);
        return;
    }
    m_stConfig.m_bAutoStart = IsDlgButtonChecked(IDC_CHK_AUTORUN_AT_LOGIN) == BST_CHECKED;
    m_stConfig.m_bStartMinimized = IsDlgButtonChecked(IDC_CHK_START_HIDDEN_TO_TRAY) == BST_CHECKED;
    m_stConfig.m_bPreventSleep = IsDlgButtonChecked(IDC_CHK_PREVENT_AUTO_SLEEP) == BST_CHECKED;
    CDialog::OnOK();
}
