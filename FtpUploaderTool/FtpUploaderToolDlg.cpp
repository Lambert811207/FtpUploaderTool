#include "stdafx.h"
#include "FtpUploaderToolDlg.h"
static UINT m_unTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
static UINT m_unShowInstance = RegisterWindowMessageW(L"FtpUploaderTool_Show");
static const UINT m_unTray = WM_APP + 1;

// 根据字体测量和权重分配列宽，不改变主窗口控件的资源布局。
static void FitColumns(CListCtrl& listControl, const int* arrWeights, int nCount)
{
    CRect rectClient;
    listControl.GetClientRect(&rectClient);
    CClientDC dcHeader(listControl.GetHeaderCtrl());
    CFont* pFont = listControl.GetHeaderCtrl()->GetFont();
    CFont* pOldFont = pFont ? dcHeader.SelectObject(pFont) : NULL;
    int nPadding = GetSystemMetrics(SM_CXEDGE) * 4 + 16;
    std::vector<int> vecMinimums(nCount);
    int nTotalMinimum = 0;
    int nTotalWeight = 0;
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
    {
        WCHAR szTitle[128] = {0};
        LVCOLUMN stColumn = {0};
        stColumn.mask = LVCF_TEXT;
        stColumn.pszText = szTitle;
        stColumn.cchTextMax = _countof(szTitle);
        listControl.GetColumn(nIndex, &stColumn);
        int nMinimum = dcHeader.GetTextExtent(szTitle).cx + nPadding;
        if (nIndex == 0 && (listControl.GetExtendedStyle() & LVS_EX_CHECKBOXES))
            nMinimum += GetSystemMetrics(SM_CXMENUCHECK) + 4;
        vecMinimums[nIndex] = max(nMinimum, listControl.GetColumnWidth(nIndex));
        nTotalMinimum += vecMinimums[nIndex];
        nTotalWeight += arrWeights[nIndex];
    }
    if (pOldFont)
        dcHeader.SelectObject(pOldFont);
    int nExtra = max(0, rectClient.Width() - GetSystemMetrics(SM_CXVSCROLL) - nTotalMinimum);
    for (int nIndex = 0; nIndex < nCount; ++nIndex)
        listControl.SetColumnWidth(nIndex, vecMinimums[nIndex] + nExtra * arrWeights[nIndex] / nTotalWeight);
}

BEGIN_MESSAGE_MAP(CFtpUploaderToolDlg, CDialog)
    ON_COMMAND_RANGE(IDC_BTN_ADD_TASK, IDC_BTN_OPEN_LOG, OnAction)
    ON_WM_TIMER()
    ON_WM_SYSCOMMAND()
    ON_WM_QUERYENDSESSION()
    ON_WM_ENDSESSION()
    ON_WM_DESTROY()
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_TASKS, OnDoubleClick)
    ON_NOTIFY(LVN_ITEMCHANGING, IDC_LIST_TASKS, OnTaskItemChanging)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_TASKS, OnTaskCheckChanged)
    ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST_LOGS, OnLogInfo)
    ON_NOTIFY(LVN_ENDSCROLL, IDC_LIST_LOGS, OnLogScroll)
    ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnLogTip)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_LOGS, OnLogDraw)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_TASKS, OnTaskDraw)
    ON_CBN_SELCHANGE(IDC_CMB_LOG_TASK, OnFilter)
    ON_CBN_SELCHANGE(IDC_CMB_LOG_LEVEL, OnFilter)
    ON_BN_CLICKED(IDC_CHK_AUTOSCROLL, OnAutoScroll)
    ON_MESSAGE(WM_APP + 1, OnTray)
    ON_REGISTERED_MESSAGE(m_unTaskbarCreated, OnTaskbarCreated)
    ON_REGISTERED_MESSAGE(m_unShowInstance, OnShowInstance)
    ON_COMMAND(IDM_MENU_SHOW, OnTrayShow)
    ON_COMMAND(IDM_MENU_EXIT, OnExit)
END_MESSAGE_MAP()

CFtpUploaderToolDlg::CFtpUploaderToolDlg(CWnd* pParent)
    : CDialog(IDD_DLG_MAIN, pParent),
      m_strRoot(ExeDir()),
      m_managerTasks(m_strRoot, JoinPath(JoinPath(m_strRoot, L"Config"), L"FtpUploaderTool.ini"), m_loggerFile),
      m_unLogTipId(0),
      m_bClosing(false),
      m_bLoggerStopping(false),
      m_bUpdatingChecks(false),
      m_bTaskListLocked(false),
      m_bStoppingTasks(false),
      m_dwExitTick(0),
      m_hIcon(NULL)
{
}

void CFtpUploaderToolDlg::DoDataExchange(CDataExchange* pExchange)
{
    CDialog::DoDataExchange(pExchange);
    DDX_Control(pExchange, IDC_LIST_TASKS, m_listTasks);
    DDX_Control(pExchange, IDC_LIST_LOGS, m_listLogs);
    DDX_Control(pExchange, IDC_CMB_LOG_TASK, m_cmbLogTask);
    DDX_Control(pExchange, IDC_CMB_LOG_LEVEL, m_cmbLogLevel);
}

