#include "stdafx.h"
#include "TaskEditDlg.h"
struct TaskConnectionTest {
    volatile LONG m_lReferences, m_lDone; CCritSec m_csLock; CString m_strResult; TaskConfig m_stConfig; HANDLE m_hStop;
    explicit TaskConnectionTest(const TaskConfig& stConfig) : m_lReferences(1), m_lDone(0), m_stConfig(stConfig), m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)) {}
    ~TaskConnectionTest() { CloseHandle(m_hStop); }
    void Release() { if (InterlockedDecrement(&m_lReferences) == 0) delete this; }
    void Result(const CString& strText) { CAutoLock lockGuard(m_csLock); m_strResult = strText; }
};
static unsigned __stdcall RunTest(void* pContext) {
    TaskConnectionTest* pTest = (TaskConnectionTest*)pContext;
    try {
        CFtpClient ftpClientSession(pTest->m_stConfig, pTest->m_hStop); pTest->Result(L"正在连接并登录…"); int nError = ftpClientSession.Connect();
        PendingFile stFile; FILETIME stFileTime; GetSystemTimeAsFileTime(&stFileTime); stFile.m_ullModified = ((ULONGLONG)stFileTime.dwHighDateTime << 32) | stFileTime.dwLowDateTime;
        if (!nError) { pTest->Result(L"正在进入/创建远程目录…"); nError = ftpClientSession.EnsureDirectory(RemoteDirectory(pTest->m_stConfig, stFile)); }
        CString strPath; WCHAR szTemp[MAX_PATH] = {0}, szName[MAX_PATH] = {0}; GetTempPathW(_countof(szTemp), szTemp); GetTempFileNameW(szTemp, L"ftp", 0, szName); strPath = szName;
        CString strRemote = L"_ftptest_" + NewId() + L".tmp";
        {
            CAutoHandle hLocal(CreateFileW(strPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL));
            static const char szBytes[] = "FtpUploader-Test";
            if (!hLocal.Valid() || !WriteBytes(hLocal.Get(), szBytes, 16)) nError = FE_LOCAL_READ;
            if (!nError) { SetFilePointer(hLocal.Get(), 0, NULL, FILE_BEGIN); pTest->Result(L"正在测试上传/大小校验…"); nError = ftpClientSession.Upload(hLocal.Get(), strRemote, NULL); }
            if (!nError) { LONGLONG llSize; bool bSupported; nError = ftpClientSession.Size(strRemote, llSize, bSupported); if (!nError && bSupported && llSize != 16) nError = FE_VERIFY; }
            if (!nError) nError = ftpClientSession.Delete(strRemote);
            CString strResult = !nError ? CString(L"连接、登录、上传、校验和删除成功") : ftpClientSession.ErrorText(nError); pTest->Result(strResult);
            if (ftpClientSession.Connected() && !Stopped(pTest->m_hStop)) ftpClientSession.Delete(strRemote);
        }
        DeleteFileW(strPath);
    } catch (CException* pException) { pException->Delete(); pTest->Result(L"连接测试发生异常"); }
    catch (const std::exception&) { pTest->Result(L"连接测试发生 C++ 异常"); }
    InterlockedExchange(&pTest->m_lDone, 1); pTest->Release(); return 0;
}
BEGIN_MESSAGE_MAP(CTaskEditDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_BROWSE_LOCAL, OnBrowseLocal)
    ON_BN_CLICKED(IDC_BTN_BROWSE_BACKUP, OnBrowseBackup)
    ON_BN_CLICKED(IDC_BTN_TEST, OnTest)
    ON_WM_TIMER()
