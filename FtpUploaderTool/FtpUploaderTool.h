#pragma once
#include "stdafx.h"

// 应用入口负责单实例、网络环境初始化及退出后的系统资源清理。
class CFtpUploaderToolApp : public CWinApp
{
public:
    CFtpUploaderToolApp()
        : m_hInstance(NULL),
          m_bWinsock(false)
    {
    }

    virtual BOOL InitInstance();
    virtual int ExitInstance();

private:
    // 命名互斥体只用于标识当前会话已有实例，进程退出时释放。
    HANDLE m_hInstance;
    // 仅在 Winsock 初始化成功后才执行对应清理。
    bool m_bWinsock;
};