// 加载配置并初始化后台服务、列表及托盘，最后启动已勾选的旧任务。
BOOL CFtpUploaderToolDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_hIcon = AfxGetApp()->LoadIcon(IDI_ICON_APP);
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    // 固定窗口不提供调整大小或最大化命令，最小化和托盘功能保留。
    CMenu* pSystemMenu = GetSystemMenu(FALSE);
    if (pSystemMenu)
    {
        pSystemMenu->DeleteMenu(SC_SIZE, MF_BYCOMMAND);
        pSystemMenu->DeleteMenu(SC_MAXIMIZE, MF_BYCOMMAND);
    }
    m_listTasks.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
    LPCWSTR arrColumns[] = {L"名称",     L"服务器",   L"状态",     L"待传",    L"搁置",
                            L"今日成功", L"今日失败", L"最后上传", L"最后错误"};
    int arrWidths[] = {125, 130, 150, 55, 55, 80, 80, 100, 240};
    for (int nIndex = 0; nIndex < _countof(arrColumns); ++nIndex)
        m_listTasks.InsertColumn(nIndex, arrColumns[nIndex], LVCFMT_LEFT, 0);
    FitColumns(m_listTasks, arrWidths, _countof(arrColumns));
    m_listLogs.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    CToolTipCtrl* pDefaultLogTip = m_listLogs.SetToolTips(NULL);
    if (pDefaultLogTip)
        pDefaultLogTip->DestroyWindow();
    m_listLogs.InsertColumn(0, L"时间", LVCFMT_LEFT, m_listLogs.GetStringWidth(L"0000-00-00 00:00:00.000") + 24);
    m_listLogs.InsertColumn(1, L"任务", LVCFMT_LEFT, 0);
    m_listLogs.InsertColumn(2, L"级别", LVCFMT_LEFT, m_listLogs.GetStringWidth(L"ERROR") + 24);
    m_listLogs.InsertColumn(3, L"内容", LVCFMT_LEFT, 0);
    int arrLogWidths[] = {14, 15, 8, 63};
    FitColumns(m_listLogs, arrLogWidths, _countof(arrLogWidths));
    m_cmbLogLevel.AddString(L"DEBUG 及以上");
    m_cmbLogLevel.AddString(L"INFO 及以上");
    m_cmbLogLevel.AddString(L"WARN 及以上");
    m_cmbLogLevel.AddString(L"ERROR");
    m_cmbLogLevel.SetCurSel(1);
    CheckDlgButton(IDC_CHK_AUTOSCROLL, BST_CHECKED);
    CString strMessage;
    bool bLoaded = m_managerTasks.Load(m_stGlobal, strMessage);
    if (!bLoaded)
        AfxMessageBox(strMessage, MB_ICONERROR);
    m_loggerFile.Start(JoinPath(m_strRoot, L"Log"), m_stGlobal);
    if (!m_tempCleanup.Start(m_strRoot, m_loggerFile))
        m_loggerFile.Write(NULL, 3, L"远程临时文件后台清理线程启动失败，待清理记录将在下次启动时重试");
    if (!strMessage.IsEmpty())
        m_loggerFile.Write(NULL, 2, strMessage);
    CString strLoadResult;
    strLoadResult.Format(L"%s；任务数=%u", bLoaded ? L"加载成功" : L"加载失败", (UINT)m_managerTasks.Count());
    m_loggerFile.Operation(L"加载配置", strLoadResult, NULL, bLoaded ? 1 : 3);
    m_toolTip.Create(this, TTS_ALWAYSTIP);
    m_toolTip.SetMaxTipWidth(480);
    m_toolTip.SetDelayTime(TTDT_INITIAL, 200);
    m_toolTip.SetDelayTime(TTDT_RESHOW, 100);
    m_toolTip.AddTool(&m_listTasks, L"勾选任务表示参与上传；任务运行期间列表锁定，全部停止后可修改。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_ADD_TASK), L"新增任务，最多支持 32 个任务。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_DELETE_TASK), L"删除当前选中的任务，请先停止全部任务。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_START_ALL), L"启动所有已勾选的任务。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_STOP_ALL), L"停止全部任务，停止完成后可修改任务勾选状态。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_SYSTEM_SETTINGS), L"打开系统设置。");
    m_toolTip.AddTool(GetDlgItem(IDC_CMB_LOG_TASK), L"按任务筛选日志。");
    m_toolTip.AddTool(GetDlgItem(IDC_CMB_LOG_LEVEL), L"按日志级别筛选日志。");
    m_toolTip.AddTool(GetDlgItem(IDC_CHK_AUTOSCROLL), L"新日志到达时自动滚动到底部。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_CLEAR_LOG), L"清空当前界面的日志。");
    m_toolTip.AddTool(GetDlgItem(IDC_BTN_OPEN_LOG), L"打开日志文件目录。");
    m_toolTip.Activate(TRUE);
    m_toolTipLog.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
    m_toolTipLog.SetMaxTipWidth(800);
    m_toolTipLog.SetDelayTime(TTDT_INITIAL, 200);
    m_toolTipLog.SetDelayTime(TTDT_RESHOW, 100);
    m_toolTipLog.Activate(TRUE);
    RebuildTasks();
    ApplyGlobal();
    Tray(true);
    SetTimer(1, 200, NULL);
    SetTimer(2, 500, NULL);
    SetTimer(3, 100, NULL);
    m_managerTasks.StartSelected(L"程序启动后自动启动");
    RefreshStatus();
    m_loggerFile.Write(NULL, 1, L"程序已启动");
    return TRUE;
}

int CFtpUploaderToolDlg::Selected() const
{
    POSITION posPosition = m_listTasks.GetFirstSelectedItemPosition();
    return posPosition ? m_listTasks.GetNextSelectedItem(posPosition) : -1;
}

bool CFtpUploaderToolDlg::Save()
{
    CString strMessage;
    if (!m_managerTasks.Save(m_stGlobal, strMessage))
    {
        m_loggerFile.Operation(L"保存配置", L"失败：" + strMessage, NULL, 3);
        AfxMessageBox(strMessage, MB_ICONERROR);
        return false;
    }
    return true;
}

bool CFtpUploaderToolDlg::CheckConflict(const TaskConfig& stConfig, int nExcept)
{
    CString strMessage;
    if (m_managerTasks.CheckConflict(stConfig, nExcept, strMessage))
        return true;
    m_loggerFile.Operation(L"校验任务配置", L"失败：" + strMessage, &stConfig, 2);
    AfxMessageBox(strMessage);
    return false;
}

