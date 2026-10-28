#include "stdafx.h"
#include "TaskEditDlg.h"
#include "DialogHelpers.h"

BEGIN_MESSAGE_MAP(CTaskEditDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_TASK_OK, OnOK)
    ON_BN_CLICKED(IDC_BTN_TASK_CANCEL, OnCancel)
    ON_BN_CLICKED(IDC_BTN_BROWSE_LOCAL_WATCH_DIR, OnBrowseLocal)
    ON_BN_CLICKED(IDC_BTN_BROWSE_LOCAL_BACKUP_DIR, OnBrowseBackup)
    ON_CBN_SELCHANGE(IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION, OnPolicyChanged)
    ON_BN_CLICKED(IDC_CHK_USE_TEMP_FILENAME, OnPolicyChanged)
    ON_BN_CLICKED(IDC_CHK_DELETE_OLD_EMPTY_DIRS, OnPolicyChanged)
    ON_BN_CLICKED(IDC_BTN_TEST, OnTest)
    ON_WM_TIMER()
END_MESSAGE_MAP()

CTaskEditDlg::CTaskEditDlg(const TaskConfig& stConfig, CWnd* pParent, CLogger* pLogger)
    : CDialog(IDD_DLG_TASK, pParent),
      m_stConfig(stConfig),
      m_pLogger(pLogger),
      m_bTestResultLogged(false)
{
}

CTaskEditDlg::~CTaskEditDlg()
{
    CancelTest();
}

void CTaskEditDlg::LogOperation(const CString& strAction, const CString& strResult, int nLevel)
{
    if (m_pLogger)
        m_pLogger->Operation(strAction, strResult, &m_stConfig, nLevel);
}

static void AddChoices(CWnd* pDialog, int nId, LPCWSTR pszChoices)
{
    CComboBox* pCombo = (CComboBox*)pDialog->GetDlgItem(nId);
    CString strText(pszChoices);
    CString strPart;
    int nStart = 0;
    while (!(strPart = strText.Tokenize(L"|", nStart)).IsEmpty())
        pCombo->AddString(strPart);
}

// 资源文件决定控件布局，此处只填充参数、选项及鼠标说明。
BOOL CTaskEditDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    HICON hIcon = AfxGetApp()->LoadIcon(IDI_ICON_APP);
    SetIcon(hIcon, TRUE);
    SetIcon(hIcon, FALSE);
    AddChoices(this, IDC_CMB_FILENAME_ENCODING, L"自动（UTF-8/GBK）|UTF-8|GBK");
    AddChoices(this, IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION, L"删除本地文件|移到备份目录|保留本地文件");
    AddChoices(this, IDC_CMB_REMOTE_NAME_CONFLICT_POLICY, L"覆盖服务器文件|改名后上传|不上传该文件");
#define BIND_TEXT(id, field) SetDlgItemText(id, m_stConfig.field)
#define NUMBER(id, field) SetDlgItemInt(id, m_stConfig.field)
#define CHECK(id, field) CheckDlgButton(id, m_stConfig.field ? BST_CHECKED : BST_UNCHECKED)
    BIND_TEXT(IDC_EDIT_TASK_NAME, m_strName);
    BIND_TEXT(IDC_EDIT_FTP_SERVER_ADDRESS, m_strHost);
    BIND_TEXT(IDC_EDIT_FTP_LOGIN_USERNAME, m_strUser);
    BIND_TEXT(IDC_EDIT_FTP_LOGIN_PASSWORD, m_strPassword);
    BIND_TEXT(IDC_EDIT_LOCAL_WATCH_DIR, m_strLocalDir);
    BIND_TEXT(IDC_EDIT_SERVER_UPLOAD_DIR, m_strRemoteDir);
    BIND_TEXT(IDC_EDIT_LOCAL_BACKUP_DIR, m_strBackupDir);
    BIND_TEXT(IDC_EDIT_UPLOAD_FILENAME_MASK, m_strIncludeMask);
    BIND_TEXT(IDC_EDIT_SKIP_FILENAME_MASK, m_strExcludeMask);
    BIND_TEXT(IDC_EDIT_TEMP_FILENAME_SUFFIX, m_strTempSuffix);
    NUMBER(IDC_EDIT_FTP_SERVER_PORT, m_unPort);
    NUMBER(IDC_EDIT_SCAN_INTERVAL_SEC, m_unScanInterval);
    NUMBER(IDC_EDIT_PREUPLOAD_UNCHANGED_SEC, m_unStableSec);
    NUMBER(IDC_EDIT_FILE_FAILURE_LIMIT, m_unMaxFileRetry);
    NUMBER(IDC_EDIT_EMPTY_DIR_AGE_DAYS, m_unEmptyDirAgeDays);
    NUMBER(IDC_EDIT_CONNECTION_ESTABLISH_TIMEOUT, m_unConnTimeout);
    NUMBER(IDC_EDIT_DATA_IO_TIMEOUT, m_unIoTimeout);
    NUMBER(IDC_EDIT_CONNECTION_CHECK_INTERVAL, m_unHeartbeat);
    NUMBER(IDC_EDIT_RECONNECT_INITIAL_INTERVAL, m_unReconnMin);
    NUMBER(IDC_EDIT_RECONNECT_MAX_INTERVAL, m_unReconnMax);
    NUMBER(IDC_EDIT_IDLE_DISCONNECT_TIMEOUT, m_unIdleDisconn);
    CHECK(IDC_CHK_CLIENT_INITIATES_DATA_CONNECTION, m_bPassive);
    CHECK(IDC_CHK_SCAN_LOCAL_SUBDIRECTORIES, m_bIncludeSubDir);
    CHECK(IDC_CHK_DELETE_OLD_EMPTY_DIRS, m_bDeleteOldEmptyDirs);
    CHECK(IDC_CHK_USE_TEMP_FILENAME, m_bUseTempName);
    CHECK(IDC_CHK_VERIFY_UPLOADED_FILE_SIZE, m_bVerifySize);
