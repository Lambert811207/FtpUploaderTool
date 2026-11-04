#include "stdafx.h"
#include "Worker.h"
#include "ConnectionTest.h"
#include <stdio.h>

CWinApp probeApp;

int wmain(int argc, wchar_t** argv)
{
    if (argc != 9 || !AfxWinInit(GetModuleHandle(NULL), NULL, GetCommandLine(), 0))
        return 2;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return 3;
    TaskConfig config;
    config.m_unPort = _wtoi(argv[2]);
    CString root(argv[3]);
    config.m_strRemoteDir = argv[4];
    config.m_strLocalDir = JoinPath(root, L"source");
    config.m_bUseTempName = _wtoi(argv[5]) != 0;
    config.m_nRemoteExist = _wtoi(argv[6]);
    const bool expected = _wtoi(argv[7]) != 0;
    config.m_strPassword = argv[8];
    config.m_unConnTimeout = config.m_unIoTimeout = 3;
    config.m_unScanInterval = config.m_unStableSec = config.m_unHeartbeat = 1;
    config.m_unUploadIntervalMs = 50;
    config.m_unMaxFileRetry = 1;
    config.m_nPostAction = POST_KEEP;
    config.m_unTaskNumber = 1;
    bool success = false;
    CString description;
    DWORD started = GetTickCount();
    if (wcscmp(argv[1], L"connection") == 0)
    {
        CConnectionTest test;
        if (!test.Start(config))
            return 4;
        while (!test.Snapshot().m_bDone && !TickElapsed(started, 10000))
            Sleep(10);
        ConnectionTestResult result = test.Snapshot();
        if (!result.m_bDone)
            return 5;
        success = result.m_bSucceeded;
        description = result.m_strText;
    }
    else if (wcscmp(argv[1], L"login") == 0 || wcscmp(argv[1], L"directory") == 0)
    {
        CAutoHandle stop(CreateEvent(NULL, TRUE, FALSE, NULL));
        CFtpClient ftp(config, stop.Get());
        int error = ftp.Connect();
        CString relative;
        if (!error && wcscmp(argv[1], L"directory") == 0)
            error = ftp.PrepareUploadDirectory(config.m_strRemoteDir, relative);
        if (!error && wcscmp(argv[1], L"directory") == 0)
            error = ftp.PrepareUploadDirectory(config.m_strRemoteDir, relative);
        success = error == FE_OK;
        description = ftp.ErrorText(error);
    }
    else
    {
        GlobalConfig global;
        CLogger logger;
        if (!logger.Start(root, global))
            return 6;
        CTaskWorker worker(config, root, logger);
        if (!worker.Start())
            return 7;
        TaskStatus status;
        UINT expectedUploads = wcscmp(argv[1], L"worker-multiple") == 0 ? 2 : 1;
        do
        {
            status = worker.Status();
            if (status.m_unTodayOk >= expectedUploads || status.m_unTodayFail || status.m_unParked)
                break;
            Sleep(10);
        } while (!TickElapsed(started, 10000));
        success = status.m_unTodayOk == expectedUploads;
        description = status.m_strLastError;
        worker.RequestStop();
        while (!worker.PollStopped() && !TickElapsed(started, 12000))
            Sleep(10);
        if (worker.Running())
            return 8;
        logger.Stop();
    }
    printf("success=%d %s\n", success ? 1 : 0, (LPCSTR)ToBytes(description, CP_UTF8));
    return success == expected ? 0 : 1;
}
