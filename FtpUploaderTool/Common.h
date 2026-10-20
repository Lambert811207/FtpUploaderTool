#pragma once
#include "stdafx.h"

class CCritSec {
public:
    CCritSec() { InitializeCriticalSection(&m_csLock); }
    ~CCritSec() { DeleteCriticalSection(&m_csLock); }
    void Lock() { EnterCriticalSection(&m_csLock); }
    void Unlock() { LeaveCriticalSection(&m_csLock); }
private:
    CRITICAL_SECTION m_csLock;
    CCritSec(const CCritSec&); CCritSec& operator=(const CCritSec&);
};
class CAutoLock {
public:
    explicit CAutoLock(CCritSec& csLock) : m_csLock(csLock) { m_csLock.Lock(); }
    ~CAutoLock() { m_csLock.Unlock(); }
private:
    CCritSec& m_csLock;
    CAutoLock(const CAutoLock&); CAutoLock& operator=(const CAutoLock&);
};
class CAutoHandle {
public:
    explicit CAutoHandle(HANDLE hHandle = INVALID_HANDLE_VALUE) : m_hHandle(hHandle) {}
    ~CAutoHandle() { Close(); }
    bool Valid() const { return m_hHandle != NULL && m_hHandle != INVALID_HANDLE_VALUE; }
    HANDLE Get() const { return m_hHandle; }
    void Close() { if (Valid()) CloseHandle(m_hHandle); m_hHandle = INVALID_HANDLE_VALUE; }
    void Attach(HANDLE hHandle) { Close(); m_hHandle = hHandle; }
private:
    HANDLE m_hHandle;
    CAutoHandle(const CAutoHandle&); CAutoHandle& operator=(const CAutoHandle&);
};
class CFindHandle {
public:
    explicit CFindHandle(HANDLE hHandle) : m_hHandle(hHandle) {}
    ~CFindHandle() { if (m_hHandle != INVALID_HANDLE_VALUE) FindClose(m_hHandle); }
private:
    HANDLE m_hHandle;
    CFindHandle(const CFindHandle&); CFindHandle& operator=(const CFindHandle&);
};
inline bool TickElapsed(DWORD dwStart, DWORD dwInterval) { return (DWORD)(GetTickCount() - dwStart) >= dwInterval; }
inline bool Stopped(HANDLE hHandle) { return hHandle && WaitForSingleObject(hHandle, 0) == WAIT_OBJECT_0; }
CString ExeDir();
CString JoinPath(const CString& strA, const CString& strB);
CString FullPath(const CString& strPath);
bool IsWithin(const CString& strParent, const CString& strChild);
bool MakeDirs(const CString& strPath);
CString NewId();
CStringA ToBytes(const CString& strText, UINT unCodePage = CP_UTF8);
CString FromBytes(const CStringA& strABytes, UINT unCodePage = CP_UTF8);
bool WriteBytes(HANDLE hFile, const void* pData, DWORD dwSize);
bool AtomicWrite(const CString& strPath, const CStringA& strAData);
CString TimeText();
CString DateText();
CString WinError(DWORD dwError);
CString EncryptPassword(const CString& strPlain);
bool DecryptPassword(const CString& strEncrypted, CString& strPlain);
CString SafeFileName(const CString& strText);
bool DeleteTree(const CString& strPath, HANDLE hStop);
