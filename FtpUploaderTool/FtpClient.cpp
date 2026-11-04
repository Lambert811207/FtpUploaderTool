#include "stdafx.h"
#include "FtpClient.h"

CString FtpErrorText(int nError, int nReplyCode, int nWsaError)
{
    LPCWSTR pszText = L"未知错误";
    switch (nError)
    {
        case FE_OK:
            pszText = L"成功";
            break;
        case FE_TIMEOUT:
            pszText = L"网络操作超时";
            break;
        case FE_STOPPED:
            pszText = L"操作已取消";
            break;
        case FE_SOCKET:
            pszText = L"套接字连接失败";
            break;
        case FE_CLOSED:
            pszText = L"服务器关闭连接";
            break;
        case FE_PROTOCOL:
            pszText = L"FTP 应答格式错误";
            break;
        case FE_RESOLVE:
            pszText = L"无法解析服务器地址";
            break;
        case FE_LOGIN:
            pszText = L"登录失败，请核对账号密码或服务器连接限制";
            break;
        case FE_SESSION:
            pszText = L"FTP 会话或数据连接中断";
            break;
        case FE_FILE_TEMP:
            pszText = L"文件暂不可用或服务器磁盘已满";
            break;
        case FE_FILE_PERM:
            pszText = L"目录/文件无权限或文件名不被接受";
            break;
        case FE_LOCAL_OPEN:
            pszText = L"本地文件被占用或无法读取";
            break;
        case FE_LOCAL_READ:
            pszText = L"本地文件读取失败";
            break;
        case FE_VERIFY:
            pszText = L"上传大小校验不一致";
            break;
        case FE_REMOTE_EXISTS:
            pszText = L"服务器目标文件已存在，未上传，保留本地文件";
            break;
        case FE_REMOTE_STATE_UNKNOWN:
            pszText = L"无法确认服务器目标文件状态，未上传，保留本地文件";
            break;
        case FE_TEMP_JOURNAL:
            pszText = L"无法写入远程临时文件清理记录，未上传，保留本地文件";
            break;
        case FE_HISTORY:
            pszText = L"无法写入上传成功记录，不确认成功，保留本地文件";
            break;
    }
    CString strResult(pszText);
    CString strExtra;
    // 本地错误与上一条 FTP 应答无关，不附加可能过期的应答码。
    bool bLocal = nError == FE_LOCAL_OPEN || nError == FE_LOCAL_READ || nError == FE_TEMP_JOURNAL ||
                  nError == FE_HISTORY;
    if (nReplyCode >= 400 && !bLocal)
    {
        strExtra.Format(L" (FTP %d)", nReplyCode);
        strResult += strExtra;
    }
    if (nError == FE_SOCKET && nWsaError)
    {
        strExtra.Format(L" (WSA %d)", nWsaError);
        strResult += strExtra;
    }
    return strResult;
}

CFtpSocket::CFtpSocket()
    : m_sockSocket(INVALID_SOCKET),
      m_hStop(NULL),
      m_nWsaError(0)
{
}

CFtpSocket::~CFtpSocket()
{
    Close();
}

// select 最多等待 200 毫秒一个分片，以便及时检查停止事件。
int CFtpSocket::Ready(SOCKET sockSocket, bool bWrite, DWORD dwTimeout)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        if (Stopped(m_hStop))
            return FE_STOPPED;
        DWORD dwElapsed = (DWORD)(GetTickCount() - dwStart);
        if (dwElapsed >= dwTimeout)
            return FE_TIMEOUT;
        DWORD dwSlice = min((DWORD)200, dwTimeout - dwElapsed);
        fd_set stFds;
        fd_set stErrors;
        FD_ZERO(&stFds);
        FD_ZERO(&stErrors);
        FD_SET(sockSocket, &stFds);
        FD_SET(sockSocket, &stErrors);
        timeval stTv;
        stTv.tv_sec = 0;
        stTv.tv_usec = dwSlice * 1000;
        int nResult = select(0, bWrite ? NULL : &stFds, bWrite ? &stFds : NULL, &stErrors, &stTv);
        if (nResult == SOCKET_ERROR)
        {
            m_nWsaError = WSAGetLastError();
            return FE_SOCKET;
        }
        if (FD_ISSET(sockSocket, &stErrors))
        {
            int nSize = sizeof(m_nWsaError);
            getsockopt(sockSocket, SOL_SOCKET, SO_ERROR, (char*)&m_nWsaError, &nSize);
            return FE_SOCKET;
        }
        if (nResult > 0)
            return FE_OK;
    }
}

