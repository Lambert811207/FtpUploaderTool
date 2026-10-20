#include "stdafx.h"
#include "FtpUploaderTool.h"
#include "FtpUploaderToolDlg.h"
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
CFtpUploaderToolApp m_appFtpUploaderTool;
static LONG WINAPI CrashDump(EXCEPTION_POINTERS* pException) {
    CString strDirectory = JoinPath(ExeDir(), L"Dump"); MakeDirs(strDirectory); CString strName = TimeText(); strName.Replace(L':', L'_'); strName.Replace(L' ', L'_');
    CAutoHandle hFile(CreateFileW(JoinPath(strDirectory, L"FtpUploaderTool_" + strName + L".dmp"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (hFile.Valid()) { MINIDUMP_EXCEPTION_INFORMATION stInfo = {GetCurrentThreadId(), pException, FALSE}; MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile.Get(), MiniDumpNormal, &stInfo, NULL, NULL); }
    return EXCEPTION_EXECUTE_HANDLER;
}
BOOL CFtpUploaderToolApp::InitInstance() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX); SetUnhandledExceptionFilter(CrashDump);
    CString strPath = FullPath(ExeDir()); strPath.MakeLower(); DWORD dwHash = 2166136261UL;
    for (int nIndex = 0; nIndex < strPath.GetLength(); ++nIndex) { dwHash ^= (WORD)strPath[nIndex]; dwHash *= 16777619UL; }
    CString strMutex; strMutex.Format(L"Local\\FtpUploaderTool_%08lX", dwHash); m_hInstance = CreateMutexW(NULL, FALSE, strMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) { PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"FtpUploaderTool_Show"), 0, 0); return FALSE; }
    WSADATA stData; if (WSAStartup(MAKEWORD(2, 2), &stData)) { AfxMessageBox(L"Winsock 初始化失败"); return FALSE; } m_bWinsock = true;
    INITCOMMONCONTROLSEX stControls = {sizeof(stControls), ICC_WIN95_CLASSES}; InitCommonControlsEx(&stControls); CWinApp::InitInstance(); AfxOleInit();
    ULONG ulLfh = 2; HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation, &ulLfh, sizeof(ulLfh));
    CFtpUploaderToolDlg dlgDialog; m_pMainWnd = &dlgDialog; dlgDialog.DoModal(); return FALSE;
}
int CFtpUploaderToolApp::ExitInstance() { if (m_bWinsock) WSACleanup(); if (m_hInstance) CloseHandle(m_hInstance); SetThreadExecutionState(ES_CONTINUOUS); return CWinApp::ExitInstance(); }