END_MESSAGE_MAP()
CTaskEditDlg::CTaskEditDlg(const TaskConfig& stConfig, CWnd* pParent) : CDialog(IDD_DLG_TASK, pParent), m_stConfig(stConfig), m_pTest(NULL) {}
CTaskEditDlg::~CTaskEditDlg() { CancelTest(); }
static void AddChoices(CWnd* pDialog, int nId, LPCWSTR pszChoices) {
    CComboBox* pCombo = (CComboBox*)pDialog->GetDlgItem(nId); CString strText(pszChoices), strPart; int nStart = 0;
    while (!(strPart = strText.Tokenize(L"|", nStart)).IsEmpty()) pCombo->AddString(strPart);
}
BOOL CTaskEditDlg::OnInitDialog() {
    CDialog::OnInitDialog();
    AddChoices(this, IDC_CMB_ENCODING, L"自动|UTF-8|GBK"); AddChoices(this, IDC_CMB_POST_ACTION, L"删除|移动到备份|保留"); AddChoices(this, IDC_CMB_REMOTE_EXIST, L"覆盖|改名加时间戳|跳过并搁置");
#define BIND_TEXT(id, field) SetDlgItemText(id, m_stConfig.field)
#define NUMBER(id, field) SetDlgItemInt(id, m_stConfig.field)
#define CHECK(id, field) CheckDlgButton(id, m_stConfig.field ? BST_CHECKED : BST_UNCHECKED)
    BIND_TEXT(IDC_EDIT_NAME, m_strName); BIND_TEXT(IDC_EDIT_HOST, m_strHost); BIND_TEXT(IDC_EDIT_USER, m_strUser); BIND_TEXT(IDC_EDIT_PASSWORD, m_strPassword); BIND_TEXT(IDC_EDIT_LOCAL_DIR, m_strLocalDir); BIND_TEXT(IDC_EDIT_REMOTE_DIR, m_strRemoteDir);
    BIND_TEXT(IDC_EDIT_BACKUP_DIR, m_strBackupDir); BIND_TEXT(IDC_EDIT_INCLUDE_MASK, m_strIncludeMask); BIND_TEXT(IDC_EDIT_EXCLUDE_MASK, m_strExcludeMask); BIND_TEXT(IDC_EDIT_TEMP_SUFFIX, m_strTempSuffix);
    NUMBER(IDC_EDIT_PORT, m_unPort); NUMBER(IDC_EDIT_SCAN_INTERVAL, m_unScanInterval); NUMBER(IDC_EDIT_STABLE_SEC, m_unStableSec); NUMBER(IDC_EDIT_MAX_RETRY, m_unMaxFileRetry); NUMBER(IDC_EDIT_MAX_AGE, m_unMaxFileAgeDays); NUMBER(IDC_EDIT_KEEP_DAYS, m_unKeepDays);
    NUMBER(IDC_EDIT_CONNECT_TIMEOUT, m_unConnTimeout); NUMBER(IDC_EDIT_IO_TIMEOUT, m_unIoTimeout); NUMBER(IDC_EDIT_HEARTBEAT, m_unHeartbeat); NUMBER(IDC_EDIT_RECONNECT_MIN, m_unReconnMin); NUMBER(IDC_EDIT_RECONNECT_MAX, m_unReconnMax); NUMBER(IDC_EDIT_IDLE_DISCONNECT, m_unIdleDisconn);
    CHECK(IDC_CHK_PASSIVE, m_bPassive); CHECK(IDC_CHK_SUBDIR, m_bIncludeSubDir); CHECK(IDC_CHK_TEMP_NAME, m_bUseTempName); CHECK(IDC_CHK_VERIFY_SIZE, m_bVerifySize);
#undef BIND_TEXT
#undef NUMBER
#undef CHECK
    ((CComboBox*)GetDlgItem(IDC_CMB_ENCODING))->SetCurSel(m_stConfig.m_nEncoding); ((CComboBox*)GetDlgItem(IDC_CMB_POST_ACTION))->SetCurSel(m_stConfig.m_nPostAction); ((CComboBox*)GetDlgItem(IDC_CMB_REMOTE_EXIST))->SetCurSel(m_stConfig.m_nRemoteExist);
    SetTimer(1, 200, NULL);
    MONITORINFO stMonitor = {sizeof(stMonitor)}; CRect rectWindow; GetWindowRect(&rectWindow);
    if (GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &stMonitor)) {
        int nX = max(stMonitor.rcWork.left, min(rectWindow.left, stMonitor.rcWork.right - rectWindow.Width()));
        int nY = max(stMonitor.rcWork.top, min(rectWindow.top, stMonitor.rcWork.bottom - rectWindow.Height()));
        SetWindowPos(NULL, nX, nY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    return TRUE;
}
bool CTaskEditDlg::ReadConfig(TaskConfig& stConfig) {
    stConfig = m_stConfig; stConfig.m_strError.Empty();
#define BIND_TEXT(id, field) GetDlgItemText(id, stConfig.field)
#define NUMBER(id, field) { BOOL bValid; stConfig.field = GetDlgItemInt(id, &bValid, FALSE); if (!bValid) { AfxMessageBox(L"数值项必须为非负整数"); return false; } }
#define CHECK(id, field) stConfig.field = IsDlgButtonChecked(id) == BST_CHECKED
    BIND_TEXT(IDC_EDIT_NAME, m_strName); BIND_TEXT(IDC_EDIT_HOST, m_strHost); BIND_TEXT(IDC_EDIT_USER, m_strUser); BIND_TEXT(IDC_EDIT_PASSWORD, m_strPassword); BIND_TEXT(IDC_EDIT_LOCAL_DIR, m_strLocalDir); BIND_TEXT(IDC_EDIT_REMOTE_DIR, m_strRemoteDir);
    BIND_TEXT(IDC_EDIT_BACKUP_DIR, m_strBackupDir); BIND_TEXT(IDC_EDIT_INCLUDE_MASK, m_strIncludeMask); BIND_TEXT(IDC_EDIT_EXCLUDE_MASK, m_strExcludeMask); BIND_TEXT(IDC_EDIT_TEMP_SUFFIX, m_strTempSuffix);
    NUMBER(IDC_EDIT_PORT, m_unPort); NUMBER(IDC_EDIT_SCAN_INTERVAL, m_unScanInterval); NUMBER(IDC_EDIT_STABLE_SEC, m_unStableSec); NUMBER(IDC_EDIT_MAX_RETRY, m_unMaxFileRetry); NUMBER(IDC_EDIT_MAX_AGE, m_unMaxFileAgeDays); NUMBER(IDC_EDIT_KEEP_DAYS, m_unKeepDays);
    NUMBER(IDC_EDIT_CONNECT_TIMEOUT, m_unConnTimeout); NUMBER(IDC_EDIT_IO_TIMEOUT, m_unIoTimeout); NUMBER(IDC_EDIT_HEARTBEAT, m_unHeartbeat); NUMBER(IDC_EDIT_RECONNECT_MIN, m_unReconnMin); NUMBER(IDC_EDIT_RECONNECT_MAX, m_unReconnMax); NUMBER(IDC_EDIT_IDLE_DISCONNECT, m_unIdleDisconn);
    CHECK(IDC_CHK_PASSIVE, m_bPassive); CHECK(IDC_CHK_SUBDIR, m_bIncludeSubDir); CHECK(IDC_CHK_TEMP_NAME, m_bUseTempName); CHECK(IDC_CHK_VERIFY_SIZE, m_bVerifySize);
#undef BIND_TEXT
#undef NUMBER
#undef CHECK
    stConfig.m_nEncoding = ((CComboBox*)GetDlgItem(IDC_CMB_ENCODING))->GetCurSel(); stConfig.m_nPostAction = ((CComboBox*)GetDlgItem(IDC_CMB_POST_ACTION))->GetCurSel(); stConfig.m_nRemoteExist = ((CComboBox*)GetDlgItem(IDC_CMB_REMOTE_EXIST))->GetCurSel();
    stConfig.m_strName.Trim(); stConfig.m_strHost.Trim(); stConfig.m_strLocalDir.Trim(); stConfig.m_strLocalDir.TrimRight(L"\\/"); stConfig.m_strBackupDir.Trim(); stConfig.m_strRemoteDir.Replace(L'\\', L'/'); stConfig.m_strRemoteDir.Trim();
    if (stConfig.m_strRemoteDir.Left(1) != L"/") stConfig.m_strRemoteDir = L"/" + stConfig.m_strRemoteDir; if (stConfig.m_strRemoteDir.GetLength() > 1) stConfig.m_strRemoteDir.TrimRight(L"/");
    stConfig.m_strEncryptedPassword.Empty(); CString strMessage; if (!stConfig.Validate(strMessage)) { AfxMessageBox(strMessage); return false; } return true;
}
void CTaskEditDlg::OnOK() {
    TaskConfig stConfig; if (!ReadConfig(stConfig)) return;
    if (!PathIsDirectoryW(stConfig.m_strLocalDir) && AfxMessageBox(L"本地目录当前不存在，仍然保存？", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    m_stConfig = stConfig; CancelTest(); CDialog::OnOK();
}
void CTaskEditDlg::OnCancel() { CancelTest(); CDialog::OnCancel(); }
void CTaskEditDlg::CancelTest() { if (m_pTest) { SetEvent(m_pTest->m_hStop); m_pTest->Release(); m_pTest = NULL; } }
void CTaskEditDlg::Browse(int nControl) {
    BROWSEINFOW stInfo = {0}; stInfo.hwndOwner = m_hWnd; stInfo.lpszTitle = L"选择文件夹"; stInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pPidl = SHBrowseForFolderW(&stInfo); if (pPidl) { WCHAR szPath[MAX_PATH] = {0}; if (SHGetPathFromIDListW(pPidl, szPath)) SetDlgItemText(nControl, szPath); CoTaskMemFree(pPidl); }
}
void CTaskEditDlg::OnBrowseLocal() { Browse(IDC_EDIT_LOCAL_DIR); }
void CTaskEditDlg::OnBrowseBackup() { Browse(IDC_EDIT_BACKUP_DIR); }
void CTaskEditDlg::OnTest() {
    if (m_pTest && !InterlockedCompareExchange(&m_pTest->m_lDone, 0, 0)) { SetEvent(m_pTest->m_hStop); return; }
    TaskConfig stConfig; if (!ReadConfig(stConfig)) return;
    if (AfxMessageBox(L"将连接服务器并在目标目录写入、校验和删除测试文件；目录不存在时会尝试创建。继续？", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    CancelTest(); m_pTest = new TaskConnectionTest(stConfig); InterlockedIncrement(&m_pTest->m_lReferences);
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, RunTest, m_pTest, 0, NULL);
    if (!hThread) { m_pTest->Release(); CancelTest(); AfxMessageBox(L"无法创建测试线程"); return; }
    CloseHandle(hThread); SetDlgItemText(IDC_BTN_TEST, L"取消测试");
}
void CTaskEditDlg::OnTimer(UINT_PTR unId) {
    if (m_pTest) { CString strResult; { CAutoLock lockGuard(m_pTest->m_csLock); strResult = m_pTest->m_strResult; } SetDlgItemText(IDC_STATIC_TEST_RESULT, strResult); if (InterlockedCompareExchange(&m_pTest->m_lDone, 0, 0)) SetDlgItemText(IDC_BTN_TEST, L"测试连接"); }
    CDialog::OnTimer(unId);
}
BOOL CSettingsDlg::OnInitDialog() {
    CDialog::OnInitDialog(); CheckDlgButton(IDC_CHK_AUTOSTART, m_stConfig.m_bAutoStart); CheckDlgButton(IDC_CHK_MINIMIZED, m_stConfig.m_bStartMinimized); CheckDlgButton(IDC_CHK_PREVENT_SLEEP, m_stConfig.m_bPreventSleep);
    SetDlgItemInt(IDC_EDIT_LOG_DAYS, m_stConfig.m_unLogKeepDays); SetDlgItemInt(IDC_EDIT_BACKUP_DAYS, m_stConfig.m_unBackupKeepDays); SetDlgItemInt(IDC_EDIT_UI_LINES, m_stConfig.m_unUiMaxLines); return TRUE;
}
void CSettingsDlg::OnOK() {
    BOOL bA, bB, bC; m_stConfig.m_unLogKeepDays = GetDlgItemInt(IDC_EDIT_LOG_DAYS, &bA); m_stConfig.m_unBackupKeepDays = GetDlgItemInt(IDC_EDIT_BACKUP_DAYS, &bB); m_stConfig.m_unUiMaxLines = GetDlgItemInt(IDC_EDIT_UI_LINES, &bC);
    if (!bA || !bB || !bC || m_stConfig.m_unLogKeepDays < 1 || m_stConfig.m_unLogKeepDays > 36500 || m_stConfig.m_unBackupKeepDays > 36500 || m_stConfig.m_unUiMaxLines < 100 || m_stConfig.m_unUiMaxLines > 10000) { AfxMessageBox(L"日志天数 1–36500，备份天数 0–36500，日志行数 100–10000"); return; }
    m_stConfig.m_bAutoStart = IsDlgButtonChecked(IDC_CHK_AUTOSTART) == BST_CHECKED; m_stConfig.m_bStartMinimized = IsDlgButtonChecked(IDC_CHK_MINIMIZED) == BST_CHECKED; m_stConfig.m_bPreventSleep = IsDlgButtonChecked(IDC_CHK_PREVENT_SLEEP) == BST_CHECKED; CDialog::OnOK();
}
