#pragma once
#include "stdafx.h"

// 封装临界区，保护不同线程共享的数据。
class CCritSec
{
public:
    CCritSec()
    {
        InitializeCriticalSection(&m_csLock);
    }

    ~CCritSec()
    {
        DeleteCriticalSection(&m_csLock);
    }

    void Lock()
    {
        EnterCriticalSection(&m_csLock);
    }

    void Unlock()
    {
        LeaveCriticalSection(&m_csLock);
    }

private:
    CRITICAL_SECTION m_csLock;
    CCritSec(const CCritSec&);
    CCritSec& operator=(const CCritSec&);
};

// 作用域内加锁，离开作用域时自动释放。
class CAutoLock
{
public:
    explicit CAutoLock(CCritSec& csLock)
        : m_csLock(csLock)
    {
        m_csLock.Lock();
    }

    ~CAutoLock()
    {
        m_csLock.Unlock();
    }

private:
    CCritSec& m_csLock;
    CAutoLock(const CAutoLock&);
    CAutoLock& operator=(const CAutoLock&);
};

// 独占普通 Win32 句柄；不用于 SOCKET 或 FindFirstFile 返回的句柄。
class CAutoHandle
{
public:
    explicit CAutoHandle(HANDLE hHandle = INVALID_HANDLE_VALUE)
        : m_hHandle(hHandle)
    {
    }

    ~CAutoHandle()
    {
        Close();
    }

    bool Valid() const
    {
        return m_hHandle != NULL && m_hHandle != INVALID_HANDLE_VALUE;
    }

    HANDLE Get() const
    {
        return m_hHandle;
    }

    void Close()
    {
        if (Valid())
            CloseHandle(m_hHandle);
        m_hHandle = INVALID_HANDLE_VALUE;
    }

    void Attach(HANDLE hHandle)
    {
        Close();
        m_hHandle = hHandle;
    }

private:
    HANDLE m_hHandle;
    CAutoHandle(const CAutoHandle&);
    CAutoHandle& operator=(const CAutoHandle&);
};

// 独占目录枚举句柄，离开作用域时调用 FindClose。
class CFindHandle
{
public:
    explicit CFindHandle(HANDLE hHandle)
        : m_hHandle(hHandle)
    {
    }

    ~CFindHandle()
    {
        if (m_hHandle != INVALID_HANDLE_VALUE)
            FindClose(m_hHandle);
    }

private:
    HANDLE m_hHandle;
    CFindHandle(const CFindHandle&);
    CFindHandle& operator=(const CFindHandle&);
};

// 使用无符号差值比较毫秒计时，兼容 GetTickCount 回绕。
inline bool TickElapsed(DWORD dwStart, DWORD dwInterval)
{
    return (DWORD)(GetTickCount() - dwStart) >= dwInterval;
}

// 非阻塞检查停止事件，供后台循环及时响应取消。
inline bool Stopped(HANDLE hHandle)
{
    return hHandle && WaitForSingleObject(hHandle, 0) == WAIT_OBJECT_0;
}

CString ExeDir();
CString JoinPath(const CString& strA, const CString& strB);
CString FullPath(const CString& strPath);
// 比较规范化路径，判断目录相同或子路径位于指定目录内。
bool IsWithin(const CString& strParent, const CString& strChild);
bool MakeDirs(const CString& strPath);
CString NewId();
CStringA ToBytes(const CString& strText, UINT unCodePage = CP_UTF8);
CString FromBytes(const CStringA& strABytes, UINT unCodePage = CP_UTF8);
bool WriteBytes(HANDLE hFile, const void* pData, DWORD dwSize);
// 先写临时文件并刷新磁盘，再替换目标文件。
bool AtomicWrite(const CString& strPath, const CStringA& strAData);
CString TimeText();
CString DateText();
CString WinError(DWORD dwError);
// 仅供读取旧版 DPAPI 密码并迁移；新配置仍按要求保存明文。
bool DecryptPassword(const CString& strEncrypted, CString& strPlain);
CString SafeFileName(const CString& strText);