// 使用非阻塞连接，完成后读取 SO_ERROR 确认真正连接成功。
int CFtpSocket::Connect(const sockaddr_in& stAddress, DWORD dwTimeout)
{
    Close();
    m_nWsaError = 0;
    m_sockSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_sockSocket == INVALID_SOCKET)
    {
        m_nWsaError = WSAGetLastError();
        return FE_SOCKET;
    }
    u_long stNonblocking = 1;
    ioctlsocket(m_sockSocket, FIONBIO, &stNonblocking);
    BOOL bOn = TRUE;
    setsockopt(m_sockSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&bOn, sizeof(bOn));
    tcp_keepalive stKeep = {1, 15000, 3000};
    DWORD dwReturned = 0;
    WSAIoctl(m_sockSocket, SIO_KEEPALIVE_VALS, &stKeep, sizeof(stKeep), NULL, 0, &dwReturned, NULL, NULL);
    int nResult = connect(m_sockSocket, (const sockaddr*)&stAddress, sizeof(stAddress));
    if (nResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        m_nWsaError = WSAGetLastError();
        Close();
        return FE_SOCKET;
    }
    int nError = Ready(m_sockSocket, true, dwTimeout);
    if (!nError)
    {
        int nSize = sizeof(m_nWsaError);
        if (getsockopt(m_sockSocket, SOL_SOCKET, SO_ERROR, (char*)&m_nWsaError, &nSize) != 0 || m_nWsaError)
            nError = FE_SOCKET;
    }
    if (nError)
        Close();
    return nError;
}

int CFtpSocket::SendAll(const char* pBytes, int nCount, DWORD dwTimeout)
{
    DWORD dwProgress = GetTickCount();
    while (nCount > 0)
    {
        DWORD dwElapsed = (DWORD)(GetTickCount() - dwProgress);
        if (dwElapsed >= dwTimeout)
            return FE_TIMEOUT;
        int nError = Ready(m_sockSocket, true, dwTimeout - dwElapsed);
        if (nError)
            return nError;
        int nResult = send(m_sockSocket, pBytes, nCount, 0);
        if (nResult == SOCKET_ERROR)
        {
            m_nWsaError = WSAGetLastError();
            if (m_nWsaError == WSAEWOULDBLOCK)
                continue;
            return FE_SOCKET;
        }
        if (!nResult)
            return FE_CLOSED;
        pBytes += nResult;
        nCount -= nResult;
        dwProgress = GetTickCount();
    }
    return FE_OK;
}

int CFtpSocket::Receive(char* pBytes, int nCapacity, int& nCount, DWORD dwTimeout)
{
    nCount = 0;
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        DWORD dwElapsed = (DWORD)(GetTickCount() - dwStart);
        if (dwElapsed >= dwTimeout)
            return FE_TIMEOUT;
        int nError = Ready(m_sockSocket, false, dwTimeout - dwElapsed);
        if (nError)
            return nError;
        int nResult = recv(m_sockSocket, pBytes, nCapacity, 0);
        if (nResult == 0)
            return FE_CLOSED;
        if (nResult == SOCKET_ERROR)
        {
            m_nWsaError = WSAGetLastError();
            if (m_nWsaError == WSAEWOULDBLOCK)
                continue;
            return FE_SOCKET;
        }
        nCount = nResult;
        return FE_OK;
    }
}

int CFtpSocket::Accept(SOCKET sockListener, const sockaddr_in& stPeer, DWORD dwTimeout)
{
    int nError = Ready(sockListener, false, dwTimeout);
    if (nError)
        return nError;
    sockaddr_in stAddress;
    int nLength = sizeof(stAddress);
    m_sockSocket = accept(sockListener, (sockaddr*)&stAddress, &nLength);
    if (m_sockSocket == INVALID_SOCKET)
    {
        m_nWsaError = WSAGetLastError();
        return FE_SOCKET;
    }
    if (stAddress.sin_addr.s_addr != stPeer.sin_addr.s_addr)
    {
        Close();
        return FE_PROTOCOL;
    }
    u_long stNb = 1;
    ioctlsocket(m_sockSocket, FIONBIO, &stNb);
    return FE_OK;
}

void CFtpSocket::ShutdownSend()
{
    if (m_sockSocket != INVALID_SOCKET)
        shutdown(m_sockSocket, SD_SEND);
}