#undef BIND_TEXT
#undef NUMBER
#undef CHECK
    ((CComboBox*)GetDlgItem(IDC_CMB_FILENAME_ENCODING))->SetCurSel(m_stConfig.m_nEncoding);
    ((CComboBox*)GetDlgItem(IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION))->SetCurSel(m_stConfig.m_nPostAction);
    ((CComboBox*)GetDlgItem(IDC_CMB_REMOTE_NAME_CONFLICT_POLICY))->SetCurSel(m_stConfig.m_nRemoteExist);
    OnPolicyChanged();
    GetDlgItem(IDC_EDIT_TASK_NAME)->SetFocus();
    ((CEdit*)GetDlgItem(IDC_EDIT_TASK_NAME))->SetSel(0, 0);
    m_toolTip.Create(this, TTS_ALWAYSTIP);
    m_toolTip.SetMaxTipWidth(480);
    m_toolTip.SetDelayTime(TTDT_INITIAL, 200);
    m_toolTip.SetDelayTime(TTDT_RESHOW, 100);
    AddParameterTip(m_toolTip, this, IDC_EDIT_TASK_NAME, L"任务显示名称，用于区分不同的上传任务。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_FTP_SERVER_ADDRESS, L"FTP 服务器的 IP 地址或域名。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_FTP_SERVER_PORT, L"FTP 控制端口，默认 21。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_FTP_LOGIN_USERNAME, L"FTP 登录账号。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_FTP_LOGIN_PASSWORD, L"FTP 登录密码，直接显示并以明文保存到配置文件。");
    AddParameterTip(m_toolTip, this, IDC_CHK_CLIENT_INITIATES_DATA_CONNECTION,
                    L"勾选：本软件连接服务器的数据端口，即被动模式 PASV。取消：服务器反向连接本机的数据端口，即主动模式 PORT，需要允许本机对应的入站连接。");
    AddParameterTip(m_toolTip, this, IDC_CMB_FILENAME_ENCODING,
                    L"服务器路径和文件名使用的编码，不转换文件内容。自动模式在服务器声明支持 UTF-8 时使用 UTF-8，否则使用 GBK。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_LOCAL_WATCH_DIR, L"本机需要监控并上传文件的目录；扫描范围和文件名规则在“本地扫描及清理”中设置。");
    AddParameterTip(m_toolTip, this, IDC_BTN_BROWSE_LOCAL_WATCH_DIR, L"选择本地监控目录。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_SERVER_UPLOAD_DIR, L"FTP 服务器上的上传目录，需以 / 开头。可用 {yyyy} 年、{MM} 月、{dd} 日、{HH} 时、{sub} 子目录和 {pc} 本机名；日期取源文件最后修改时间。");
    AddParameterTip(m_toolTip, this, IDC_CHK_SCAN_LOCAL_SUBDIRECTORIES, L"扫描本地目录：勾选后同时处理本地监控目录下的子文件夹；未勾选只处理本地监控目录当前这一层的文件，不扫描服务器目录。");
    AddParameterTip(m_toolTip, this, IDC_CHK_DELETE_OLD_EMPTY_DIRS,
                    L"任务运行时，删除本地监控目录内最后修改时间超过指定天数且当前为空的子文件夹，最多检查 10 层；启动后检查一次，此后每小时检查。独立于上传扫描范围，保留根目录，跳过隐藏、系统及链接目录。默认关闭。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_EMPTY_DIR_AGE_DAYS,
                    L"按文件夹最后修改时间计算，超过此天数且当前为空才删除，范围为 1–36500 天，默认 7 天。非空、被占用或删除失败时跳过；不记录连续空置时间，其他软件复用已清理目录时需自行重建。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_UPLOAD_FILENAME_MASK, L"扫描时允许处理的文件名通配符，例如 *.jpg;*.png；多个规则用分号分隔。为空或填写 * 都表示包含全部文件，仍受排除规则等扫描条件限制。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_SKIP_FILENAME_MASK, L"扫描时忽略的文件名通配符，例如 *.tmp;*.part；多个规则用分号分隔。为空表示不按此项排除文件。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_SCAN_INTERVAL_SEC, L"多久重新扫描一次本地目录，单位为秒；每次扫描应用子文件夹和文件名规则。不改变自动删除旧空文件夹的每小时间隔。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_PREUPLOAD_UNCHANGED_SEC, L"上传前，文件大小和最后修改时间必须连续保持不变的秒数。例如填 3，连续 3 秒未变化才允许上传；文件发生变化会重新等待。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_FILE_FAILURE_LIMIT,
                    L"“文件上传及处理”中的单文件失败次数上限，包含首次可计数失败。达到后暂停该文件，停止并重新启动任务可再试；网络中断由“连接维护及重连”处理，不占用此次数。");
    AddParameterTip(m_toolTip, this, IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION,
                    L"上传成功后对本地源文件执行的操作：删除本地文件、移到本地备份目录或保留本地文件。成功记录会保存到磁盘，重启后跳过未变化的已上传文件。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_LOCAL_BACKUP_DIR, L"选择“移到备份目录”后，成功上传的本地源文件移入此目录；这是本地路径。选择删除或保留时不使用此目录。");
    AddParameterTip(m_toolTip, this, IDC_BTN_BROWSE_LOCAL_BACKUP_DIR, L"选择备份目录。");
    AddParameterTip(m_toolTip, this, IDC_CMB_REMOTE_NAME_CONFLICT_POLICY,
                    L"服务器已有同名文件时，按选项覆盖、改名后实际上传，或暂停该文件并保留本地源文件。大小相同不代表上传成功；改名目标也已存在或服务器不支持同名检查时保留源文件并报告原因。停止并重新启动任务后可再试。");
    AddParameterTip(m_toolTip, this, IDC_CHK_USE_TEMP_FILENAME, L"先使用临时文件名上传，完成后再改为正式名称。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_TEMP_FILENAME_SUFFIX, L"启用临时名上传时使用的文件后缀，例如 .uploading；上传成功后改为正式文件名，关闭临时名上传时不使用此项。");
    AddParameterTip(m_toolTip, this, IDC_CHK_VERIFY_UPLOADED_FILE_SIZE, L"上传完成后用 FTP SIZE 命令核对远程文件与本地文件的字节数；这是大小校验，不校验文件内容哈希。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_CONNECTION_ESTABLISH_TIMEOUT,
                    L"建立 FTP 控制连接或数据连接时允许等待的最长时间，单位为秒。主动模式下也用于等待服务器的数据连接。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_DATA_IO_TIMEOUT,
                    L"网络发送、接收数据或等待 FTP 应答的超时时间，单位为秒；不是整个文件上传的总时限。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_CONNECTION_CHECK_INTERVAL,
                    L"没有文件正在上传时，每隔多少秒向服务器发送一次 NOOP 保活命令，检查连接是否可用；检查失败后自动重连。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_IDLE_DISCONNECT_TIMEOUT,
                    L"“连接维护及重连”控制没有待传文件时多久主动断开，单位为秒；0 表示不主动断开，空闲期间按连接状态检查间隔检查连接。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_RECONNECT_INITIAL_INTERVAL,
                    L"网络故障后首次重连前的等待时间，单位为秒。连续失败时逐次加倍，直到最大重连间隔；不会因达到某个次数而停止重连。");
    AddParameterTip(m_toolTip, this, IDC_EDIT_RECONNECT_MAX_INTERVAL,
                    L"连续重连失败时每次等待时间的上限，单位为秒；达到上限后仍按此间隔持续重试。登录失败时直接使用此间隔。");
    AddParameterTip(m_toolTip, this, IDC_BTN_TEST,
                    L"使用当前服务器和远程目录立即测试连接、登录、目录、上传 16 字节测试文件、校验和删除；不上传监控目录中的文件。再次点击可取消，结果显示在本组右侧。");
    AddParameterTip(m_toolTip, this, IDC_BTN_TASK_OK, L"保存任务配置。");
    AddParameterTip(m_toolTip, this, IDC_BTN_TASK_CANCEL, L"取消本次修改。");
    m_toolTip.Activate(TRUE);
    SetTimer(1, 200, NULL);
    MONITORINFO stMonitor = {sizeof(stMonitor)};
    CRect rectWindow;
    GetWindowRect(&rectWindow);
    if (GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &stMonitor))
    {
        int nX = max(stMonitor.rcWork.left, min(rectWindow.left, stMonitor.rcWork.right - rectWindow.Width()));
        int nY = max(stMonitor.rcWork.top, min(rectWindow.top, stMonitor.rcWork.bottom - rectWindow.Height()));
        SetWindowPos(NULL, nX, nY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    return FALSE;
}

BOOL CTaskEditDlg::PreTranslateMessage(MSG* pMessage)
{
    if (m_toolTip.GetSafeHwnd())
        m_toolTip.RelayEvent(pMessage);
    return CDialog::PreTranslateMessage(pMessage);
}

// 将非法数字转换为越界哨兵，统一由配置校验返回对应参数的提示。
bool CTaskEditDlg::ReadConfig(TaskConfig& stConfig)
{
    stConfig = m_stConfig;
    stConfig.m_strError.Empty();
#define BIND_TEXT(id, field) GetDlgItemText(id, stConfig.field)
#define NUMBER(id, field)                                                                                              \
    {                                                                                                                  \
        BOOL bValid;                                                                                                   \
        UINT unValue = GetDlgItemInt(id, &bValid, FALSE);                                                                \
        stConfig.field = bValid ? unValue : (UINT)-1;                                                                   \
    }
#define CHECK(id, field) stConfig.field = IsDlgButtonChecked(id) == BST_CHECKED
    BIND_TEXT(IDC_EDIT_TASK_NAME, m_strName);
    BIND_TEXT(IDC_EDIT_FTP_SERVER_ADDRESS, m_strHost);
    BIND_TEXT(IDC_EDIT_FTP_LOGIN_USERNAME, m_strUser);
    BIND_TEXT(IDC_EDIT_FTP_LOGIN_PASSWORD, m_strPassword);
    BIND_TEXT(IDC_EDIT_LOCAL_WATCH_DIR, m_strLocalDir);
    BIND_TEXT(IDC_EDIT_SERVER_UPLOAD_DIR, m_strRemoteDir);
    BIND_TEXT(IDC_EDIT_LOCAL_BACKUP_DIR, m_strBackupDir);
    BIND_TEXT(IDC_EDIT_UPLOAD_FILENAME_MASK, m_strIncludeMask);
    BIND_TEXT(IDC_EDIT_SKIP_FILENAME_MASK, m_strExcludeMask);
    BIND_TEXT(IDC_EDIT_TEMP_FILENAME_SUFFIX, m_strTempSuffix);
    NUMBER(IDC_EDIT_FTP_SERVER_PORT, m_unPort);
    NUMBER(IDC_EDIT_SCAN_INTERVAL_SEC, m_unScanInterval);
    NUMBER(IDC_EDIT_PREUPLOAD_UNCHANGED_SEC, m_unStableSec);
    NUMBER(IDC_EDIT_FILE_FAILURE_LIMIT, m_unMaxFileRetry);
    NUMBER(IDC_EDIT_CONNECTION_ESTABLISH_TIMEOUT, m_unConnTimeout);
    NUMBER(IDC_EDIT_DATA_IO_TIMEOUT, m_unIoTimeout);
    NUMBER(IDC_EDIT_CONNECTION_CHECK_INTERVAL, m_unHeartbeat);
    NUMBER(IDC_EDIT_RECONNECT_INITIAL_INTERVAL, m_unReconnMin);
    NUMBER(IDC_EDIT_RECONNECT_MAX_INTERVAL, m_unReconnMax);
    NUMBER(IDC_EDIT_IDLE_DISCONNECT_TIMEOUT, m_unIdleDisconn);
    CHECK(IDC_CHK_CLIENT_INITIATES_DATA_CONNECTION, m_bPassive);
    CHECK(IDC_CHK_SCAN_LOCAL_SUBDIRECTORIES, m_bIncludeSubDir);
    CHECK(IDC_CHK_USE_TEMP_FILENAME, m_bUseTempName);
    CHECK(IDC_CHK_VERIFY_UPLOADED_FILE_SIZE, m_bVerifySize);
    CHECK(IDC_CHK_DELETE_OLD_EMPTY_DIRS, m_bDeleteOldEmptyDirs);
    if (stConfig.m_bDeleteOldEmptyDirs)
        NUMBER(IDC_EDIT_EMPTY_DIR_AGE_DAYS, m_unEmptyDirAgeDays);
#undef BIND_TEXT
#undef NUMBER
#undef CHECK
    stConfig.m_nEncoding = ((CComboBox*)GetDlgItem(IDC_CMB_FILENAME_ENCODING))->GetCurSel();
    stConfig.m_nPostAction = ((CComboBox*)GetDlgItem(IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION))->GetCurSel();
    stConfig.m_nRemoteExist = ((CComboBox*)GetDlgItem(IDC_CMB_REMOTE_NAME_CONFLICT_POLICY))->GetCurSel();
    stConfig.m_strName.Trim();
    stConfig.m_strHost.Trim();
    stConfig.m_strLocalDir.Trim();
    stConfig.m_strLocalDir.TrimRight(L"\\/");
    stConfig.m_strBackupDir.Trim();
    stConfig.m_strRemoteDir.Replace(L'\\', L'/');
    stConfig.m_strRemoteDir.Trim();
    if (stConfig.m_strRemoteDir.Left(1) != L"/")
        stConfig.m_strRemoteDir = L"/" + stConfig.m_strRemoteDir;
    if (stConfig.m_strRemoteDir.GetLength() > 1)
        stConfig.m_strRemoteDir.TrimRight(L"/");
    CString strMessage;
    if (!stConfig.Validate(strMessage))
    {
        LogOperation(L"校验任务配置", L"失败：" + strMessage, 2);
        AfxMessageBox(strMessage);
        return false;
    }
    return true;
}

// 先确认输入并结束本次连接测试，磁盘保存由主窗口和任务管理器负责。
void CTaskEditDlg::OnOK()
{
    TaskConfig stConfig;
    if (!ReadConfig(stConfig))
        return;
    if (!PathIsDirectoryW(stConfig.m_strLocalDir) &&
        AfxMessageBox(L"本地目录当前不存在，仍然保存？", MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        LogOperation(L"确认任务配置", L"用户取消保存：本地目录不存在");
        return;
    }
    LogOperation(L"确认任务配置", L"校验通过，等待保存");
    m_stConfig = stConfig;
    CancelTest();
    CDialog::OnOK();
}

void CTaskEditDlg::OnCancel()
{
    CancelTest();
    CDialog::OnCancel();
}

void CTaskEditDlg::CancelTest()
{
    ConnectionTestResult stResult = m_testConnection.Snapshot();
    if (!stResult.m_bStarted)
        return;
    LogTestResult();
    if (!m_bTestResultLogged && !stResult.m_bCancelled)
        LogOperation(L"取消测试连接", L"关闭或保存任务配置，已请求取消测试");
    m_testConnection.Reset();
}

// 定时器可能重复观察到完成状态，因此用标记保证最终结果只记录一次。
void CTaskEditDlg::LogTestResult()
{
    ConnectionTestResult stResult = m_testConnection.Snapshot();
    if (!stResult.m_bDone || m_bTestResultLogged)
        return;
    if (m_pLogger)
        m_pLogger->Operation(L"测试连接", (stResult.m_bCancelled ? L"已取消：" : stResult.m_bSucceeded ? L"成功：" : L"失败：") + stResult.m_strText,
                             &stResult.m_stConfig, stResult.m_bCancelled || stResult.m_bSucceeded ? 1 : 2);
    m_bTestResultLogged = true;
}

void CTaskEditDlg::Browse(int nControl)
{
    CString strAction = nControl == IDC_EDIT_LOCAL_WATCH_DIR ? L"选择本地目录" : L"选择备份目录";
    BROWSEINFOW stInfo = {0};
    stInfo.hwndOwner = m_hWnd;
    stInfo.lpszTitle = L"选择文件夹";
    stInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pPidl = SHBrowseForFolderW(&stInfo);
    if (pPidl)
    {
        WCHAR szPath[MAX_PATH] = {0};
        if (SHGetPathFromIDListW(pPidl, szPath))
        {
            SetDlgItemText(nControl, szPath);
            LogOperation(strAction, CString(L"已选择：") + szPath);
        }
        else
            LogOperation(strAction, L"失败：无法取得文件夹路径", 3);
        CoTaskMemFree(pPidl);
    }
    else
        LogOperation(strAction, L"已取消");
}

void CTaskEditDlg::OnBrowseLocal()
{
    Browse(IDC_EDIT_LOCAL_WATCH_DIR);
}

void CTaskEditDlg::OnBrowseBackup()
{
    Browse(IDC_EDIT_LOCAL_BACKUP_DIR);
}

// 仅移动策略启用备份目录，仅临时名上传启用后缀及其标签。
void CTaskEditDlg::OnPolicyChanged()
{
    int nPostAction = ((CComboBox*)GetDlgItem(IDC_CMB_UPLOAD_SUCCESS_LOCAL_ACTION))->GetCurSel();
    BOOL bBackup = nPostAction == POST_MOVE;
    GetDlgItem(IDC_STATIC_LOCAL_BACKUP_DIR)->EnableWindow(bBackup);
    GetDlgItem(IDC_EDIT_LOCAL_BACKUP_DIR)->EnableWindow(bBackup);
    GetDlgItem(IDC_BTN_BROWSE_LOCAL_BACKUP_DIR)->EnableWindow(bBackup);
    BOOL bTempName = IsDlgButtonChecked(IDC_CHK_USE_TEMP_FILENAME) == BST_CHECKED;
    GetDlgItem(IDC_STATIC_TEMP_FILENAME_SUFFIX)->EnableWindow(bTempName);
    GetDlgItem(IDC_EDIT_TEMP_FILENAME_SUFFIX)->EnableWindow(bTempName);
    BOOL bCleanup = IsDlgButtonChecked(IDC_CHK_DELETE_OLD_EMPTY_DIRS) == BST_CHECKED;
    GetDlgItem(IDC_STATIC_EMPTY_DIR_AGE_DAYS)->EnableWindow(bCleanup);
    GetDlgItem(IDC_EDIT_EMPTY_DIR_AGE_DAYS)->EnableWindow(bCleanup);
    GetDlgItem(IDC_STATIC_EMPTY_DIR_AGE_DAYS_UNIT)->EnableWindow(bCleanup);
}

// 已有测试时直接请求取消，否则按当前输入启动后台测试，不再弹开始确认。
void CTaskEditDlg::OnTest()
{
    ConnectionTestResult stResult = m_testConnection.Snapshot();
    if (stResult.m_bStarted && !stResult.m_bDone)
    {
        if (!stResult.m_bCancelled)
            LogOperation(L"取消测试连接", L"已请求取消");
        m_testConnection.RequestStop();
        return;
    }
    TaskConfig stConfig;
    if (!ReadConfig(stConfig))
        return;
    CancelTest();
    m_bTestResultLogged = false;
    CString strTarget;
    strTarget.Format(L"开始测试；服务器=%s:%u；远程目录=%s", (LPCWSTR)stConfig.m_strHost,
                     stConfig.m_unPort, (LPCWSTR)stConfig.m_strRemoteDir);
    if (m_pLogger)
        m_pLogger->Operation(L"测试连接", strTarget, &stConfig);
    if (!m_testConnection.Start(stConfig))
    {
        LogOperation(L"测试连接", L"失败：无法创建测试线程", 3);
        m_bTestResultLogged = true;
        SetDlgItemText(IDC_STATIC_TEST_RESULT, L"无法创建测试线程");
        return;
    }
    SetDlgItemText(IDC_BTN_TEST, L"取消测试");
}

// 界面线程定时读取测试快照，后台线程不直接修改控件。
void CTaskEditDlg::OnTimer(UINT_PTR unId)
{
    ConnectionTestResult stResult = m_testConnection.Snapshot();
    if (stResult.m_bStarted)
    {
        SetDlgItemText(IDC_STATIC_TEST_RESULT, stResult.m_strText);
        if (stResult.m_bDone)
        {
            LogTestResult();
            SetDlgItemText(IDC_BTN_TEST, L"测试连接");
        }
    }
    CDialog::OnTimer(unId);
}
