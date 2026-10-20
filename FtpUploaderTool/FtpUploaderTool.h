#pragma once
#include "stdafx.h"
class CFtpUploaderToolApp : public CWinApp {
public:
    CFtpUploaderToolApp() : m_hInstance(NULL), m_bWinsock(false) {}
    virtual BOOL InitInstance(); virtual int ExitInstance();
private:
    HANDLE m_hInstance; bool m_bWinsock;
};