void CFtpSocket::Close(bool bAbort)
{
    if (m_sockSocket == INVALID_SOCKET)
        return;
    if (bAbort)
    {
        linger stValue = {1, 0};
        setsockopt(m_sockSocket, SOL_SOCKET, SO_LINGER, (char*)&stValue, sizeof(stValue));
    }
    closesocket(m_sockSocket);
    m_sockSocket = INVALID_SOCKET;
}

// 解析线程与调用方共同持有引用；调用方超时或取消后放弃等待，最后一个引用释放时销毁。
struct ResolveState
{
    volatile LONG m_lReferences;
    CStringA m_strAHost;
    CStringA m_strAPort;
    CAutoHandle m_hDone;
    sockaddr_in m_stAddress;
    bool m_bResolved;

    ResolveState(const CStringA& strAHost, const CStringA& strAPort)
        : m_lReferences(1),
          m_strAHost(strAHost),
          m_strAPort(strAPort),
          m_hDone(CreateEvent(NULL, TRUE, FALSE, NULL)),
          m_bResolved(false)
    {
        ZeroMemory(&m_stAddress, sizeof(m_stAddress));
    }

    void Release()
    {
        if (InterlockedDecrement(&m_lReferences) == 0)
            delete this;
    }
};

static bool LookupAddress(const CStringA& strAHost, const CStringA& strAPort, int nFlags, sockaddr_in& stAddress)
{
    addrinfo stHints = {0};
    addrinfo* pAddresses = NULL;
    stHints.ai_flags = nFlags;
    stHints.ai_family = AF_INET;
    stHints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(strAHost, strAPort, &stHints, &pAddresses) != 0 || !pAddresses)
        return false;
    bool bFound = pAddresses->ai_addrlen >= sizeof(stAddress);
    if (bFound)
        stAddress = *(sockaddr_in*)pAddresses->ai_addr;
    freeaddrinfo(pAddresses);
    return bFound;
}

static unsigned __stdcall ResolveThread(void* pContext)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    ResolveState* pState = (ResolveState*)pContext;
    pState->m_bResolved = LookupAddress(pState->m_strAHost, pState->m_strAPort, 0, pState->m_stAddress);
    SetEvent(pState->m_hDone.Get());
    pState->Release();
    return 0;
}

// IP 地址直接转换；域名在辅助线程解析，等待受停止事件及连接建立超时限制。
static int ResolveAddress(const CString& strHost, UINT unPort, HANDLE hStop, DWORD dwTimeout, sockaddr_in& stAddress)
{
    CStringA strAHost = ToBytes(strHost, CP_ACP);
    CStringA strAPort;
    strAPort.Format("%u", unPort);
    if (LookupAddress(strAHost, strAPort, AI_NUMERICHOST, stAddress))
        return FE_OK;
    ResolveState* pState = new ResolveState(strAHost, strAPort);
    if (!pState->m_hDone.Valid())
    {
        pState->Release();
        return FE_RESOLVE;
    }
    InterlockedIncrement(&pState->m_lReferences);
    CAutoHandle hThread((HANDLE)_beginthreadex(NULL, 0, ResolveThread, pState, 0, NULL));
    if (!hThread.Valid())
    {
        pState->Release();
        pState->Release();
        return FE_RESOLVE;
    }
    HANDLE arrHandles[] = {pState->m_hDone.Get(), hStop};
    DWORD dwWait = WaitForMultipleObjects(hStop ? 2 : 1, arrHandles, FALSE, dwTimeout);
    int nError = FE_RESOLVE;
    if (dwWait == WAIT_OBJECT_0 + 1)
        nError = FE_STOPPED;
    else if (dwWait == WAIT_OBJECT_0 && pState->m_bResolved)
    {
        stAddress = pState->m_stAddress;
        nError = FE_OK;
    }
    pState->Release();
    return nError;
}

CFtpClient::CFtpClient(const TaskConfig& stConfig, HANDLE hStop)
    : m_stConfig(stConfig),
      m_hStop(hStop),
      m_bResolved(false),
      m_unCodePage(stConfig.m_nEncoding == ENC_UTF8 ? CP_UTF8 : 936),
      m_nCode(0)
{
    ZeroMemory(&m_stPeer, sizeof(m_stPeer));
    ZeroMemory(&m_stResolved, sizeof(m_stResolved));
    m_ftpSocketControl.SetStop(hStop);
}

CFtpClient::~CFtpClient()
{
    Disconnect();
}

