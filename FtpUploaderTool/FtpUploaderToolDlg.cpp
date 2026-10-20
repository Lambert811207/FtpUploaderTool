#include "stdafx.h"
#include "FtpUploaderToolDlg.h"
static UINT m_unTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
static UINT m_unShowInstance = RegisterWindowMessageW(L"FtpUploaderTool_Show");
static const UINT m_unTray = WM_APP + 1;
BEGIN_MESSAGE_MAP(CFtpUploaderToolDlg, CDialog)
    ON_COMMAND_RANGE(IDC_BTN_ADD, IDC_BTN_OPEN_LOG, OnAction)
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_SYSCOMMAND()
    ON_WM_QUERYENDSESSION()
    ON_WM_ENDSESSION()
    ON_WM_DESTROY()
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_TASKS, OnDoubleClick)
    ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST_LOGS, OnLogInfo)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_LOGS, OnLogDraw)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_TASKS, OnTaskDraw)
    ON_CBN_SELCHANGE(IDC_CMB_LOG_TASK, OnFilter)
    ON_CBN_SELCHANGE(IDC_CMB_LOG_LEVEL, OnFilter)
    ON_MESSAGE(WM_APP + 1, OnTray)
    ON_REGISTERED_MESSAGE(m_unTaskbarCreated, OnTaskbarCreated)
    ON_REGISTERED_MESSAGE(m_unShowInstance, OnShowInstance)
    ON_COMMAND(IDM_MENU_SHOW, OnTrayShow)
    ON_COMMAND(IDM_MENU_EXIT, OnExit)
END_MESSAGE_MAP()
CFtpUploaderToolDlg::CFtpUploaderToolDlg(CWnd* pParent) : CDialog(IDD_DLG_MAIN, pParent), m_strRoot(ExeDir()),
    m_strConfigPath(JoinPath(JoinPath(m_strRoot, L"Config"), L"FtpUploaderTool.ini")), m_bClosing(false), m_nEditAfterStop(-1),
    m_dwExitTick(0), m_dwStartTick(GetTickCount()), m_hIcon(NULL) {}
