#include "stdafx.h"
#include "ConnectionTest.h"
#include "Files.h"
#include "RemoteTemp.h"

// 窗口和测试线程共同持有引用，最后一个引用释放时销毁停止事件及结果。
struct CConnectionTest::State
{
    volatile LONG m_lReferences;
    // 测试结果和临时文件收尾结束后才发布完成标记。
    volatile LONG m_lDone;
    CCritSec m_csLock;
    CString m_strResult;
    TaskConfig m_stConfig;
    CAutoHandle m_hStop;
    bool m_bSucceeded;

    explicit State(const TaskConfig& stConfig)
        : m_lReferences(1),
          m_lDone(0),
          m_stConfig(stConfig),
          m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)),
          m_bSucceeded(false)
    {
    }

    void Release()
    {
        if (InterlockedDecrement(&m_lReferences) == 0)
            delete this;
    }

    void Result(const CString& strText)
    {
        CAutoLock lockGuard(m_csLock);
        m_strResult = strText;
    }
};

// 测试完整的登录、目录、16 字节上传、大小核对与删除流程。
unsigned __stdcall CConnectionTest::ThreadProc(void* pContext)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    State* pTest = (State*)pContext;
    try
    {
        CFtpClient ftpClientSession(pTest->m_stConfig, pTest->m_hStop.Get());
        pTest->Result(L"正在连接并登录…");
        int nError = ftpClientSession.Connect();
        PendingFile stFile;
        FILETIME stFileTime;
        GetSystemTimeAsFileTime(&stFileTime);
        stFile.m_ullModified = ((ULONGLONG)stFileTime.dwHighDateTime << 32) | stFileTime.dwLowDateTime;
        if (!nError)
        {
            pTest->Result(L"正在进入/创建远程目录…");
            nError = ftpClientSession.EnsureDirectory(RemoteDirectory(pTest->m_stConfig, stFile));
        }
        CString strPath;
        WCHAR szTemp[MAX_PATH] = {0};
        WCHAR szName[MAX_PATH] = {0};
        GetTempPathW(_countof(szTemp), szTemp);
        GetTempFileNameW(szTemp, L"ftp", 0, szName);
        strPath = szName;
        CString strRemote = L"_ftptest_" + NewId() + L".tmp";
        CRemoteTempFile tempRemote(ExeDir(), ftpClientSession);
        {
            CAutoHandle hLocal(CreateFileW(strPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL));
            static const char szBytes[] = "FtpUploader-Test";
            if (!hLocal.Valid() || !WriteBytes(hLocal.Get(), szBytes, 16))
                nError = FE_LOCAL_READ;
            if (!nError)
            {
                SetFilePointer(hLocal.Get(), 0, NULL, FILE_BEGIN);
                pTest->Result(L"正在测试上传/大小校验…");
                if (!tempRemote.Begin(pTest->m_stConfig, RemoteDirectory(pTest->m_stConfig, stFile), strRemote))
                    nError = FE_TEMP_JOURNAL;
                else
                    nError = ftpClientSession.Upload(hLocal.Get(), strRemote, NULL);
            }
            if (!nError)
            {
                LONGLONG llSize;
                bool bSupported;
                nError = ftpClientSession.Size(strRemote, llSize, bSupported);
                if (!nError && bSupported && llSize != 16)
                    nError = FE_VERIFY;
            }
            if (!nError)
            {
                nError = ftpClientSession.Delete(strRemote);
                if (!nError && ftpClientSession.LastCode() == 250)
                    tempRemote.Commit();
            }
            CString strResult =
                !nError ? CString(L"连接、登录、上传、校验和删除成功") : ftpClientSession.ErrorText(nError);
            pTest->Result(strResult);
            pTest->m_bSucceeded = nError == FE_OK;
        }
        DeleteFileW(strPath);
    }
    catch (CException* pException)
    {
        pException->Delete();
        pTest->Result(L"连接测试发生异常");
    }
    catch (const std::exception&)
    {
        pTest->Result(L"连接测试发生 C++ 异常");
    }
    // 此时 FTP 会话和临时文件对象已离开作用域，窗口可以读取最终结果。
    InterlockedExchange(&pTest->m_lDone, 1);
    pTest->Release();
    return 0;
}

CConnectionTest::CConnectionTest()
    : m_pState(NULL)
{
}

CConnectionTest::~CConnectionTest()
{
    Reset();
}

// 线程持有独立引用，窗口关闭只释放自己的引用，不让后台访问已析构窗口。
bool CConnectionTest::Start(const TaskConfig& stConfig)
{
    if (m_pState && !InterlockedCompareExchange(&m_pState->m_lDone, 0, 0))
        return false;
    Reset();
    m_pState = new State(stConfig);
    if (!m_pState->m_hStop.Valid())
    {
        Reset();
        return false;
    }
    InterlockedIncrement(&m_pState->m_lReferences);
    CAutoHandle hThread((HANDLE)_beginthreadex(NULL, 0, ThreadProc, m_pState, 0, NULL));
    if (!hThread.Valid())
    {
        m_pState->Release();
        Reset();
        return false;
    }
    return true;
}

void CConnectionTest::RequestStop()
{
    if (m_pState)
        SetEvent(m_pState->m_hStop.Get());
}

void CConnectionTest::Reset()
{
    RequestStop();
    if (m_pState)
    {
        m_pState->Release();
        m_pState = NULL;
    }
}

// 先读取完成标记，再复制结果；未完成时不读取线程尚未发布的成功值。
ConnectionTestResult CConnectionTest::Snapshot() const
{
    ConnectionTestResult stResult;
    if (!m_pState)
        return stResult;
    stResult.m_bStarted = true;
    stResult.m_bDone = InterlockedCompareExchange(&m_pState->m_lDone, 0, 0) != 0;
    stResult.m_bCancelled = Stopped(m_pState->m_hStop.Get());
    stResult.m_stConfig = m_pState->m_stConfig;
    CAutoLock lockGuard(m_pState->m_csLock);
    stResult.m_strText = m_pState->m_strResult;
    stResult.m_bSucceeded = stResult.m_bDone && m_pState->m_bSucceeded;
    return stResult;
}
