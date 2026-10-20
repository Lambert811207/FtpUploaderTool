#pragma once
#include "Config.h"
enum FtpError { FE_OK = 0, FE_TIMEOUT = -1, FE_STOPPED = -2, FE_SOCKET = -3, FE_CLOSED = -4, FE_PROTOCOL = -5,
    FE_RESOLVE = -6, FE_LOGIN = -7, FE_SESSION = -8, FE_FILE_TEMP = -20, FE_FILE_PERM = -21, FE_LOCAL_OPEN = -22,
    FE_LOCAL_READ = -23, FE_VERIFY = -24 };
inline bool IsSessionError(int nError) { return nError < 0 && nError > -20 && nError != FE_STOPPED; }
CString FtpErrorText(int nError, int nReplyCode, int nWsaError);
class CFtpSocket {
public:
    CFtpSocket(); ~CFtpSocket();
    void SetStop(HANDLE hStop) { m_hStop = hStop; }
    int Connect(const sockaddr_in& stAddress, DWORD dwTimeout);
    int SendAll(const char* pBytes, int nCount, DWORD dwTimeout);
    int Receive(char* pBytes, int nCapacity, int& nCount, DWORD dwTimeout);
    int Accept(SOCKET sockListener, const sockaddr_in& stPeer, DWORD dwTimeout);
    void ShutdownSend(); void Close(bool bAbort = true);
    SOCKET Get() const { return m_sockSocket; }
    int WsaError() const { return m_nWsaError; }
private:
    int Ready(SOCKET sockSocket, bool bWrite, DWORD dwTimeout);
    SOCKET m_sockSocket; HANDLE m_hStop; int m_nWsaError;
    CFtpSocket(const CFtpSocket&); CFtpSocket& operator=(const CFtpSocket&);
};
class IFtpProgress {
public:
    virtual void Progress(ULONGLONG ullSent) = 0;
    virtual ~IFtpProgress() {}
};
class CFtpClient {
public:
    explicit CFtpClient(const TaskConfig& stConfig, HANDLE hStop);
    ~CFtpClient();
    int Connect();
    int Noop();
    int EnsureDirectory(const CString& strDirectory);
    int Size(const CString& strName, LONGLONG& llSize, bool& bSupported);
    int Upload(HANDLE hFile, const CString& strName, IFtpProgress* pProgress);
    int Rename(const CString& strFrom, const CString& strTo);
    int Delete(const CString& strName);
    void Disconnect();
    bool Connected() const { return m_ftpSocketControl.Get() != INVALID_SOCKET; }
    int LastCode() const { return m_nCode; }
    CString ErrorText(int nError) const { return FtpErrorText(nError, m_nCode, m_ftpSocketControl.WsaError()); }
private:
    int Command(const CString& strCommand);
    int ReadReply(); int ReadLine(CStringA& strALine);
    int DataConnection(CFtpSocket& ftpSocketData, SOCKET& sockListener);
    int MapError() const;
    int Expect(int nCode1, int nCode2 = 0) const;
    TaskConfig m_stConfig; HANDLE m_hStop; CFtpSocket m_ftpSocketControl;
    CStringA m_strABuffer; CString m_strReply, m_strCwd;
    sockaddr_in m_stPeer; UINT m_unCodePage; int m_nCode;
    CFtpClient(const CFtpClient&); CFtpClient& operator=(const CFtpClient&);
};