void CFtpClient::Disconnect()
{
    m_ftpSocketControl.Close();
    m_strCwd.Empty();
    m_strLoginDir.Empty();
    m_strABuffer.Empty();
}

int CFtpClient::MapError() const
{
    switch (m_nCode)
    {
        case 421:
        case 425:
        case 426:
            return FE_SESSION;
        case 530:
            return FE_LOGIN;
        case 450:
        case 451:
        case 452:
        case 552:
            return FE_FILE_TEMP;
        case 550:
        case 553:
        case 501:
        case 504:
            return FE_FILE_PERM;
        default:
            return FE_PROTOCOL;
    }
}

int CFtpClient::Expect(int nCode1, int nCode2) const
{
    return m_nCode == nCode1 || (nCode2 != 0 && m_nCode == nCode2) ? FE_OK : MapError();
}

int CFtpClient::ReadLine(CStringA& strALine)
{
    DWORD dwStart = GetTickCount();
    for (;;)
    {
        int nResult = m_strABuffer.Find('\n');
        if (nResult >= 0)
        {
            strALine = m_strABuffer.Left(nResult);
            if (!strALine.IsEmpty() && strALine[strALine.GetLength() - 1] == '\r')
                strALine.Truncate(strALine.GetLength() - 1);
            m_strABuffer.Delete(0, nResult + 1);
            return FE_OK;
        }
        if (m_strABuffer.GetLength() > 65536)
            return FE_PROTOCOL;
        DWORD dwElapsed = GetTickCount() - dwStart;
        DWORD dwTimeout = m_stConfig.m_unIoTimeout * 1000;
        if (dwElapsed >= dwTimeout)
            return FE_TIMEOUT;
        char szBytes[4096];
        int nCount = 0;
        int nError = m_ftpSocketControl.Receive(szBytes, sizeof(szBytes), nCount, dwTimeout - dwElapsed);
        if (nError)
            return nError;
        m_strABuffer.Append(szBytes, nCount);
    }
}

// 解析 FTP 单行及多行应答，限制缓冲长度与总等待时间。
int CFtpClient::ReadReply()
{
    CStringA strALine;
    CStringA strAAll;
    int nError = ReadLine(strALine);
    if (nError)
        return nError;
    if (strALine.GetLength() < 4 || strALine[0] < '1' || strALine[0] > '5' || !isdigit((BYTE)strALine[1]) ||
        !isdigit((BYTE)strALine[2]) || (strALine[3] != '-' && strALine[3] != ' '))
        return FE_PROTOCOL;
    int nCode = atoi(strALine.Left(3));
    strAAll = strALine;
    DWORD dwStart = GetTickCount();
    if (strALine[3] == '-')
    {
        CStringA strAEnd;
        strAEnd.Format("%03d ", nCode);
        do
        {
            if (TickElapsed(dwStart, m_stConfig.m_unIoTimeout * 1000))
                return FE_TIMEOUT;
            nError = ReadLine(strALine);
            if (nError)
                return nError;
            strAAll += "\n";
            strAAll += strALine;
            if (strAAll.GetLength() > 65536)
                return FE_PROTOCOL;
        } while (strALine.Left(4) != strAEnd);
    }
    m_nCode = nCode;
    m_strReply = FromBytes(strAAll, m_unCodePage);
    return nCode == 421 || nCode == 425 || nCode == 426 ? FE_SESSION : FE_OK;
}

// 一条命令对应一次应答读取，拒绝换行以免意外拼入其他 FTP 命令。
int CFtpClient::Command(const CString& strCommand)
{
    CString strVerb = strCommand.SpanExcluding(L" \r\n");
    strVerb.MakeUpper();
    bool bCredential = strVerb == L"PASS" || strVerb == L"USER" || strVerb == L"ACCT";
    m_strCommand = bCredential ? strVerb + L" [已隐藏]" : strCommand;
    m_strReply.Empty();
    m_nCode = 0;
    if (strCommand.FindOneOf(L"\r\n") >= 0)
    {
        m_strCommand = strVerb;
        return FE_FILE_PERM;
    }
    CStringA strABytes = ToBytes(strCommand, m_unCodePage) + "\r\n";
    int nError = m_ftpSocketControl.SendAll(strABytes, strABytes.GetLength(), m_stConfig.m_unIoTimeout * 1000);
    return nError ? nError : ReadReply();
}

