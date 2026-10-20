#pragma once
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define VC_EXTRALEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define _WINSOCKAPI_
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>
#include <afxole.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <process.h>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <algorithm>
#include <memory>
#include <stdexcept>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