CFtpUploaderToolDlg::~CFtpUploaderToolDlg() { for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) delete m_vecWorkers[nIndex]; }
void CFtpUploaderToolDlg::DoDataExchange(CDataExchange* pExchange) {
    CDialog::DoDataExchange(pExchange); DDX_Control(pExchange, IDC_LIST_TASKS, m_listTasks); DDX_Control(pExchange, IDC_LIST_LOGS, m_listLogs);
    DDX_Control(pExchange, IDC_CMB_LOG_TASK, m_cmbLogTask); DDX_Control(pExchange, IDC_CMB_LOG_LEVEL, m_cmbLogLevel);
}
BOOL CFtpUploaderToolDlg::OnInitDialog() {
    CDialog::OnInitDialog(); m_hIcon = LoadIcon(NULL, IDI_APPLICATION); SetIcon(m_hIcon, TRUE); SetIcon(m_hIcon, FALSE);
    m_listTasks.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LPCWSTR arrColumns[] = {L"名称", L"服务器", L"状态", L"待传", L"搁置", L"今日成功", L"今日失败", L"最后上传", L"最后错误"};
    int arrWidths[] = {140, 160, 220, 60, 60, 80, 80, 90, 300};
    for (int nIndex = 0; nIndex < _countof(arrColumns); ++nIndex) m_listTasks.InsertColumn(nIndex, arrColumns[nIndex], LVCFMT_LEFT, arrWidths[nIndex]);
    m_listLogs.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    m_listLogs.InsertColumn(0, L"时间", LVCFMT_LEFT, 105); m_listLogs.InsertColumn(1, L"任务", LVCFMT_LEFT, 140); m_listLogs.InsertColumn(2, L"级别", LVCFMT_LEFT, 70); m_listLogs.InsertColumn(3, L"内容", LVCFMT_LEFT, 800);
    m_cmbLogLevel.AddString(L"DEBUG 及以上"); m_cmbLogLevel.AddString(L"INFO 及以上"); m_cmbLogLevel.AddString(L"WARN 及以上"); m_cmbLogLevel.AddString(L"ERROR"); m_cmbLogLevel.SetCurSel(1); CheckDlgButton(IDC_CHK_AUTOSCROLL, BST_CHECKED);
    CString strMessage;
    if (!m_storeConfig.Load(m_strConfigPath, m_stGlobal, m_vecConfigs, strMessage)) AfxMessageBox(strMessage, MB_ICONERROR);
    m_loggerFile.Start(JoinPath(m_strRoot, L"Log"), m_stGlobal.m_unLogKeepDays); if (!strMessage.IsEmpty()) m_loggerFile.Write(NULL, 2, strMessage);
    for (size_t nIndex = 0; nIndex < m_vecConfigs.size(); ++nIndex) m_vecWorkers.push_back(new CTaskWorker(m_vecConfigs[nIndex], m_strRoot, m_loggerFile, m_stGlobal.m_unBackupKeepDays));
    RebuildTasks(); ApplyGlobal(); Tray(true); SetTimer(1, 200, NULL); SetTimer(2, 500, NULL); SetTimer(3, 100, NULL);
    for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) m_vecWorkers[nIndex]->Start();
    m_loggerFile.Write(NULL, 1, L"FtpUploaderTool 已启动（VS2010 / MFC / Win32）");
    CRect rectWindow; GetClientRect(&rectWindow); Layout(rectWindow.Width(), rectWindow.Height()); return TRUE;
}
int CFtpUploaderToolDlg::Selected() const { POSITION posPosition = m_listTasks.GetFirstSelectedItemPosition(); return posPosition ? m_listTasks.GetNextSelectedItem(posPosition) : -1; }
bool CFtpUploaderToolDlg::Save() {
    CString strMessage; if (!m_storeConfig.Save(m_strConfigPath, m_stGlobal, m_vecConfigs, strMessage)) { AfxMessageBox(strMessage, MB_ICONERROR); return false; } return true;
}
bool CFtpUploaderToolDlg::CheckConflict(const TaskConfig& stConfig, int nExcept) {
    for (size_t nIndex = 0; nIndex < m_vecConfigs.size(); ++nIndex) {
        if ((int)nIndex == nExcept) continue;
        if (stConfig.m_strName.CompareNoCase(m_vecConfigs[nIndex].m_strName) == 0) { AfxMessageBox(L"任务名称不能重复"); return false; }
        if (IsWithin(stConfig.m_strLocalDir, m_vecConfigs[nIndex].m_strLocalDir) || IsWithin(m_vecConfigs[nIndex].m_strLocalDir, stConfig.m_strLocalDir)) {
            AfxMessageBox(L"监控目录不能相同或互相包含，以免重复上传/删除"); return false;
        }
    }
    return true;
}
void CFtpUploaderToolDlg::RebuildTasks() {
    m_listTasks.DeleteAllItems(); m_cmbLogTask.ResetContent(); m_cmbLogTask.AddString(L"全部"); m_cmbLogTask.AddString(L"系统");
    for (size_t nIndex = 0; nIndex < m_vecConfigs.size(); ++nIndex) {
        m_listTasks.InsertItem((int)nIndex, m_vecConfigs[nIndex].m_strName); m_cmbLogTask.AddString(m_vecConfigs[nIndex].m_strName);
        CString strAddress; strAddress.Format(L"%s:%u", (LPCWSTR)m_vecConfigs[nIndex].m_strHost, m_vecConfigs[nIndex].m_unPort); m_listTasks.SetItemText((int)nIndex, 1, strAddress);
    }
    m_cmbLogTask.SetCurSel(0); RefreshStatus(); RebuildLogView();
}
void CFtpUploaderToolDlg::Edit(int nIndex) {
    if (nIndex < 0 || nIndex >= (int)m_vecWorkers.size()) return;
    if (m_vecWorkers[nIndex]->Running()) {
        if (AfxMessageBox(L"编辑将先停止该任务；当前文件会在重启后重新上传。继续？", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        m_nEditAfterStop = nIndex; m_vecWorkers[nIndex]->RequestStop(); return;
    }
    CTaskEditDlg dlgDialog(m_vecConfigs[nIndex], this); if (dlgDialog.DoModal() != IDOK) return;
    TaskConfig stConfig = dlgDialog.Config(); if (!CheckConflict(stConfig, nIndex)) return;
    TaskConfig stOld = m_vecConfigs[nIndex]; m_vecConfigs[nIndex] = stConfig;
    if (!Save()) { m_vecConfigs[nIndex] = stOld; return; }
    delete m_vecWorkers[nIndex]; m_vecWorkers[nIndex] = new CTaskWorker(stConfig, m_strRoot, m_loggerFile, m_stGlobal.m_unBackupKeepDays);
    RebuildTasks(); m_vecWorkers[nIndex]->Start();
}
void CFtpUploaderToolDlg::OnAction(UINT unId) {
    if (m_bClosing) return; int nSelected = Selected();
    if (unId == IDC_BTN_ADD) {
        if (m_vecConfigs.size() >= 32) { AfxMessageBox(L"最多支持 32 个任务"); return; }
        TaskConfig stConfig;
        CTaskEditDlg dlgDialog(stConfig, this); if (dlgDialog.DoModal() != IDOK) return; stConfig = dlgDialog.Config(); if (!CheckConflict(stConfig, -1)) return;
        m_vecConfigs.push_back(stConfig); if (!Save()) { m_vecConfigs.pop_back(); return; }
        m_vecWorkers.push_back(new CTaskWorker(stConfig, m_strRoot, m_loggerFile, m_stGlobal.m_unBackupKeepDays)); RebuildTasks(); m_vecWorkers.back()->Start();
    } else if (unId == IDC_BTN_EDIT) Edit(nSelected);
    else if (unId == IDC_BTN_START_ALL || unId == IDC_BTN_STOP_ALL) {
        for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) { if (unId == IDC_BTN_STOP_ALL) m_vecWorkers[nIndex]->RequestStop(); else if (!m_vecWorkers[nIndex]->Running()) m_vecWorkers[nIndex]->Start(); }
    } else if (unId == IDC_BTN_START || unId == IDC_BTN_STOP) {
        POSITION posPosition = m_listTasks.GetFirstSelectedItemPosition();
        while (posPosition) { int nIndex = m_listTasks.GetNextSelectedItem(posPosition); if (unId == IDC_BTN_STOP) m_vecWorkers[nIndex]->RequestStop(); else if (!m_vecWorkers[nIndex]->Running()) m_vecWorkers[nIndex]->Start(); }
    } else if (unId == IDC_BTN_CLEAR_LOG) { m_deqUiLogs.clear(); RebuildLogView(); }
    else if (unId == IDC_BTN_OPEN_LOG) { CString strPath = JoinPath(JoinPath(m_strRoot, L"Log"), DateText()); MakeDirs(strPath); ShellExecuteW(m_hWnd, L"open", strPath, NULL, NULL, SW_SHOWNORMAL); }
    else if (unId == IDC_BTN_SETTINGS) {
        CSettingsDlg dlgDialog(m_stGlobal, this); if (dlgDialog.DoModal() == IDOK) { GlobalConfig stOld = m_stGlobal; m_stGlobal = dlgDialog.m_stConfig; if (!Save()) m_stGlobal = stOld; else { ApplyGlobal(); AfxMessageBox(L"设置已保存。日志及备份保留天数将在程序重启后生效。"); } }
    }
}
void CFtpUploaderToolDlg::RefreshStatus() {
    UINT unOnline = 0, unPending = 0; ULONGLONG ullOk = 0;
    for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) {
        TaskStatus stStatus = m_vecWorkers[nIndex]->Status(); CString arrValues[7]; arrValues[0] = stStatus.m_strText;
        if (stStatus.m_nState == CS_UPLOADING && stStatus.m_ullTotal) { CString strProgress; strProgress.Format(L" %u%% %s", (UINT)(stStatus.m_ullSent * 100.0 / stStatus.m_ullTotal), (LPCWSTR)stStatus.m_strCurrentFile); arrValues[0] += strProgress; }
        arrValues[1].Format(L"%u", stStatus.m_unPending); arrValues[2].Format(L"%u", stStatus.m_unParked); arrValues[3].Format(L"%u", stStatus.m_unTodayOk); arrValues[4].Format(L"%u", stStatus.m_unTodayFail); arrValues[5] = stStatus.m_strLastOk; arrValues[6] = stStatus.m_strLastError;
        for (int nColumn = 0; nColumn < 7; ++nColumn) if (m_listTasks.GetItemText((int)nIndex, nColumn + 2) != arrValues[nColumn]) m_listTasks.SetItemText((int)nIndex, nColumn + 2, arrValues[nColumn]);
        if (stStatus.m_nState == CS_IDLE || stStatus.m_nState == CS_UPLOADING) ++unOnline; unPending += stStatus.m_unPending; ullOk += stStatus.m_unTodayOk;
    }
    CString strText; strText.Format(L"运行 %lu 分钟  |  在线 %u/%u  |  待传 %u  |  今日成功 %I64u  |  %s", (GetTickCount() - m_dwStartTick) / 60000, unOnline, (UINT)m_vecConfigs.size(), unPending, ullOk, m_loggerFile.DiskError() ? L"日志写入失败" : L"日志正常"); SetDlgItemText(IDC_STATIC_STATUS, strText); Tray(false);
}
void CFtpUploaderToolDlg::RefreshLogs() {
    std::vector<LogItem> vecItems; m_loggerFile.PopUi(vecItems); if (vecItems.empty()) return;
    bool bAtBottom = m_vecView.empty() || m_listLogs.GetTopIndex() + m_listLogs.GetCountPerPage() >= (int)m_vecView.size() - 1;
    for (size_t nIndex = 0; nIndex < vecItems.size(); ++nIndex) m_deqUiLogs.push_back(vecItems[nIndex]); while (m_deqUiLogs.size() > m_stGlobal.m_unUiMaxLines) m_deqUiLogs.pop_front();
    RebuildLogView(); if (bAtBottom && IsDlgButtonChecked(IDC_CHK_AUTOSCROLL) && !m_vecView.empty()) m_listLogs.EnsureVisible((int)m_vecView.size() - 1, FALSE);
}
void CFtpUploaderToolDlg::RebuildLogView() {
    m_vecView.clear(); int nTaskIndex = m_cmbLogTask.GetCurSel(), nLevel = max(0, m_cmbLogLevel.GetCurSel());
    for (size_t nIndex = 0; nIndex < m_deqUiLogs.size(); ++nIndex) {
        const LogItem& stItem = m_deqUiLogs[nIndex]; bool bMatch = nTaskIndex <= 0 || (nTaskIndex == 1 ? stItem.m_strId.IsEmpty() : nTaskIndex - 2 < (int)m_vecConfigs.size() && stItem.m_strId == m_vecConfigs[nTaskIndex - 2].m_strId);
        if (bMatch && stItem.m_nLevel >= nLevel) m_vecView.push_back((int)nIndex);
    }
    m_listLogs.SetItemCountEx((int)m_vecView.size(), LVSICF_NOSCROLL); m_listLogs.Invalidate(FALSE);
}
void CFtpUploaderToolDlg::OnTimer(UINT_PTR unId) {
    if (unId == 1) RefreshLogs();
    if (unId == 2) {
        bool bAllStopped = true;
        for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) if (!m_vecWorkers[nIndex]->PollStopped()) bAllStopped = false;
        RefreshStatus();
        if (m_nEditAfterStop >= 0 && m_nEditAfterStop < (int)m_vecWorkers.size() && !m_vecWorkers[m_nEditAfterStop]->Running()) { int nIndex = m_nEditAfterStop; m_nEditAfterStop = -1; Edit(nIndex); }
        if (m_bClosing && bAllStopped) { Tray(false); NOTIFYICONDATAW stData = {0}; stData.cbSize = sizeof(stData); stData.hWnd = m_hWnd; stData.uID = 1; Shell_NotifyIconW(NIM_DELETE, &stData); m_loggerFile.Write(NULL, 1, L"程序正常退出"); m_loggerFile.Stop(); EndDialog(IDOK); return; }
        if (m_bClosing && TickElapsed(m_dwExitTick, 8000)) { m_loggerFile.Write(NULL, 3, L"退出等待超时，由系统回收进程资源"); ExitProcess(2); }
    }
    if (unId == 3) { KillTimer(3); if (m_stGlobal.m_bStartMinimized) ShowWindow(SW_HIDE); }
    CDialog::OnTimer(unId);
}
void CFtpUploaderToolDlg::OnDoubleClick(NMHDR*, LRESULT* pResult) { *pResult = 0; if (!m_bClosing) Edit(Selected()); }
void CFtpUploaderToolDlg::OnLogInfo(NMHDR* pHeader, LRESULT* pResult) {
    LVITEMW& stItem = ((NMLVDISPINFOW*)pHeader)->item; *pResult = 0;
    if (!(stItem.mask & LVIF_TEXT) || stItem.iItem < 0 || stItem.iItem >= (int)m_vecView.size()) return;
    const LogItem& stLog = m_deqUiLogs[m_vecView[stItem.iItem]]; LPCWSTR arrLevels[] = {L"DEBUG", L"INFO", L"WARN", L"ERROR"}; CString strText;
    switch (stItem.iSubItem) { case 0: strText = stLog.m_strTime.Mid(11); break; case 1: strText = stLog.m_strName; break; case 2: strText = arrLevels[stLog.m_nLevel]; break; case 3: strText = stLog.m_strText; break; }
    lstrcpynW(stItem.pszText, strText, stItem.cchTextMax);
}
void CFtpUploaderToolDlg::OnLogDraw(NMHDR* pHeader, LRESULT* pResult) {
    NMLVCUSTOMDRAW* pDraw = (NMLVCUSTOMDRAW*)pHeader; *pResult = CDRF_DODEFAULT;
    if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT) *pResult = CDRF_NOTIFYITEMDRAW;
    else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && pDraw->nmcd.dwItemSpec < m_vecView.size()) { int nLevel = m_deqUiLogs[m_vecView[pDraw->nmcd.dwItemSpec]].m_nLevel; pDraw->clrText = nLevel == 3 ? RGB(190, 30, 30) : nLevel == 2 ? RGB(185, 110, 0) : nLevel == 0 ? RGB(120, 120, 120) : RGB(30, 30, 30); }
}
void CFtpUploaderToolDlg::OnTaskDraw(NMHDR* pHeader, LRESULT* pResult) {
    NMLVCUSTOMDRAW* pDraw = (NMLVCUSTOMDRAW*)pHeader; *pResult = CDRF_DODEFAULT;
    if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT) *pResult = CDRF_NOTIFYITEMDRAW;
    else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) *pResult = CDRF_NOTIFYSUBITEMDRAW;
    else if (pDraw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM) && pDraw->iSubItem == 2 && pDraw->nmcd.dwItemSpec < m_vecWorkers.size()) {
        TaskStatus stStatus = m_vecWorkers[pDraw->nmcd.dwItemSpec]->Status(); pDraw->clrText = stStatus.m_nState == CS_CONFIG_ERROR || stStatus.m_nState == CS_RECONNECT || stStatus.m_unParked ? RGB(190, 30, 30) : stStatus.m_nState == CS_IDLE || stStatus.m_nState == CS_UPLOADING ? RGB(0, 130, 50) : stStatus.m_nState == CS_CONNECTING ? RGB(180, 120, 0) : RGB(110, 110, 110);
    }
}
void CFtpUploaderToolDlg::OnFilter() { RebuildLogView(); }
BOOL CFtpUploaderToolDlg::PreTranslateMessage(MSG* pMessage) {
    if (pMessage->message == WM_KEYDOWN && pMessage->wParam == 'C' && GetKeyState(VK_CONTROL) < 0 && GetFocus() == &m_listLogs) {
        CString strText; POSITION posPos = m_listLogs.GetFirstSelectedItemPosition();
        while (posPos) { int nIndex = m_listLogs.GetNextSelectedItem(posPos); if (nIndex < (int)m_vecView.size()) { const LogItem& stItem = m_deqUiLogs[m_vecView[nIndex]]; strText += stItem.m_strTime + L" [" + stItem.m_strName + L"] " + stItem.m_strText + L"\r\n"; } }
        if (OpenClipboard()) { EmptyClipboard(); SIZE_T nBytes = (strText.GetLength() + 1) * sizeof(WCHAR); HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, nBytes); if (hMemory) { void* pBuffer = GlobalLock(hMemory); memcpy(pBuffer, (LPCWSTR)strText, nBytes); GlobalUnlock(hMemory); if (!SetClipboardData(CF_UNICODETEXT, hMemory)) GlobalFree(hMemory); } CloseClipboard(); } return TRUE;
    }
    return CDialog::PreTranslateMessage(pMessage);
}
void CFtpUploaderToolDlg::OnSize(UINT unType, int nCx, int nCy) {
    CDialog::OnSize(unType, nCx, nCy); if (!m_listTasks.GetSafeHwnd() || unType == SIZE_MINIMIZED) return;
    Layout(nCx, nCy);
}
void CFtpUploaderToolDlg::Layout(int nCx, int nCy) {
    CRect rectUnits(0, 0, 8, 34); MapDialogRect(&rectUnits);
    int nMargin = rectUnits.right, nTaskTop = rectUnits.bottom;
    CRect rectBar(0, 0, 0, 18); MapDialogRect(&rectBar);
    int nMiddle = max(nTaskTop + rectBar.bottom * 3, nCy * 45 / 100);
    m_listTasks.MoveWindow(nMargin, nTaskTop, max(100, nCx - nMargin * 2), max(80, nMiddle - nTaskTop - nMargin));
    const int arrIds[] = {IDC_STATIC_LOG_LABEL, IDC_CMB_LOG_TASK, IDC_CMB_LOG_LEVEL, IDC_CHK_AUTOSCROLL, IDC_BTN_CLEAR_LOG, IDC_BTN_OPEN_LOG};
    for (int nIndex = 0; nIndex < _countof(arrIds); ++nIndex) {
        CWnd* pControl = GetDlgItem(arrIds[nIndex]); CRect rectWindow; pControl->GetWindowRect(&rectWindow); ScreenToClient(&rectWindow);
        int nOffset = arrIds[nIndex] == IDC_STATIC_LOG_LABEL || arrIds[nIndex] == IDC_CHK_AUTOSCROLL ? nMargin / 2 : 0;
        pControl->SetWindowPos(NULL, rectWindow.left, nMiddle + nOffset, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    int nLogTop = nMiddle + rectBar.bottom + nMargin, nStatusHeight = rectBar.bottom;
    m_listLogs.MoveWindow(nMargin, nLogTop, max(100, nCx - nMargin * 2), max(80, nCy - nLogTop - nStatusHeight - nMargin * 2));
    GetDlgItem(IDC_STATIC_STATUS)->MoveWindow(nMargin, nCy - nStatusHeight - nMargin / 2, max(100, nCx - nMargin * 2), nStatusHeight);
}
void CFtpUploaderToolDlg::OnGetMinMaxInfo(MINMAXINFO* pInfo) { pInfo->ptMinTrackSize.x = 1024; pInfo->ptMinTrackSize.y = 700; CDialog::OnGetMinMaxInfo(pInfo); }
void CFtpUploaderToolDlg::Tray(bool bAdd) {
    UINT unOnline = 0, unTotal = 0; bool bError = false, bConnecting = false;
    for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) { TaskStatus stStatus = m_vecWorkers[nIndex]->Status(); ++unTotal; if (stStatus.m_nState == CS_IDLE || stStatus.m_nState == CS_UPLOADING) ++unOnline; if (stStatus.m_nState == CS_CONFIG_ERROR || stStatus.m_unParked) bError = true; if (stStatus.m_nState == CS_CONNECTING || stStatus.m_nState == CS_RECONNECT) bConnecting = true; }
    NOTIFYICONDATAW stData = {0}; stData.cbSize = sizeof(stData); stData.hWnd = m_hWnd; stData.uID = 1; stData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; stData.uCallbackMessage = m_unTray;
    stData.hIcon = LoadIcon(NULL, bError ? IDI_ERROR : bConnecting ? IDI_WARNING : unOnline ? IDI_INFORMATION : IDI_APPLICATION);
    CString strTip; strTip.Format(L"FTP 上传 %u/%u 在线", unOnline, unTotal); lstrcpynW(stData.szTip, strTip, _countof(stData.szTip)); Shell_NotifyIconW(bAdd ? NIM_ADD : NIM_MODIFY, &stData);
}
LRESULT CFtpUploaderToolDlg::OnTray(WPARAM, LPARAM lParam) {
    if (lParam == WM_LBUTTONDBLCLK) OnTrayShow();
    else if (lParam == WM_RBUTTONUP) { CMenu menuMenu; menuMenu.CreatePopupMenu(); menuMenu.AppendMenu(MF_STRING, IDM_MENU_SHOW, L"显示主窗口"); menuMenu.AppendMenu(MF_STRING, IDM_MENU_EXIT, L"退出"); POINT stPoint; GetCursorPos(&stPoint); SetForegroundWindow(); menuMenu.TrackPopupMenu(TPM_RIGHTBUTTON, stPoint.x, stPoint.y, this); PostMessage(WM_NULL); } return 0;
}
LRESULT CFtpUploaderToolDlg::OnTaskbarCreated(WPARAM, LPARAM) { Tray(true); return 0; }
LRESULT CFtpUploaderToolDlg::OnShowInstance(WPARAM, LPARAM) { OnTrayShow(); return 0; }
void CFtpUploaderToolDlg::OnTrayShow() { ShowWindow(SW_RESTORE); SetForegroundWindow(); }
void CFtpUploaderToolDlg::OnCancel() { ShowWindow(SW_HIDE); }
void CFtpUploaderToolDlg::OnSysCommand(UINT unId, LPARAM lParam) { if ((unId & 0xFFF0) == SC_MINIMIZE) ShowWindow(SW_HIDE); else CDialog::OnSysCommand(unId, lParam); }
void CFtpUploaderToolDlg::BeginExit() { if (m_bClosing) return; m_bClosing = true; m_nEditAfterStop = -1; m_dwExitTick = GetTickCount(); for (size_t nIndex = 0; nIndex < m_vecWorkers.size(); ++nIndex) m_vecWorkers[nIndex]->RequestStop(); SetDlgItemText(IDC_STATIC_STATUS, L"正在停止全部任务…"); }
void CFtpUploaderToolDlg::OnExit() { if (AfxMessageBox(L"确认停止上传并退出？", MB_YESNO | MB_ICONQUESTION) == IDYES) BeginExit(); }
BOOL CFtpUploaderToolDlg::OnQueryEndSession() { return TRUE; }
void CFtpUploaderToolDlg::OnEndSession(BOOL bEnding) { if (bEnding) BeginExit(); CDialog::OnEndSession(bEnding); }
void CFtpUploaderToolDlg::OnDestroy() {
    KillTimer(1); KillTimer(2); KillTimer(3);
    NOTIFYICONDATAW stData = {0}; stData.cbSize = sizeof(stData); stData.hWnd = m_hWnd; stData.uID = 1; Shell_NotifyIconW(NIM_DELETE, &stData);
    CDialog::OnDestroy();
}
void CFtpUploaderToolDlg::ApplyGlobal() {
    SetThreadExecutionState(m_stGlobal.m_bPreventSleep ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
    HKEY stKey; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, KEY_SET_VALUE, NULL, &stKey, NULL) == ERROR_SUCCESS) {
        if (m_stGlobal.m_bAutoStart) { CString strCommand = L"\"" + JoinPath(m_strRoot, L"FtpUploaderTool.exe") + L"\""; RegSetValueExW(stKey, L"FtpUploaderTool", 0, REG_SZ, (const BYTE*)(LPCWSTR)strCommand, (strCommand.GetLength() + 1) * sizeof(WCHAR)); }
        else RegDeleteValueW(stKey, L"FtpUploaderTool"); RegCloseKey(stKey);
    }
}