// 重建控制连接，完成登录、文件名编码协商及二进制传输设置。
int CFtpClient::Connect()
{
    Disconnect();
    m_nCode = 0;
    m_strReply.Empty();
    m_strCommand = L"连接欢迎消息";
    m_unCodePage = m_stConfig.m_nEncoding == ENC_UTF8 ? CP_UTF8 : 936;
    sockaddr_in stAddress;
    int nResolve =
        ResolveAddress(m_stConfig.m_strHost, m_stConfig.m_unPort, m_hStop, m_stConfig.m_unConnTimeout * 1000, stAddress);
    if (nResolve == FE_OK)
    {
        m_stResolved = stAddress;
        m_bResolved = true;
    }
    else if (nResolve != FE_RESOLVE || !m_bResolved)
        return nResolve;
    // 解析失败或超时时沿用上次成功的地址，连接失败仍按网络错误重连。
    m_stPeer = m_stResolved;
    if (Stopped(m_hStop))
        return FE_STOPPED;
    int nError = m_ftpSocketControl.Connect(m_stPeer, m_stConfig.m_unConnTimeout * 1000);
    if (nError)
        return nError;
    for (int nIndex = 0; nIndex < 8; ++nIndex)
    {
        nError = ReadReply();
        if (nError)
            return nError;
        if (m_nCode != 120)
            break;
    }
    if (m_nCode != 220)
        return FE_SESSION;
    nError = Command(L"USER " + (m_stConfig.m_strUser.IsEmpty() ? CString(L"anonymous") : m_stConfig.m_strUser));
    if (nError)
        return nError;
    if (m_nCode == 331)
    {
        nError = Command(L"PASS " + m_stConfig.m_strPassword);
        if (nError)
            return nError;
    }
    if (m_nCode != 230)
        return FE_LOGIN;
    if (m_stConfig.m_nEncoding != ENC_GBK)
    {
        nError = Command(L"FEAT");
        if (nError)
            return nError;
        if (m_nCode == 530)
            return FE_LOGIN;
        CString strFeatures(m_strReply);
        strFeatures.MakeUpper();
        if (m_stConfig.m_nEncoding == ENC_UTF8 || (m_nCode == 211 && strFeatures.Find(L"UTF8") >= 0))
        {
            nError = Command(L"OPTS UTF8 ON");
            if (nError)
                return nError;
            if (m_nCode == 530)
                return FE_LOGIN;
            m_unCodePage = CP_UTF8;
        }
    }
    nError = Command(L"TYPE I");
    if (!nError)
        nError = Expect(200);
    // 相对路径以每次登录的初始目录为基准，不能以最后一个文件的目录为基准。
    if (!nError && m_stConfig.m_strRemoteDir.Left(1) != L"/")
    {
        nError = Command(L"PWD");
        if (!nError)
            nError = Expect(257);
        if (nError)
            return nError;
        int nQuote = m_strReply.Find(L'"');
        if (nQuote < 0)
            return FE_PROTOCOL;
        CString strDirectory;
        bool bClosed = false;
        for (int nIndex = nQuote + 1; nIndex < m_strReply.GetLength(); ++nIndex)
        {
            WCHAR ch = m_strReply[nIndex];
            if (ch == L'"')
            {
                if (nIndex + 1 < m_strReply.GetLength() && m_strReply[nIndex + 1] == L'"')
                    ++nIndex;
                else
                {
                    bClosed = true;
                    break;
                }
            }
            strDirectory += ch;
        }
        if (!bClosed || strDirectory.Left(1) != L"/" || strDirectory.FindOneOf(L"\r\n") >= 0)
            return FE_PROTOCOL;
        strDirectory.TrimRight(L'/');
        m_strLoginDir = strDirectory.IsEmpty() ? CString(L"/") : strDirectory;
        m_strCwd = m_strLoginDir;
    }
    return nError;
}

int CFtpClient::Noop()
{
    int nError = Command(L"NOOP");
    return nError ? nError : Expect(200);
}

CString CFtpClient::AbsoluteDirectory(const CString& strDirectory) const
{
    CString strResult(strDirectory);
    if (strResult.Left(1) != L"/")
    {
        if (m_strLoginDir.IsEmpty())
            return CString();
        strResult = m_strLoginDir + (m_strLoginDir == L"/" ? L"" : L"/") + strResult;
    }
    strResult.TrimRight(L'/');
    return strResult.IsEmpty() ? CString(L"/") : strResult;
}

