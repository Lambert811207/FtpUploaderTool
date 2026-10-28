#include "stdafx.h"
#include "FtpUploaderTool.h"
#include "FtpUploaderToolDlg.h"
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
CFtpUploaderToolApp m_appFtpUploaderTool;

// 未处理异常时生成最小转储，供定位崩溃使用，不在此恢复上传任务。
static LONG WINAPI CrashDump(EXCEPTION_POINTERS* pException)
{
    CString strDirectory = JoinPath(ExeDir(), L"Dump");
    MakeDirs(strDirectory);
    CString strName = TimeText();
    strName.Replace(L':', L'_');
    strName.Replace(L' ', L'_');
    CAutoHandle hFile(CreateFileW(JoinPath(strDirectory, L"FtpUploaderTool_" + strName + L".dmp"), GENERIC_WRITE, 0,
                                  NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (hFile.Valid())
    {
        MINIDUMP_EXCEPTION_INFORMATION stInfo = {GetCurrentThreadId(), pException, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile.Get(), MiniDumpNormal, &stInfo, NULL, NULL);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// 先检查单实例，再初始化网络和 MFC 环境并运行主对话框。
BOOL CFtpUploaderToolApp::InitInstance()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(CrashDump);
    // 使用当前 Windows 会话的命名互斥体，重复启动时通知已有窗口显示。
    m_hInstance = CreateMutexW(NULL, FALSE, L"Local\\FtpUploaderTool_SingleInstance");
    DWORD dwError = GetLastError();
    if (!m_hInstance)
    {
        AfxMessageBox(L"无法检查程序运行状态：" + WinError(dwError), MB_ICONERROR);
        return FALSE;
    }
    if (dwError == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(m_hInstance);
        m_hInstance = NULL;
        PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"FtpUploaderTool_Show"), 0, 0);
        AfxMessageBox(L"FtpUploaderTool 已在运行，请勿重复打开。", MB_ICONINFORMATION);
        return FALSE;
    }
    WSADATA stData;
    if (WSAStartup(MAKEWORD(2, 2), &stData))
    {
        AfxMessageBox(L"Winsock 初始化失败");
        return FALSE;
    }
    m_bWinsock = true;
    INITCOMMONCONTROLSEX stControls = {sizeof(stControls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&stControls);
    CWinApp::InitInstance();
    AfxOleInit();
    ULONG ulLfh = 2;
    HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation, &ulLfh, sizeof(ulLfh));
    CFtpUploaderToolDlg dlgDialog;
    m_pMainWnd = &dlgDialog;
    dlgDialog.DoModal();
    return FALSE;
}

// 应用退出后释放网络环境、单实例标识及运行期间的睡眠限制。
int CFtpUploaderToolApp::ExitInstance()
{
    if (m_bWinsock)
        WSACleanup();
    if (m_hInstance)
        CloseHandle(m_hInstance);
    SetThreadExecutionState(ES_CONTINUOUS);
    return CWinApp::ExitInstance();
}