// 刷新任务与筛选项时屏蔽复选框通知，防止程序填充触发保存。
void CFtpUploaderToolDlg::RebuildTasks()
{
    m_bUpdatingChecks = true;
    m_listTasks.DeleteAllItems();
    m_cmbLogTask.ResetContent();
    m_cmbLogTask.AddString(L"全部");
    m_cmbLogTask.AddString(L"系统");
    for (size_t nIndex = 0; nIndex < m_managerTasks.Count(); ++nIndex)
    {
        m_listTasks.InsertItem((int)nIndex, m_managerTasks.Config(nIndex).m_strName);
        m_listTasks.SetCheck((int)nIndex, m_managerTasks.Config(nIndex).m_bSelected);
        m_cmbLogTask.AddString(m_managerTasks.Config(nIndex).m_strName);
        CString strAddress;
        strAddress.Format(L"%s:%u", (LPCWSTR)m_managerTasks.Config(nIndex).m_strHost, m_managerTasks.Config(nIndex).m_unPort);
        m_listTasks.SetItemText((int)nIndex, 1, strAddress);
    }
    m_cmbLogTask.SetCurSel(0);
    m_bUpdatingChecks = false;
    RefreshStatus();
    RebuildLogView();
}

// 编辑只在任务全部停止时进入，保存失败不替换原配置。
void CFtpUploaderToolDlg::Edit(int nIndex)
{
    if (m_bClosing || m_bTaskListLocked || nIndex < 0 || nIndex >= (int)m_managerTasks.Count())
        return;
    m_loggerFile.Operation(L"编辑任务", L"打开配置", &m_managerTasks.Config(nIndex));
    CTaskEditDlg dlgDialog(m_managerTasks.Config(nIndex), this, &m_loggerFile);
    INT_PTR nDialogResult = dlgDialog.DoModal();
    if (nDialogResult != IDOK)
    {
        m_loggerFile.Operation(L"编辑任务", nDialogResult == IDCANCEL ? L"已取消" : L"失败：无法打开配置窗口",
                               &m_managerTasks.Config(nIndex), nDialogResult == IDCANCEL ? 1 : 3);
        return;
    }
    TaskConfig stConfig = dlgDialog.Config();
    if (!CheckConflict(stConfig, nIndex))
    {
        m_loggerFile.Operation(L"编辑任务", L"失败：任务配置冲突", &stConfig, 2);
        return;
    }
    TaskConfig stOld = m_managerTasks.Config(nIndex);
    CString strMessage;
    if (!m_managerTasks.Update(nIndex, stConfig, m_stGlobal, strMessage))
    {
        m_loggerFile.Operation(L"保存配置", L"失败：" + strMessage, NULL, 3);
        AfxMessageBox(strMessage, MB_ICONERROR);
        m_loggerFile.Operation(L"编辑任务", L"失败：配置保存失败", &stConfig, 3);
        return;
    }
    CString strEditResult = L"已保存；原名称=" + stOld.m_strName;
    if (stOld.m_strPassword != stConfig.m_strPassword)
        strEditResult += L"；密码已修改";
    CString strCleanupSettings;
    strCleanupSettings.Format(L"；自动删除旧空文件夹=%s；超过=%u 天；依据=目录最后修改时间",
                              stConfig.m_bDeleteOldEmptyDirs ? L"开启" : L"关闭", stConfig.m_unEmptyDirAgeDays);
    strEditResult += strCleanupSettings;
    m_loggerFile.Operation(L"编辑任务", strEditResult, &stConfig);
    RebuildTasks();
    if (m_managerTasks.Config(nIndex).m_bSelected)
        m_managerTasks.Start(nIndex, L"编辑任务后自动启动");
    RefreshStatus();
}