// 使用绝对路径逐层检查/创建，不要求业务账号能进入 FTP 根目录。
int CFtpClient::EnsureDirectory(const CString& strRequested)
{
    CString strDirectory = AbsoluteDirectory(strRequested);
    if (strDirectory.IsEmpty())
        return FE_PROTOCOL;
    if (m_strCwd == strDirectory)
        return FE_OK;
    // 初次从登录目录定位时保留用户填写的相对路径，后续使用已锚定的绝对路径。
    int nError = Command(L"CWD " + (strRequested.Left(1) != L"/" && m_strCwd == m_strLoginDir
                                        ? strRequested : strDirectory));
    if (nError)
        return nError;
    if (m_nCode == 250)
    {
        m_strCwd = strDirectory;
        return FE_OK;
    }
    if (m_nCode != 550)
        return MapError();
    if (strDirectory == L"/")
        return MapError();
    CString strPrefix(m_strCwd);
    strPrefix.TrimRight(L'/');
    int nStart = 0;
    if (!strPrefix.IsEmpty() && strDirectory.Left(strPrefix.GetLength() + 1) == strPrefix + L"/")
        nStart = strPrefix.GetLength() + 1;
    else
        strPrefix.Empty();
    m_strCwd.Empty();
    CString strPart;
    while (!(strPart = strDirectory.Tokenize(L"/", nStart)).IsEmpty())
    {
        strPrefix += L"/" + strPart;
        nError = Command(L"CWD " + strPrefix);
        if (nError)
            return nError;
        if (m_nCode == 250)
            continue;
        if (m_nCode != 550)
            return MapError();
        nError = Command(L"MKD " + strPrefix);
        if (nError)
            return nError;
        if (m_nCode != 257 && m_nCode != 250 && m_nCode != 550)
            return MapError();
        nError = Command(L"CWD " + strPrefix);
        if (nError)
            return nError;
        if ((nError = Expect(250)) != FE_OK)
            return nError;
    }
    m_strCwd = strDirectory;
    return FE_OK;
}

// 与 WinINet 旧程序一致：从配置目录发送带子路径的 STOR/SIZE/RNTO 等命令。
int CFtpClient::PrepareUploadDirectory(const CString& strDirectory, CString& strRelativeDir)
{
    strRelativeDir.Empty();
    CString strBase(m_stConfig.m_strRemoteDir);
    // 日期、机器名和 {sub} 展开部分属于相对路径，固定前缀作为会话目录。
    int nPlaceholder = strBase.Find(L'{');
    if (nPlaceholder >= 0)
    {
        strBase = strBase.Left(nPlaceholder);
        strBase = strBase.Left(strBase.ReverseFind(L'/') + 1);
    }
    strBase.TrimRight(L'/');
    if (strBase.IsEmpty())
        strBase = m_stConfig.m_strRemoteDir.Left(1) == L"/" ? CString(L"/") : m_strLoginDir;
    CString strRequestedBase(strBase);
    strBase = AbsoluteDirectory(strBase);
    CString strTarget = AbsoluteDirectory(strDirectory);
    if (strBase.IsEmpty() || strTarget.IsEmpty())
        return FE_PROTOCOL;
    CString strPrefix = strBase == L"/" ? strBase : strBase + L"/";
    if (strTarget != strBase && strTarget.Left(strPrefix.GetLength()) != strPrefix)
        return FE_FILE_PERM;
    int nError = EnsureDirectory(strRequestedBase);
    if (nError)
        return nError;
    nError = EnsureDirectory(strTarget);
    if (nError)
        return nError;
    nError = EnsureDirectory(strBase);
    if (!nError && strTarget != strBase)
    {
        strRelativeDir = strTarget.Mid(strPrefix.GetLength());
        strRelativeDir.TrimRight(L'/');
        if (!strRelativeDir.IsEmpty())
            strRelativeDir += L"/";
    }
    return nError;
}

CString CFtpClient::ErrorText(int nError) const
{
    CString strText = FtpErrorText(nError, m_nCode, m_ftpSocketControl.WsaError());
    bool bLocal = nError == FE_LOCAL_OPEN || nError == FE_LOCAL_READ || nError == FE_TEMP_JOURNAL ||
                  nError == FE_HISTORY || nError == FE_REMOTE_EXISTS || nError == FE_VERIFY || nError == FE_STOPPED;
    if (nError == FE_OK || bLocal || m_strCommand.IsEmpty())
        return strText;
    CString strCommand(m_strCommand);
    CString strReply(m_strReply);
    if (strCommand.Left(5) == L"PASS " || strCommand.Left(5) == L"USER " || strCommand.Left(5) == L"ACCT ")
        strReply = L"登录应答内容已隐藏";
    if (!m_stConfig.m_strPassword.IsEmpty())
    {
        strCommand.Replace(m_stConfig.m_strPassword, L"[已隐藏]");
        strReply.Replace(m_stConfig.m_strPassword, L"[已隐藏]");
    }
    strReply.Replace(L'\r', L' ');
    strReply.Replace(L'\n', L' ');
    strText += L"；命令=" + strCommand.Left(1024);
    if (!strReply.IsEmpty())
        strText += L"；服务器回复=" + strReply.Left(2048);
    return strText;
}

int CFtpClient::Size(const CString& strName, LONGLONG& llSize, bool& bSupported)
{
    llSize = -1;
    bSupported = true;
    int nError = Command(L"SIZE " + strName);
    if (nError)
        return nError;
    if (m_nCode == 213)
    {
        CString strValue = m_strReply.Mid(4);
        strValue.Trim();
        WCHAR* pEnd = NULL;
        llSize = _wcstoi64(strValue, &pEnd, 10);
        return pEnd && *pEnd == 0 && llSize >= 0 ? FE_OK : FE_PROTOCOL;
    }
    if (m_nCode == 550)
        return FE_OK;
    if (m_nCode == 500 || m_nCode == 502 || m_nCode == 504)
    {
        bSupported = false;
        return FE_OK;
    }
    return MapError();
}

int CFtpClient::Rename(const CString& strFrom, const CString& strTo)
{
    int nError = Command(L"RNFR " + strFrom);
    if (nError)
        return nError;
    if ((nError = Expect(350)) != FE_OK)
        return nError;
    nError = Command(L"RNTO " + strTo);
    return nError ? nError : Expect(250);
}

int CFtpClient::Delete(const CString& strName)
{
    int nError = Command(L"DELE " + strName);
    return nError ? nError : m_nCode == 550 ? FE_OK : Expect(250);
}

// 550 不能直接证明已删除，进一步核对目录清单中的精确文件名。
int CFtpClient::DeleteTemporary(const CString& strPath)
{
    int nError = Delete(strPath);
    if (nError || m_nCode != 550)
        return nError;
    int nSlash = strPath.ReverseFind(L'/');
    CString strName = strPath.Mid(nSlash + 1);
    CString strDirectory = strPath.Left(nSlash + 1);
    CFtpSocket ftpData;
    SOCKET sockListener = INVALID_SOCKET;
    nError = DataConnection(ftpData, sockListener);
    if (!nError)
    {
        nError = Command(L"NLST " + strDirectory);
        if (!nError)
            nError = Expect(125, 150);
    }
    if (!nError && !m_stConfig.m_bPassive)
        nError = ftpData.Accept(sockListener, m_stPeer, m_stConfig.m_unConnTimeout * 1000);
    if (sockListener != INVALID_SOCKET)
        closesocket(sockListener);
    if (nError)
        return nError;
    CStringA strListing;
    for (;;)
    {
        char szBytes[4096];
        int nCount = 0;
        nError = ftpData.Receive(szBytes, sizeof(szBytes), nCount, m_stConfig.m_unIoTimeout * 1000);
        if (nError == FE_CLOSED)
            break;
        if (nError)
            return nError;
        strListing.Append(szBytes, nCount);
        if (strListing.GetLength() > 1024 * 1024)
            return FE_PROTOCOL;
    }
    nError = ReadReply();
    if (nError || (nError = Expect(226, 250)) != FE_OK)
        return nError;
    CString strFiles = FromBytes(strListing, m_unCodePage);
    int nStart = 0;
    CString strFile;
    while (!(strFile = strFiles.Tokenize(L"\r\n", nStart)).IsEmpty())
        if (strFile.Mid(strFile.ReverseFind(L'/') + 1) == strName)
            return FE_FILE_PERM;
    return FE_OK;
}