// 界面命令入口，负责交互反馈，任务数据修改交由管理器提交。
void CFtpUploaderToolDlg::OnAction(UINT unId)
{
    if (m_bClosing)
        return;
    if (unId == IDC_BTN_ADD_TASK)
    {
        if (m_bTaskListLocked)
            return;
        if (m_managerTasks.Count() >= m_unMaxTasks)
        {
            m_loggerFile.Operation(L"新增任务", L"失败：任务数量已达到 32 个", NULL, 2);
            AfxMessageBox(L"最多支持 32 个任务");
            return;
        }
        TaskConfig stConfig;
        m_loggerFile.Operation(L"新增任务", L"打开配置");
        CTaskEditDlg dlgDialog(stConfig, this, &m_loggerFile);
        INT_PTR nDialogResult = dlgDialog.DoModal();
        if (nDialogResult != IDOK)
        {
            m_loggerFile.Operation(L"新增任务", nDialogResult == IDCANCEL ? L"已取消" : L"失败：无法打开配置窗口",
                                   NULL, nDialogResult == IDCANCEL ? 1 : 3);
            return;
        }
        stConfig = dlgDialog.Config();
        if (!CheckConflict(stConfig, -1))
        {
            m_loggerFile.Operation(L"新增任务", L"失败：任务配置冲突", &stConfig, 2);
            return;
        }
        CString strMessage;
        if (!m_managerTasks.Add(stConfig, m_stGlobal, strMessage))
        {
            m_loggerFile.Operation(L"保存配置", L"失败：" + strMessage, NULL, 3);
            AfxMessageBox(strMessage, MB_ICONERROR);
            m_loggerFile.Operation(L"新增任务", L"失败：配置保存失败", &stConfig, 3);
            return;
        }
        CString strAddResult;
        strAddResult.Format(L"已保存；自动删除旧空文件夹=%s；超过=%u 天；依据=目录最后修改时间",
                            stConfig.m_bDeleteOldEmptyDirs ? L"开启" : L"关闭", stConfig.m_unEmptyDirAgeDays);
        m_loggerFile.Operation(L"新增任务", strAddResult, &stConfig);
        RebuildTasks();
        RefreshStatus();
    }
    else if (unId == IDC_BTN_DELETE_TASK)
    {
        if (m_bTaskListLocked)
            return;
        int nIndex = Selected();
        if (nIndex < 0 || nIndex >= (int)m_managerTasks.Count())
            return;
        CString strMessage;
        strMessage.Format(L"确定删除任务“%s”？\n仅移除任务配置，保留本地文件。",
                          (LPCWSTR)m_managerTasks.Config(nIndex).m_strName);
        m_loggerFile.Operation(L"删除任务", L"请求删除", &m_managerTasks.Config(nIndex));
        if (AfxMessageBox(strMessage, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        {
            m_loggerFile.Operation(L"删除任务", L"已取消", &m_managerTasks.Config(nIndex));
            return;
        }
        TaskConfig stDeleted = m_managerTasks.Config(nIndex);
        if (!m_managerTasks.Remove(nIndex, m_stGlobal, strMessage))
        {
            m_loggerFile.Operation(L"删除任务", L"失败：" + strMessage, &stDeleted, 3);
            AfxMessageBox(strMessage, MB_ICONERROR);
            return;
        }
        m_loggerFile.Operation(L"删除任务", L"已删除配置，保留本地文件", &stDeleted);
        RebuildTasks();
        if (m_managerTasks.Count() != 0)
            m_listTasks.SetItemState(min(nIndex, (int)m_managerTasks.Count() - 1),
                                    LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    else if (unId == IDC_BTN_START_ALL || unId == IDC_BTN_STOP_ALL)
    {
        if (!GetDlgItem(unId)->IsWindowEnabled())
            return;
        GetDlgItem(unId)->EnableWindow(FALSE);
        if (unId == IDC_BTN_STOP_ALL)
            m_bStoppingTasks = true;
        m_loggerFile.Operation(unId == IDC_BTN_STOP_ALL ? L"全部停止" : L"全部启动", L"已请求");
        UINT unCount = unId == IDC_BTN_STOP_ALL ? m_managerTasks.RequestStopAll(L"已请求停止")
                                               : m_managerTasks.StartSelected(L"全部启动");
        CString strBatchResult;
        strBatchResult.Format(unId == IDC_BTN_STOP_ALL ? L"已请求停止 %u 个运行任务" : L"已启动 %u 个勾选任务", unCount);
        m_loggerFile.Operation(unId == IDC_BTN_STOP_ALL ? L"全部停止" : L"全部启动", strBatchResult);
        RefreshStatus();
    }
    else if (unId == IDC_BTN_CLEAR_LOG)
    {
        CString strResult;
        strResult.Format(L"已清空界面中的 %u 条日志，磁盘日志保留", (UINT)m_deqUiLogs.size());
        m_deqUiLogs.clear();
        RebuildLogView();
        m_loggerFile.Operation(L"清屏", strResult);
    }
    else if (unId == IDC_BTN_OPEN_LOG)
    {
        CString strPath = JoinPath(m_strRoot, L"Log");
        if (!MakeDirs(strPath))
            m_loggerFile.Operation(L"打开日志目录", L"失败：无法创建日志目录", NULL, 3);
        else
        {
            INT_PTR nResult = (INT_PTR)ShellExecuteW(m_hWnd, L"open", strPath, NULL, NULL, SW_SHOWNORMAL);
            CString strResult;
            strResult.Format(L"%s；目录=%s；返回值=%Id", nResult > 32 ? L"已打开" : L"打开失败", (LPCWSTR)strPath, nResult);
            m_loggerFile.Operation(L"打开日志目录", strResult, NULL, nResult > 32 ? 1 : 3);
        }
    }
    else if (unId == IDC_BTN_SYSTEM_SETTINGS)
    {
        m_loggerFile.Operation(L"系统设置", L"打开设置");
        CSettingsDlg dlgDialog(m_stGlobal, this, &m_loggerFile);
        INT_PTR nDialogResult = dlgDialog.DoModal();
        if (nDialogResult == IDOK)
        {
            GlobalConfig stOld = m_stGlobal;
            m_stGlobal = dlgDialog.m_stConfig;
            if (!Save())
            {
                m_stGlobal = stOld;
                m_loggerFile.Operation(L"系统设置", L"失败：配置保存失败", NULL, 3);
            }
            else
            {
                ApplyGlobal();
                m_loggerFile.Configure(m_stGlobal);
                CString strResult;
                strResult.Format(L"已保存；登录自启动=%u；启动隐藏到托盘=%u；阻止自动睡眠=%u；单个日志大小上限=%u MB；归档文件上限=%u；界面日志行数上限=%u",
                                 (UINT)m_stGlobal.m_bAutoStart, (UINT)m_stGlobal.m_bStartMinimized,
                                 (UINT)m_stGlobal.m_bPreventSleep, m_stGlobal.m_unLogMaxSizeMb,
                                 m_stGlobal.m_unLogArchiveCount, m_stGlobal.m_unUiMaxLines);
                m_loggerFile.Operation(L"系统设置", strResult);
                AfxMessageBox(L"设置已保存，日志设置已生效。");
            }
        }
        else
            m_loggerFile.Operation(L"系统设置", nDialogResult == IDCANCEL ? L"已取消" : L"失败：无法打开设置窗口",
                                   NULL, nDialogResult == IDCANCEL ? 1 : 3);
    }
}

// 读取工作线程快照更新文字及按钮状态，不在界面执行 FTP 操作。
void CFtpUploaderToolDlg::RefreshStatus()
{
    bool bRunning = false;
    bool bHasSelected = false;
    for (size_t nIndex = 0; nIndex < m_managerTasks.Count(); ++nIndex)
    {
        if (m_managerTasks.Running(nIndex))
            bRunning = true;
        if (m_managerTasks.Config(nIndex).m_bSelected)
            bHasSelected = true;
        TaskStatus stStatus = m_managerTasks.Status(nIndex);
        CString arrValues[7];
        arrValues[0] = stStatus.m_strText;
        if (stStatus.m_nState == CS_UPLOADING && stStatus.m_ullTotal)
        {
            CString strProgress;
            strProgress.Format(L" %u%% %s", (UINT)(stStatus.m_ullSent * 100.0 / stStatus.m_ullTotal),
                               (LPCWSTR)stStatus.m_strCurrentFile);
            arrValues[0] += strProgress;
        }
        arrValues[1].Format(L"%u", stStatus.m_unPending);
        arrValues[2].Format(L"%u", stStatus.m_unParked);
        arrValues[3].Format(L"%u", stStatus.m_unTodayOk);
        arrValues[4].Format(L"%u", stStatus.m_unTodayFail);
        arrValues[5] = stStatus.m_strLastOk;
        arrValues[6] = stStatus.m_strLastError;
        for (int nColumn = 0; nColumn < 7; ++nColumn)
            if (m_listTasks.GetItemText((int)nIndex, nColumn + 2) != arrValues[nColumn])
                m_listTasks.SetItemText((int)nIndex, nColumn + 2, arrValues[nColumn]);
    }
    if (!bRunning)
        m_bStoppingTasks = false;
    m_bTaskListLocked = m_bClosing || bRunning;
    GetDlgItem(IDC_BTN_START_ALL)->EnableWindow(!m_bTaskListLocked && bHasSelected);
    GetDlgItem(IDC_BTN_STOP_ALL)->EnableWindow(!m_bClosing && bRunning && !m_bStoppingTasks);
    GetDlgItem(IDC_BTN_ADD_TASK)->EnableWindow(!m_bTaskListLocked && m_managerTasks.Count() < m_unMaxTasks);
    GetDlgItem(IDC_BTN_DELETE_TASK)->EnableWindow(!m_bTaskListLocked && Selected() >= 0);
    Tray(false);
}

// 批量合并待显示日志并裁剪到配置上限，自动滚动选中时持续跟随末行。
void CFtpUploaderToolDlg::RefreshLogs()
{
    std::deque<LogItem> deqItems;
    m_loggerFile.PopUi(deqItems);
    if (deqItems.empty())
        return;
    for (size_t nIndex = 0; nIndex < deqItems.size(); ++nIndex)
        m_deqUiLogs.push_back(deqItems[nIndex]);
    while (m_deqUiLogs.size() > m_stGlobal.m_unUiMaxLines)
        m_deqUiLogs.pop_front();
    RebuildLogView();
    if (IsDlgButtonChecked(IDC_CHK_AUTOSCROLL) == BST_CHECKED && !m_vecView.empty())
        m_listLogs.EnsureVisible((int)m_vecView.size() - 1, FALSE);
}

// 只建立可见行索引，虚拟列表按需要读取文本，避免复制全部单元格。
void CFtpUploaderToolDlg::RebuildLogView()
{
    m_vecView.clear();
    int nTaskIndex = m_cmbLogTask.GetCurSel();
    int nLevel = max(0, m_cmbLogLevel.GetCurSel());
    for (size_t nIndex = 0; nIndex < m_deqUiLogs.size(); ++nIndex)
    {
        const LogItem& stItem = m_deqUiLogs[nIndex];
        bool bMatch = nTaskIndex <= 0 || (nTaskIndex == 1 ? stItem.m_strId.IsEmpty()
                                                          : nTaskIndex - 2 < (int)m_managerTasks.Count() &&
                                                                stItem.m_strId == m_managerTasks.Config(nTaskIndex - 2).m_strId);
        if (bMatch && stItem.m_nLevel >= nLevel)
            m_vecView.push_back((int)nIndex);
    }
    m_listLogs.SetItemCountEx((int)m_vecView.size(), LVSICF_NOSCROLL);
    m_listLogs.Invalidate(FALSE);
    UpdateLogTip();
}

// 定时刷新界面并轮询后台退出，正常关闭路径不在界面同步等待线程。
void CFtpUploaderToolDlg::OnTimer(UINT_PTR unId)
{
    if (unId == 1)
    {
        RefreshLogs();
        UpdateLogTip();
    }
    if (unId == 2)
    {
        bool bAllStopped = m_managerTasks.PollStopped();
        if (m_bStoppingTasks && bAllStopped)
            m_loggerFile.Operation(L"全部停止", L"所有任务已停止");
        RefreshStatus();
        // 任务与远端清理结束后才停止日志线程，保证收尾日志先入队。
        if (m_bClosing && bAllStopped && m_tempCleanup.PollStopped())
        {
            if (!m_bLoggerStopping)
            {
                m_loggerFile.Write(NULL, 1, L"程序正常退出");
                m_loggerFile.Operation(L"退出程序", L"所有任务已停止，正常退出");
                m_loggerFile.RequestStop();
                m_bLoggerStopping = true;
            }
            if (m_loggerFile.PollStopped())
            {
                EndDialog(IDOK);
                return;
            }
        }
        if (m_bClosing && TickElapsed(m_dwExitTick, 8000))
        {
            m_loggerFile.Write(NULL, 3, L"退出等待超时，由系统回收进程资源");
            ExitProcess(2);
        }
    }
    if (unId == 3)
    {
        KillTimer(3);
        if (m_stGlobal.m_bStartMinimized)
        {
            ShowWindow(SW_HIDE);
            m_loggerFile.Operation(L"启动隐藏到托盘", L"已隐藏到托盘");
        }
    }
    CDialog::OnTimer(unId);
}

void CFtpUploaderToolDlg::OnDoubleClick(NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    if (!m_bClosing && !m_bTaskListLocked)
        Edit(Selected());
}

void CFtpUploaderToolDlg::OnTaskItemChanging(NMHDR* pHeader, LRESULT* pResult)
{
    NMLISTVIEW* pChange = (NMLISTVIEW*)pHeader;
    *pResult = !m_bUpdatingChecks && m_bTaskListLocked && (pChange->uChanged & LVIF_STATE) ? TRUE : FALSE;
}

// 用户勾选需持久化，保存失败时将复选框恢复到原状态。
void CFtpUploaderToolDlg::OnTaskCheckChanged(NMHDR* pHeader, LRESULT* pResult)
{
    *pResult = 0;
    NMLISTVIEW* pChange = (NMLISTVIEW*)pHeader;
    if (!m_bUpdatingChecks && ((pChange->uOldState ^ pChange->uNewState) & LVIS_SELECTED))
    {
        if ((pChange->uNewState & LVIS_SELECTED) && pChange->iItem >= 0 && pChange->iItem < (int)m_managerTasks.Count())
            m_loggerFile.Operation(L"选择任务", L"已选中", &m_managerTasks.Config(pChange->iItem));
        RefreshStatus();
    }
    if (m_bUpdatingChecks || pChange->iItem < 0 || pChange->iItem >= (int)m_managerTasks.Count() ||
        !(pChange->uChanged & LVIF_STATE) ||
        !((pChange->uOldState ^ pChange->uNewState) & LVIS_STATEIMAGEMASK))
        return;
    bool bSelected = m_listTasks.GetCheck(pChange->iItem) != FALSE;
    bool bOldSelected = m_managerTasks.Config(pChange->iItem).m_bSelected;
    if (bSelected == bOldSelected)
        return;
    CString strMessage;
    if (!m_managerTasks.SetSelected(pChange->iItem, bSelected, m_stGlobal, strMessage))
    {
        m_loggerFile.Operation(L"保存配置", L"失败：" + strMessage, NULL, 3);
        AfxMessageBox(strMessage, MB_ICONERROR);
        m_bUpdatingChecks = true;
        m_listTasks.SetCheck(pChange->iItem, bOldSelected);
        m_bUpdatingChecks = false;
        m_loggerFile.Operation(L"任务上传勾选", L"失败：配置保存失败，已恢复原状态", &m_managerTasks.Config(pChange->iItem), 3);
    }
    else
        m_loggerFile.Operation(L"任务上传勾选", bSelected ? L"已勾选，参与上传" : L"已取消勾选", &m_managerTasks.Config(pChange->iItem));
    RefreshStatus();
}

CString CFtpUploaderToolDlg::LogCellText(int nRow, int nColumn) const
{
    if (nRow < 0 || nRow >= (int)m_vecView.size())
        return CString();
    const LogItem& stLog = m_deqUiLogs[m_vecView[nRow]];
    LPCWSTR arrLevels[] = {L"DEBUG", L"INFO", L"WARN", L"ERROR"};
    switch (nColumn)
    {
        case 0:
            return stLog.m_strTime;
        case 1:
            return stLog.m_strName;
        case 2:
            return arrLevels[stLog.m_nLevel];
        case 3:
            return stLog.m_strText;
    }
    return CString();
}

void CFtpUploaderToolDlg::OnLogInfo(NMHDR* pHeader, LRESULT* pResult)
{
    LVITEMW& stItem = ((NMLVDISPINFOW*)pHeader)->item;
    *pResult = 0;
    if (stItem.mask & LVIF_TEXT)
        lstrcpynW(stItem.pszText, LogCellText(stItem.iItem, stItem.iSubItem), stItem.cchTextMax);
}

// 根据鼠标所在行列更新完整文本，滚动或静止鼠标下数据变化也要同步。
void CFtpUploaderToolDlg::UpdateLogTip()
{
    if (!m_toolTipLog.GetSafeHwnd())
        return;
    CPoint pointCursor;
    GetCursorPos(&pointCursor);
    m_listLogs.ScreenToClient(&pointCursor);
    CRect rectClient;
    m_listLogs.GetClientRect(&rectClient);
    LVHITTESTINFO stHit = {0};
    stHit.pt = pointCursor;
    int nRow = rectClient.PtInRect(pointCursor) ? m_listLogs.SubItemHitTest(&stHit) : -1;
    CString strText = LogCellText(nRow, stHit.iSubItem);
    UINT_PTR unTipId = strText.IsEmpty() ? 0 : (UINT_PTR)nRow * 4 + stHit.iSubItem + 1;
    if (unTipId != m_unLogTipId)
    {
        m_toolTipLog.Pop();
        if (m_unLogTipId)
            m_toolTipLog.DelTool(&m_listLogs);
        m_unLogTipId = unTipId;
        m_strLogTipText = strText;
        if (unTipId)
            m_toolTipLog.AddTool(&m_listLogs, LPSTR_TEXTCALLBACK);
    }
    else if (m_strLogTipText != strText)
    {
        m_strLogTipText = strText;
        m_toolTipLog.UpdateTipText(LPSTR_TEXTCALLBACK, &m_listLogs);
    }
}

BOOL CFtpUploaderToolDlg::OnLogTip(UINT, NMHDR* pHeader, LRESULT* pResult)
{
    if (pHeader->hwndFrom != m_toolTipLog.GetSafeHwnd())
        return FALSE;
    ((NMTTDISPINFO*)pHeader)->lpszText = (LPWSTR)(LPCWSTR)m_strLogTipText;
    *pResult = 0;
    return TRUE;
}

void CFtpUploaderToolDlg::OnLogScroll(NMHDR*, LRESULT* pResult)
{
    UpdateLogTip();
    *pResult = 0;
}

void CFtpUploaderToolDlg::OnLogDraw(NMHDR* pHeader, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* pDraw = (NMLVCUSTOMDRAW*)pHeader;
    *pResult = CDRF_DODEFAULT;
    if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
        *pResult = CDRF_NOTIFYITEMDRAW;
    else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && pDraw->nmcd.dwItemSpec < m_vecView.size())
    {
        int nLevel = m_deqUiLogs[m_vecView[pDraw->nmcd.dwItemSpec]].m_nLevel;
        pDraw->clrText = nLevel == 3   ? RGB(190, 30, 30)
                         : nLevel == 2 ? RGB(185, 110, 0)
                         : nLevel == 0 ? RGB(120, 120, 120)
                                       : RGB(30, 30, 30);
    }
}

// 逐列重置颜色，避免复用绘制状态导致普通列继承警告色。
void CFtpUploaderToolDlg::OnTaskDraw(NMHDR* pHeader, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* pDraw = (NMLVCUSTOMDRAW*)pHeader;
    *pResult = CDRF_DODEFAULT;
    if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
        *pResult = CDRF_NOTIFYITEMDRAW;
    else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        pDraw->clrText = GetSysColor(COLOR_WINDOWTEXT);
        *pResult = CDRF_NOTIFYSUBITEMDRAW;
    }
    else if (pDraw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
    {
        pDraw->clrText = GetSysColor(COLOR_WINDOWTEXT);
        if (pDraw->nmcd.dwItemSpec >= m_managerTasks.Count())
            return;
        TaskStatus stStatus = m_managerTasks.Status(pDraw->nmcd.dwItemSpec);
        if (pDraw->iSubItem == 2)
        {
            pDraw->clrText =
                stStatus.m_nState == CS_CONFIG_ERROR || stStatus.m_nState == CS_RECONNECT || stStatus.m_unParked
                    ? RGB(190, 30, 30)
                : stStatus.m_nState == CS_IDLE || stStatus.m_nState == CS_UPLOADING ? RGB(0, 130, 50)
                : stStatus.m_nState == CS_CONNECTING ? RGB(0, 102, 204) : RGB(110, 110, 110);
        }
        else if (pDraw->iSubItem == 8 && !stStatus.m_strLastError.IsEmpty())
            pDraw->clrText = RGB(190, 30, 30);
    }
}

void CFtpUploaderToolDlg::OnFilter()
{
    CString strTask;
    CString strLevel;
    m_cmbLogTask.GetWindowText(strTask);
    m_cmbLogLevel.GetWindowText(strLevel);
    m_loggerFile.Operation(L"筛选日志", L"任务=" + strTask + L"；级别=" + strLevel);
    RebuildLogView();
}

void CFtpUploaderToolDlg::OnAutoScroll()
{
    if (IsDlgButtonChecked(IDC_CHK_AUTOSCROLL) == BST_CHECKED && !m_vecView.empty())
        m_listLogs.EnsureVisible((int)m_vecView.size() - 1, FALSE);
    m_loggerFile.Operation(L"日志自动滚动", IsDlgButtonChecked(IDC_CHK_AUTOSCROLL) == BST_CHECKED ? L"已开启" : L"已关闭");
}

// 运行期间仅拦截任务列表输入，仍允许正常绘制和后台状态刷新。
BOOL CFtpUploaderToolDlg::PreTranslateMessage(MSG* pMessage)
{
    if (pMessage->message == WM_MOUSEMOVE && pMessage->hwnd == m_listLogs.GetSafeHwnd())
        UpdateLogTip();
    if (m_toolTipLog.GetSafeHwnd())
        m_toolTipLog.RelayEvent(pMessage);
    bool bTaskInput = (pMessage->message >= WM_MOUSEFIRST && pMessage->message <= WM_MOUSELAST) ||
                      (pMessage->message >= WM_KEYFIRST && pMessage->message <= WM_KEYLAST) ||
                      (pMessage->message >= WM_NCMOUSEMOVE && pMessage->message <= WM_NCXBUTTONDBLCLK) ||
                      pMessage->message == WM_CONTEXTMENU || pMessage->message == WM_HSCROLL ||
                      pMessage->message == WM_VSCROLL;
    if (m_bTaskListLocked && bTaskInput &&
        (pMessage->hwnd == m_listTasks.GetSafeHwnd() ||
         ::IsChild(m_listTasks.GetSafeHwnd(), pMessage->hwnd)))
        return TRUE;
    if (pMessage->message == WM_KEYDOWN && pMessage->wParam == 'C' && GetKeyState(VK_CONTROL) < 0 &&
        GetFocus() == &m_listLogs)
    {
        CString strText;
        UINT unCount = 0;
        bool bCopied = false;
        POSITION posPos = m_listLogs.GetFirstSelectedItemPosition();
        while (posPos)
        {
            int nIndex = m_listLogs.GetNextSelectedItem(posPos);
            if (nIndex < (int)m_vecView.size())
            {
                const LogItem& stItem = m_deqUiLogs[m_vecView[nIndex]];
                strText += stItem.m_strTime + L" [" + stItem.m_strName + L"] " + stItem.m_strText + L"\r\n";
                ++unCount;
            }
        }
        if (OpenClipboard())
        {
            EmptyClipboard();
            SIZE_T nBytes = (strText.GetLength() + 1) * sizeof(WCHAR);
            HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, nBytes);
            if (hMemory)
            {
                void* pBuffer = GlobalLock(hMemory);
                memcpy(pBuffer, (LPCWSTR)strText, nBytes);
                GlobalUnlock(hMemory);
                if (!SetClipboardData(CF_UNICODETEXT, hMemory))
                    GlobalFree(hMemory);
                else
                    bCopied = true;
            }
            CloseClipboard();
        }
        CString strResult;
        strResult.Format(bCopied ? L"已复制 %u 条日志" : L"复制失败，选择了 %u 条日志", unCount);
        m_loggerFile.Operation(L"复制日志", strResult, NULL, bCopied ? 1 : 3);
        return TRUE;
    }
    if (m_toolTip.GetSafeHwnd())
        m_toolTip.RelayEvent(pMessage);
    return CDialog::PreTranslateMessage(pMessage);
}

void CFtpUploaderToolDlg::Tray(bool bAdd)
{
    UINT unOnline = 0;
    UINT unTotal = 0;
    for (size_t nIndex = 0; nIndex < m_managerTasks.Count(); ++nIndex)
    {
        TaskStatus stStatus = m_managerTasks.Status(nIndex);
        ++unTotal;
        if (stStatus.m_nState == CS_IDLE || stStatus.m_nState == CS_UPLOADING)
            ++unOnline;
    }
    NOTIFYICONDATAW stData = {0};
    stData.cbSize = sizeof(stData);
    stData.hWnd = m_hWnd;
    stData.uID = 1;
    stData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    stData.uCallbackMessage = m_unTray;
    stData.hIcon = m_hIcon;
    CString strTip;
    strTip.Format(L"FTP 上传 %u/%u 在线", unOnline, unTotal);
    lstrcpynW(stData.szTip, strTip, _countof(stData.szTip));
    Shell_NotifyIconW(bAdd ? NIM_ADD : NIM_MODIFY, &stData);
}

LRESULT CFtpUploaderToolDlg::OnTray(WPARAM, LPARAM lParam)
{
    if (lParam == WM_LBUTTONDBLCLK)
        OnTrayShow();
    else if (lParam == WM_RBUTTONUP)
    {
        CMenu menuMenu;
        menuMenu.CreatePopupMenu();
        menuMenu.AppendMenu(MF_STRING, IDM_MENU_SHOW, L"显示主窗口");
        menuMenu.AppendMenu(MF_STRING, IDM_MENU_EXIT, L"退出");
        POINT stPoint;
        GetCursorPos(&stPoint);
        SetForegroundWindow();
        menuMenu.TrackPopupMenu(TPM_RIGHTBUTTON, stPoint.x, stPoint.y, this);
        PostMessage(WM_NULL);
    }
    return 0;
}

LRESULT CFtpUploaderToolDlg::OnTaskbarCreated(WPARAM, LPARAM)
{
    Tray(true);
    m_loggerFile.Operation(L"恢复托盘图标", L"收到系统任务栏重建通知，已重新注册");
    return 0;
}

LRESULT CFtpUploaderToolDlg::OnShowInstance(WPARAM, LPARAM)
{
    m_loggerFile.Operation(L"重复启动程序", L"已有实例，恢复现有主窗口");
    OnTrayShow();
    return 0;
}

void CFtpUploaderToolDlg::OnTrayShow()
{
    ShowWindow(SW_RESTORE);
    SetForegroundWindow();
    m_loggerFile.Operation(L"显示主窗口", L"已恢复窗口");
}

// 关闭按钮和 Esc 隐藏到托盘，真正退出走单独的停止流程。
void CFtpUploaderToolDlg::OnCancel()
{
    ShowWindow(SW_HIDE);
    m_loggerFile.Operation(L"隐藏主窗口", L"关闭按钮或 Esc，已隐藏到托盘");
}

void CFtpUploaderToolDlg::OnSysCommand(UINT unId, LPARAM lParam)
{
    // 即使收到外部系统命令，也不进入调整大小或最大化状态。
    if ((unId & 0xFFF0) == SC_SIZE || (unId & 0xFFF0) == SC_MAXIMIZE)
        return;
    if ((unId & 0xFFF0) == SC_MINIMIZE)
    {
        ShowWindow(SW_HIDE);
        m_loggerFile.Operation(L"最小化主窗口", L"已隐藏到托盘");
    }
    else
        CDialog::OnSysCommand(unId, lParam);
}

// 一次性请求任务与远端清理停止，后续由定时器完成日志排空及退出。
void CFtpUploaderToolDlg::BeginExit(const CString& strReason)
{
    if (m_bClosing)
        return;
    m_bClosing = true;
    m_loggerFile.Operation(L"退出程序", L"开始退出：" + strReason);
    m_dwExitTick = GetTickCount();
    m_managerTasks.RequestStopAll(L"退出程序，已请求停止");
    m_tempCleanup.RequestStop();
}

void CFtpUploaderToolDlg::OnExit()
{
    if (AfxMessageBox(L"确认停止上传并退出？", MB_YESNO | MB_ICONQUESTION) == IDYES)
        BeginExit(L"用户确认退出");
    else
        m_loggerFile.Operation(L"退出程序", L"用户取消退出");
}

BOOL CFtpUploaderToolDlg::OnQueryEndSession()
{
    m_loggerFile.Operation(L"系统会话结束", L"收到系统退出查询");
    return TRUE;
}

void CFtpUploaderToolDlg::OnEndSession(BOOL bEnding)
{
    if (bEnding)
        BeginExit(L"系统会话结束");
    else
        m_loggerFile.Operation(L"系统会话结束", L"系统取消会话结束");
    CDialog::OnEndSession(bEnding);
}

void CFtpUploaderToolDlg::OnDestroy()
{
    m_tempCleanup.RequestStop();
    KillTimer(1);
    KillTimer(2);
    KillTimer(3);
    NOTIFYICONDATAW stData = {0};
    stData.cbSize = sizeof(stData);
    stData.hWnd = m_hWnd;
    stData.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &stData);
    CDialog::OnDestroy();
}

// 应用当前用户登录自启动和自动睡眠策略，并记录操作结果。
void CFtpUploaderToolDlg::ApplyGlobal()
{
    EXECUTION_STATE dwState = SetThreadExecutionState(m_stGlobal.m_bPreventSleep ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
    m_loggerFile.Operation(L"设置休眠策略", dwState ? (m_stGlobal.m_bPreventSleep ? L"已禁止运行期间休眠" : L"已恢复正常休眠")
                                                 : L"失败：无法应用休眠设置", NULL, dwState ? 1 : 3);
    HKEY stKey;
    LONG lResult = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
                                   KEY_SET_VALUE, NULL, &stKey, NULL);
    if (lResult == ERROR_SUCCESS)
    {
        if (m_stGlobal.m_bAutoStart)
        {
            CString strCommand = L"\"" + JoinPath(m_strRoot, L"FtpUploaderTool.exe") + L"\"";
            lResult = RegSetValueExW(stKey, L"FtpUploaderTool", 0, REG_SZ, (const BYTE*)(LPCWSTR)strCommand,
                                     (strCommand.GetLength() + 1) * sizeof(WCHAR));
        }
        else
        {
            lResult = RegDeleteValueW(stKey, L"FtpUploaderTool");
            if (lResult == ERROR_FILE_NOT_FOUND)
                lResult = ERROR_SUCCESS;
        }
        RegCloseKey(stKey);
    }
    m_loggerFile.Operation(L"设置登录自启动", lResult == ERROR_SUCCESS ? (m_stGlobal.m_bAutoStart ? CString(L"已开启") : CString(L"已关闭"))
                                                                 : L"失败：" + WinError(lResult), NULL, lResult == ERROR_SUCCESS ? 1 : 3);
}