// PASV 由客户端连接服务器端口，PORT 则监听并等待服务器连接。
int CFtpClient::DataConnection(CFtpSocket& ftpSocketData, SOCKET& sockListener)
{
    ftpSocketData.SetStop(m_hStop);
    sockListener = INVALID_SOCKET;
    if (m_stConfig.m_bPassive)
    {
        int nError = Command(L"PASV");
        if (nError)
            return nError;
        if ((nError = Expect(227)) != FE_OK)
            return nError;
        LPCWSTR pszP = (LPCWSTR)m_strReply + 4;
        int nH1 = 0;
        int nH2 = 0;
        int nH3 = 0;
        int nH4 = 0;
        int nA = 0;
        int nB = 0;
        bool bFound = false;
        for (; *pszP; ++pszP)
            if (iswdigit(*pszP) && swscanf_s(pszP, L"%d,%d,%d,%d,%d,%d", &nH1, &nH2, &nH3, &nH4, &nA, &nB) == 6)
            {
                bFound = true;
                break;
            }
        if (!bFound || nH1 < 0 || nH1 > 255 || nH2 < 0 || nH2 > 255 || nH3 < 0 || nH3 > 255 || nH4 < 0 || nH4 > 255 ||
            nA < 0 || nA > 255 || nB < 0 || nB > 255 || nA * 256 + nB == 0)
            return FE_PROTOCOL;
        // 被动数据连接沿用控制连接的服务器地址，只采用 PASV 返回的端口。
        sockaddr_in stAddress = m_stPeer;
        stAddress.sin_port = htons((u_short)(nA * 256 + nB));
        return ftpSocketData.Connect(stAddress, m_stConfig.m_unConnTimeout * 1000);
    }
    sockaddr_in stLocal;
    int nLength = sizeof(stLocal);
    if (getsockname(m_ftpSocketControl.Get(), (sockaddr*)&stLocal, &nLength))
        return FE_SOCKET;
    stLocal.sin_port = 0;
    sockListener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockListener == INVALID_SOCKET)
        return FE_SOCKET;
    if (bind(sockListener, (sockaddr*)&stLocal, sizeof(stLocal)) || listen(sockListener, 1) ||
        getsockname(sockListener, (sockaddr*)&stLocal, &nLength))
        return FE_SOCKET;
    u_long stNb = 1;
    ioctlsocket(sockListener, FIONBIO, &stNb);
    BYTE* pIp = (BYTE*)&stLocal.sin_addr;
    UINT unPort = ntohs(stLocal.sin_port);
    CString strCommand;
    strCommand.Format(L"PORT %u,%u,%u,%u,%u,%u", pIp[0], pIp[1], pIp[2], pIp[3], unPort / 256, unPort % 256);
    int nError = Command(strCommand);
    return nError ? nError : Expect(200);
}

// 分块读取本地文件并发送，关闭数据发送方向后等待最终完成应答。
int CFtpClient::Upload(HANDLE hFile, const CString& strName, IFtpProgress* pProgress)
{
    CFtpSocket ftpSocketData;
    SOCKET sockListener = INVALID_SOCKET;
    int nError = DataConnection(ftpSocketData, sockListener);
    if (!nError)
    {
        nError = Command(L"STOR " + strName);
        if (!nError)
            nError = Expect(125, 150);
    }
    if (!nError && !m_stConfig.m_bPassive)
        nError = ftpSocketData.Accept(sockListener, m_stPeer, m_stConfig.m_unConnTimeout * 1000);
    if (sockListener != INVALID_SOCKET)
        closesocket(sockListener);
    if (nError)
        return nError;
    std::vector<char> vecBytes(65536);
    ULONGLONG ullSent = 0;
    for (;;)
    {
        if (Stopped(m_hStop))
        {
            nError = FE_STOPPED;
            break;
        }
        DWORD dwRead = 0;
        // 本地读取失败按单文件失败计数，不进入网络重连；下方断开后由工作线程重新连接。
        if (!ReadFile(hFile, &vecBytes[0], (DWORD)vecBytes.size(), &dwRead, NULL))
        {
            nError = FE_LOCAL_READ;
            break;
        }
        if (dwRead == 0)
            break;
        nError = ftpSocketData.SendAll(&vecBytes[0], dwRead, m_stConfig.m_unIoTimeout * 1000);
        if (nError)
            break;
        ullSent += dwRead;
        if (pProgress)
            pProgress->Progress(ullSent);
    }
    if (nError)
    {
        Disconnect();
        return nError;
    }
    ftpSocketData.ShutdownSend();
    nError = ReadReply();
    ftpSocketData.Close(false);
    if (!nError)
        nError = Expect(226, 250);
    if (nError)
        // 426 后可能还残留 226 应答，断开旧会话以免下一条命令读错应答。
        Disconnect();
    return nError;
}
