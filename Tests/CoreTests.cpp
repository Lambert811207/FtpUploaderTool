#include "stdafx.h"
#include "Worker.h"
#include "TaskEditDlg.h"
#include "FtpUploaderToolDlg.h"
#include <iostream>

static CWinApp m_appTest;
static void Check(bool bOk, const char* pMessage) { if (!bOk) throw std::runtime_error(pMessage); }
static void PutFile(const CString& strPath, const CStringA& strAContent) {
    CAutoHandle hFile(CreateFileW(strPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    Check(hFile.Valid() && WriteBytes(hFile.Get(), (LPCSTR)strAContent, strAContent.GetLength()) && FlushFileBuffers(hFile.Get()), "write test file");
}
static bool WaitOk(CTaskWorker& workerTask, UINT unCount, DWORD dwTimeout = 12000) {
    DWORD dwStart = GetTickCount(); while (!TickElapsed(dwStart, dwTimeout)) { if (workerTask.Status().m_unTodayOk >= unCount) return true; Sleep(50); } return false;
}

class CTestServer {
public:
    CTestServer() : m_sockListener(INVALID_SOCKET), m_hThread(NULL), m_hStop(CreateEvent(NULL, TRUE, FALSE, NULL)), m_unPort(0), m_lStores(0), m_lDrop(0), m_lReject(0), m_bUtf8(true), m_bBadSize(false), m_bNoSize(false) {
        m_sockListener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); sockaddr_in stAddress = {0}; stAddress.sin_family = AF_INET; stAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(m_sockListener, (sockaddr*)&stAddress, sizeof(stAddress)) == 0 && listen(m_sockListener, 8) == 0, "fixture listen");
        int nSize = sizeof(stAddress); getsockname(m_sockListener, (sockaddr*)&stAddress, &nSize); m_unPort = ntohs(stAddress.sin_port); m_setDirs.insert(L"/");
        m_hThread = (HANDLE)_beginthreadex(NULL, 0, Thread, this, 0, NULL); Check(m_hThread != NULL, "fixture thread");
    }
    ~CTestServer() { SetEvent(m_hStop); WaitForSingleObject(m_hThread, 5000); CloseHandle(m_hThread); closesocket(m_sockListener); CloseHandle(m_hStop); }
    UINT Port() const { return m_unPort; }
    void DropNext() { InterlockedExchange(&m_lDrop, 1); }
    void RejectNext() { InterlockedExchange(&m_lReject, 1); }
    void Gbk() { m_bUtf8 = false; }
    void BadSize() { m_bBadSize = true; }
    void NoSize() { m_bNoSize = true; }
    LONG Stores() const { return InterlockedCompareExchange((LONG*)&m_lStores, 0, 0); }
    CStringA Content(const CString& strPath) { CAutoLock lockGuard(m_csLock); return m_mapFiles[strPath]; }
    CStringA LastBody() { CAutoLock lockGuard(m_csLock); return m_strALastBody; }
    bool Exists(const CString& strPath) { CAutoLock lockGuard(m_csLock); return m_mapFiles.count(strPath) != 0; }
private:
    bool Ready(SOCKET sockS, bool bWrite = false) {
        while (!Stopped(m_hStop)) { fd_set stFds; FD_ZERO(&stFds); FD_SET(sockS, &stFds); timeval stTv = {0, 100000}; int nResult = select(0, bWrite ? NULL : &stFds, bWrite ? &stFds : NULL, NULL, &stTv); if (nResult != 0) return nResult > 0; } return false;
    }
    bool Send(SOCKET sockS, const CStringA& strAText) { CStringA strABytes = strAText + "\r\n"; int nOffset = 0; while (nOffset < strABytes.GetLength()) { if (!Ready(sockS, true)) return false; int nResult = send(sockS, (LPCSTR)strABytes + nOffset, strABytes.GetLength() - nOffset, 0); if (nResult <= 0) return false; nOffset += nResult; } return true; }
    bool Line(SOCKET sockS, CStringA& strALine) { strALine.Empty(); while (strALine.GetLength() < 65536 && Ready(sockS)) { char chByte; int nResult = recv(sockS, &chByte, 1, 0); if (nResult <= 0) return false; if (chByte == '\n') { if(!strALine.IsEmpty() && strALine[strALine.GetLength()-1]=='\r') strALine.Truncate(strALine.GetLength()-1); return true; } strALine += chByte; } return false; }
    CString Path(const CString& strCwd, const CString& strName) { return strName.Left(1) == L"/" ? strName : (strCwd == L"/" ? CString(L"/") : strCwd + L"/") + strName; }
    static unsigned __stdcall Thread(void* pBuffer) { ((CTestServer*)pBuffer)->Run(); return 0; }
    void Run() { while (Ready(m_sockListener)) { SOCKET sockControl = accept(m_sockListener, NULL, NULL); if (sockControl == INVALID_SOCKET) break; Session(sockControl); closesocket(sockControl); } }
    void Session(SOCKET sockControl) {
        SOCKET sockPassive = INVALID_SOCKET; sockaddr_in stActive = {0}; bool bBinary = false; CString strCwd(L"/"), strRename; UINT unCodePage = m_bUtf8 ? CP_UTF8 : 936;
        Send(sockControl, "220-Multiline welcome\r\nfixture text\r\n220 Ready"); CStringA strALine;
        while (Line(sockControl, strALine)) {
            int nSpace = strALine.Find(' '); CStringA strACommand = nSpace < 0 ? strALine : strALine.Left(nSpace); CString strArgument = nSpace < 0 ? CString() : FromBytes(strALine.Mid(nSpace + 1), unCodePage);
            if (strACommand == "USER") Send(sockControl, "331 Password");
            else if (strACommand == "PASS") Send(sockControl, "230 Logged in");
            else if (strACommand == "FEAT") Send(sockControl, m_bUtf8 ? "211-Features\r\n UTF8\r\n SIZE\r\n211 End" : "211-Features\r\n SIZE\r\n211 End");
            else if (strACommand == "OPTS") Send(sockControl, "200 UTF8");
            else if (strACommand == "TYPE") { bBinary = strArgument == L"I"; Send(sockControl, "200 Binary"); }
            else if (strACommand == "NOOP") Send(sockControl, "200 Alive");
            else if (strACommand == "CWD") { CString strPath = Path(strCwd, strArgument); if (m_setDirs.count(strPath)) { strCwd = strPath; Send(sockControl, "250 Directory"); } else Send(sockControl, "550 Missing"); }
            else if (strACommand == "MKD") { m_setDirs.insert(Path(strCwd, strArgument)); Send(sockControl, "257 Created"); }
            else if (strACommand == "SIZE") {
                CString strPath = Path(strCwd, strArgument); CAutoLock lockGuard(m_csLock); CStringA strAReply;
                if (m_bNoSize) Send(sockControl, "502 Unsupported");
                else if (m_mapFiles.count(strPath)) { strAReply.Format("213 %d", m_mapFiles[strPath].GetLength() + (m_bBadSize && strArgument.Right(10) == L".uploading" ? 1 : 0)); Send(sockControl, strAReply); }
                else Send(sockControl, "550 Missing");
            } else if (strACommand == "PASV") {
                if (sockPassive != INVALID_SOCKET) closesocket(sockPassive); sockPassive = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                sockaddr_in stAddress = {0}; stAddress.sin_family = AF_INET; stAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                bind(sockPassive, (sockaddr*)&stAddress, sizeof(stAddress)); listen(sockPassive, 1); int nSize = sizeof(stAddress); getsockname(sockPassive, (sockaddr*)&stAddress, &nSize); UINT unPort = ntohs(stAddress.sin_port);
                CStringA strAReply; strAReply.Format("227 Passive (10,99,88,77,%u,%u)", unPort / 256, unPort % 256); Send(sockControl, strAReply);
            } else if (strACommand == "PORT") {
                unsigned unA,unB,unC,unD,unP,unQ; CStringA strAArg = ToBytes(strArgument); sscanf_s(strAArg, "%u,%u,%u,%u,%u,%u", &unA,&unB,&unC,&unD,&unP,&unQ);
                stActive.sin_family = AF_INET; stActive.sin_addr.s_addr = htonl((unA<<24)|(unB<<16)|(unC<<8)|unD); stActive.sin_port = htons((u_short)(unP*256+unQ)); Send(sockControl, "200 PORT");
            } else if (strACommand == "STOR") {
                if (!bBinary || InterlockedExchange(&m_lReject, 0)) { Send(sockControl, "550 Denied"); continue; }
                Send(sockControl, "150 Data"); SOCKET sockData = INVALID_SOCKET;
                if (sockPassive != INVALID_SOCKET) { if (!Ready(sockPassive)) break; sockData = accept(sockPassive, NULL, NULL); closesocket(sockPassive); sockPassive = INVALID_SOCKET; }
                else { sockData = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (connect(sockData, (sockaddr*)&stActive, sizeof(stActive))) { closesocket(sockData); break; } }
                if (sockData == INVALID_SOCKET) break;
                if (InterlockedExchange(&m_lDrop, 0)) { linger stReset = {1,0}; setsockopt(sockData,SOL_SOCKET,SO_LINGER,(char*)&stReset,sizeof(stReset)); closesocket(sockData); break; }
                std::vector<char> vecReceived; char szBytes[65536]; bool bComplete = false;
                for (;;) { int nResult = recv(sockData, szBytes, sizeof(szBytes), 0); if (nResult == 0) { bComplete = true; break; } if (nResult < 0) break; vecReceived.insert(vecReceived.end(),szBytes,szBytes+nResult); }
                closesocket(sockData); if (!bComplete) break;
                CStringA strABody; if (!vecReceived.empty()) { char* pTarget=strABody.GetBuffer((int)vecReceived.size()); memcpy(pTarget,&vecReceived[0],vecReceived.size()); strABody.ReleaseBufferSetLength((int)vecReceived.size()); }
                { CAutoLock lockGuard(m_csLock); m_mapFiles[Path(strCwd,strArgument)] = strABody; m_strALastBody = strABody; } InterlockedIncrement(&m_lStores); Send(sockControl,"226 Finished");
            } else if (strACommand == "RNFR") { strRename = Path(strCwd,strArgument); Send(sockControl,"350 Rename"); }
            else if (strACommand == "RNTO") { CAutoLock lockGuard(m_csLock); if (m_mapFiles.count(strRename)) { m_mapFiles[Path(strCwd,strArgument)] = m_mapFiles[strRename]; m_mapFiles.erase(strRename); Send(sockControl,"250 Renamed"); } else Send(sockControl,"550 Missing"); }
            else if (strACommand == "DELE") { CAutoLock lockGuard(m_csLock); m_mapFiles.erase(Path(strCwd,strArgument)); Send(sockControl,"250 Deleted"); }
            else Send(sockControl,"502 Unsupported");
        }
        if (sockPassive != INVALID_SOCKET) closesocket(sockPassive);
    }
    SOCKET m_sockListener; HANDLE m_hThread, m_hStop; UINT m_unPort; CCritSec m_csLock;
    std::map<CString,CStringA> m_mapFiles; std::set<CString> m_setDirs; CStringA m_strALastBody; volatile LONG m_lStores,m_lDrop,m_lReject;
    bool m_bUtf8,m_bBadSize,m_bNoSize;
};
static TaskConfig Config(const CString& strRoot, CTestServer& serverFtp) {
    TaskConfig stConfig; stConfig.m_strLocalDir = JoinPath(strRoot,L"Inbox"); MakeDirs(stConfig.m_strLocalDir); stConfig.m_strName=L"一号线"; stConfig.m_unPort=serverFtp.Port(); stConfig.m_strRemoteDir=L"/";
    stConfig.m_unScanInterval=stConfig.m_unStableSec=stConfig.m_unReconnMin=1; stConfig.m_unReconnMax=2; stConfig.m_unConnTimeout=stConfig.m_unIoTimeout=2; stConfig.m_unMaxFileRetry=1; return stConfig;
}
static void StopWorker(CTaskWorker& workerTask) { DWORD dwStart=GetTickCount(); workerTask.RequestStop(); while (!workerTask.PollStopped() && !TickElapsed(dwStart,3000)) Sleep(20); Check(!workerTask.Running(),"worker stop deadline"); }
int _tmain(int, TCHAR**, TCHAR**) {
    int nPassed=0; CString strRoot;
    try {
        Check(!!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLineW(),0),"AfxWinInit"); InitCommonControls(); CoInitialize(NULL); WSADATA stData; Check(WSAStartup(MAKEWORD(2,2),&stData)==0,"WSAStartup");
        WCHAR szTemp[MAX_PATH]; GetTempPathW(_countof(szTemp),szTemp); strRoot=JoinPath(szTemp,L"FtpUploaderToolTests_"+NewId()); MakeDirs(strRoot);
        CTestServer serverFtp; TaskConfig stConfig=Config(strRoot,serverFtp);
        {
            CString strPath=JoinPath(strRoot,L"Config\\FtpUploaderTool.ini"), strMessage; GlobalConfig stGlobal; std::vector<TaskConfig> vecList; stConfig.m_strPassword=L"中文密码!"; vecList.push_back(stConfig); CConfigStore storeConfig;
            Check(storeConfig.Save(strPath,stGlobal,vecList,strMessage),"config save"); vecList.clear(); Check(storeConfig.Load(strPath,stGlobal,vecList,strMessage)&&vecList.size()==1&&vecList[0].m_strPassword==stConfig.m_strPassword,"config Chinese/DPAPI roundtrip");
            Check(vecList[0].m_strName==stConfig.m_strName,"Chinese task name");
            WCHAR szValue[64]; CString strSection = L"Task_" + stConfig.m_strId;
            GetPrivateProfileStringW(L"Global",L"TaskCount",L"",szValue,_countof(szValue),strPath); Check(wcscmp(szValue,L"1")==0,"task count key");
            GetPrivateProfileStringW(strSection,L"Enabled",L"missing",szValue,_countof(szValue),strPath); Check(wcscmp(szValue,L"missing")==0,"enabled key removed");
            ++nPassed; std::cout<<"PASS config / DPAPI / Unicode / task schema"<<std::endl;
        }
        {
            CString strLegacyPath = JoinPath(strRoot,L"legacy.ini"), strSavedPath = JoinPath(strRoot,L"migrated.ini"), strMessage;
            CString strLegacy = L"[Global]\r\nVersion=1\r\nChannelCount=1\r\nChannel0=" + stConfig.m_strId + L"\r\n[Ch_" + stConfig.m_strId + L"]\r\nName=旧任务\r\nEnabled=0\r\nHost=127.0.0.1\r\nLocalDir=" + stConfig.m_strLocalDir + L"\r\nPassword=" + EncryptPassword(stConfig.m_strPassword) + L"\r\n";
            { CAutoHandle hLegacy(CreateFileW(strLegacyPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL)); WORD wBom=0xFEFF;
              Check(hLegacy.Valid() && WriteBytes(hLegacy.Get(),&wBom,2) && WriteBytes(hLegacy.Get(),(LPCWSTR)strLegacy,strLegacy.GetLength()*sizeof(WCHAR)),"legacy fixture"); }
            GlobalConfig stGlobal; std::vector<TaskConfig> vecConfigs; CConfigStore storeConfig;
            Check(storeConfig.Load(strLegacyPath,stGlobal,vecConfigs,strMessage) && vecConfigs.size()==1,"legacy configuration loaded");
            Check(vecConfigs[0].m_strId==stConfig.m_strId && vecConfigs[0].m_strPassword==stConfig.m_strPassword && vecConfigs[0].m_strName==L"旧任务","legacy task and credentials retained");
            Check(storeConfig.Save(strSavedPath,stGlobal,vecConfigs,strMessage),"legacy saved in task format");
            WCHAR szValue[64]; GetPrivateProfileStringW(L"Global",L"ChannelCount",L"missing",szValue,_countof(szValue),strSavedPath); Check(wcscmp(szValue,L"missing")==0,"old schema not saved");
            ++nPassed; std::cout<<"PASS legacy configuration compatibility"<<std::endl;
        }
        {
            CTaskEditDlg dlgDialog(stConfig,NULL); Check(!!dlgDialog.Create(IDD_DLG_TASK),"create MFC connection dialog");
            CString strName; dlgDialog.GetDlgItemText(IDC_EDIT_NAME,strName); Check(strName==stConfig.m_strName && dlgDialog.GetDlgItem(IDC_BTN_TEST)!=NULL,"MFC controls initialized");
            Check(dlgDialog.GetDlgItem(1101)==NULL,"task enabled option removed"); CString strTitle; dlgDialog.GetWindowText(strTitle); Check(strTitle==L"任务配置","task dialog title"); dlgDialog.DestroyWindow();
            CSettingsDlg dlgSettings(GlobalConfig(),NULL); Check(!!dlgSettings.Create(IDD_DLG_SETTINGS),"create MFC settings dialog"); dlgSettings.DestroyWindow(); ++nPassed; std::cout<<"PASS MFC dialog resources / controls"<<std::endl;
        }
        {
            CString strLocal=JoinPath(stConfig.m_strLocalDir,L"稳定.csv"); PutFile(strLocal,"payload"); PendingFile stFile; Check(StatFile(stConfig.m_strLocalDir,strLocal,stFile),"stat"); CUploadHistory historyUpload; historyUpload.Load(JoinPath(strRoot,L"history.txt"));
            CDirScanner scannerDirectory(stConfig); std::deque<PendingFile> deqQueue; std::map<CString,FailInfo> mapFails; std::map<CString,PendingFile> mapPost;
            CAutoHandle hWriting(CreateFileW(strLocal,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL)); scannerDirectory.Scan(deqQueue,historyUpload,mapFails,mapPost,NULL); Sleep(1100); scannerDirectory.Scan(deqQueue,historyUpload,mapFails,mapPost,NULL); Check(deqQueue.empty(),"write lock stability gate");
            hWriting.Close(); scannerDirectory.Scan(deqQueue,historyUpload,mapFails,mapPost,NULL); Check(deqQueue.size()==1,"stable queue"); Check(historyUpload.Add(stFile),"history append"); CUploadHistory historyLoaded; Check(historyLoaded.Load(JoinPath(strRoot,L"history.txt"))&&historyLoaded.Contains(stFile),"restart dedup");
            DeleteFileW(strLocal); ++nPassed; std::cout<<"PASS stability / write lock / persistent history"<<std::endl;
        }
        {
            CAutoHandle hCancel(CreateEvent(NULL,TRUE,FALSE,NULL)); CFtpClient ftpClientSession(stConfig,hCancel.Get()); Check(ftpClientSession.Connect()==0,"FTP connect multiline"); Check(ftpClientSession.EnsureDirectory(L"/测试/子目录")==0,"MKD/CWD Chinese");
            CString strPath=JoinPath(strRoot,L"wire.bin"); std::vector<char> vecWire(200000); for(int nResult=0;nResult<200000;++nResult) vecWire[nResult]=(char)(nResult%251); CStringA strABytes(&vecWire[0], (int)vecWire.size()); PutFile(strPath,strABytes);
            CAutoHandle hInput(CreateFileW(strPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL)); Check(ftpClientSession.Upload(hInput.Get(),L"文件.csv.uploading",NULL)==0,"PASV upload");
            LONGLONG llSize; bool bSupported; Check(ftpClientSession.Size(L"文件.csv.uploading",llSize,bSupported)==FE_OK && llSize==strABytes.GetLength(),"SIZE"); Check(ftpClientSession.Rename(L"文件.csv.uploading",L"文件.csv")==0,"RNFR RNTO");
            CStringA strAActual = serverFtp.LastBody(); Check(strAActual.GetLength()==strABytes.GetLength() && memcmp((LPCSTR)strAActual,(LPCSTR)strABytes,strABytes.GetLength())==0,"binary content exact"); Check(serverFtp.Exists(L"/测试/子目录/文件.csv"),"Chinese exact remote filename"); ++nPassed; std::cout<<"PASS FTP PASV / NAT / UTF8 / atomic rename / bytes"<<std::endl;
        }
        {
            CTestServer serverActive; serverActive.Gbk(); TaskConfig stCfg=Config(JoinPath(strRoot,L"active"),serverActive); stCfg.m_bPassive=false; stCfg.m_nEncoding=ENC_GBK;
            CAutoHandle hCancel(CreateEvent(NULL,TRUE,FALSE,NULL)); CFtpClient ftpClientSession(stCfg,hCancel.Get()); Check(ftpClientSession.Connect()==0&&ftpClientSession.EnsureDirectory(L"/中文")==0,"GBK login");
            CString strPath=JoinPath(strRoot,L"empty"); PutFile(strPath,""); CAutoHandle hInput(CreateFileW(strPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL)); Check(ftpClientSession.Upload(hInput.Get(),L"空文件.csv",NULL)==0,"PORT empty upload");
            Check(serverActive.Exists(L"/中文/空文件.csv"),"PORT GBK filename"); ++nPassed; std::cout<<"PASS FTP PORT / GBK / empty file"<<std::endl;
        }
        {
            CString strRun=JoinPath(strRoot,L"worker"); TaskConfig stCfg=Config(strRun,serverFtp); stCfg.m_strRemoteDir=L"/worker"; PutFile(JoinPath(stCfg.m_strLocalDir,L"keep.csv"),"keep content"); CLogger loggerFile; loggerFile.Start(JoinPath(strRun,L"Log"),30);
            { CTaskWorker workerTask(stCfg,strRun,loggerFile,30); Check(workerTask.Start()&&WaitOk(workerTask,1),"worker keep upload"); StopWorker(workerTask); }
            LONG lStores=serverFtp.Stores(); { CTaskWorker workerTask(stCfg,strRun,loggerFile,30); workerTask.Start(); Sleep(2400); StopWorker(workerTask); Check(serverFtp.Stores()==lStores,"worker restart duplicated upload"); }
            loggerFile.Stop(); ++nPassed; std::cout<<"PASS worker / keep / restart dedup / stop"<<std::endl;
        }
        {
            for(int nAction=POST_DELETE;nAction<=POST_MOVE;++nAction) {
                CString strPart; strPart.Format(L"post%d",nAction); CString strRun=JoinPath(strRoot,strPart); TaskConfig stCfg=Config(strRun,serverFtp); stCfg.m_strRemoteDir=L"/"+strPart; stCfg.m_nPostAction=nAction; stCfg.m_strBackupDir=JoinPath(strRun,L"Backup");
                CString strLocal=JoinPath(stCfg.m_strLocalDir,L"result.csv"); PutFile(strLocal,"result"); PendingFile stInfo; StatFile(stCfg.m_strLocalDir,strLocal,stInfo); CLogger loggerFile; loggerFile.Start(JoinPath(strRun,L"Log"),30); CTaskWorker workerTask(stCfg,strRun,loggerFile,30);
                Check(workerTask.Start()&&WaitOk(workerTask,1),"post upload"); DWORD dwStart=GetTickCount(); while(PathFileExistsW(strLocal)&&!TickElapsed(dwStart,3000)) Sleep(30); StopWorker(workerTask); Check(PathFileExistsW(strLocal)==FALSE,"post local removal");
                if(nAction==POST_MOVE) Check(!!PathFileExistsW(JoinPath(JoinPath(stCfg.m_strBackupDir,FileDate(stInfo.m_ullModified,true)),stInfo.m_strName)),"backup missing"); loggerFile.Stop();
            } ++nPassed; std::cout<<"PASS delete / backup post processing"<<std::endl;
        }
        {
            CTestServer serverRecovering; serverRecovering.DropNext(); CString strRun=JoinPath(strRoot,L"recovery"); TaskConfig stCfg=Config(strRun,serverRecovering); std::vector<char> vecRecoveryBytes(2*1024*1024); for(int nResult=0;nResult<(int)vecRecoveryBytes.size();++nResult) vecRecoveryBytes[nResult]=(char)(nResult%249+1); CStringA strABody(&vecRecoveryBytes[0],(int)vecRecoveryBytes.size()); PutFile(JoinPath(stCfg.m_strLocalDir,L"recover.bin"),strABody);
            CLogger loggerFile; loggerFile.Start(JoinPath(strRun,L"Log"),30); CTaskWorker workerTask(stCfg,strRun,loggerFile,30); Check(workerTask.Start()&&WaitOk(workerTask,1),"disconnect recovery"); StopWorker(workerTask); Check(workerTask.Status().m_unReconnects>=1&&serverRecovering.Content(L"/recover.bin")==strABody,"recovery bytes"); loggerFile.Stop(); ++nPassed; std::cout<<"PASS transfer interruption / reconnect / complete retry"<<std::endl;
        }
        {
            CTestServer serverFailing; serverFailing.BadSize(); CString strRun=JoinPath(strRoot,L"verify"); TaskConfig stCfg=Config(strRun,serverFailing); stCfg.m_nPostAction=POST_DELETE; CString strLocal=JoinPath(stCfg.m_strLocalDir,L"bad.csv"); PutFile(strLocal,"bad size");
            CLogger loggerFile; loggerFile.Start(JoinPath(strRun,L"Log"),30); CTaskWorker workerTask(stCfg,strRun,loggerFile,30); workerTask.Start(); DWORD dwStart=GetTickCount(); while(workerTask.Status().m_unParked==0&&!TickElapsed(dwStart,6000))Sleep(30); StopWorker(workerTask);
            Check(workerTask.Status().m_unParked==1&&PathFileExistsW(strLocal)&&!serverFailing.Exists(L"/bad.csv"),"verify must preserve local and prevent final name"); loggerFile.Stop(); ++nPassed; std::cout<<"PASS verify failure preserves source / parks file"<<std::endl;
        }
        {
            CTestServer serverRejecting; serverRejecting.RejectNext(); CString strRun=JoinPath(strRoot,L"permission"); TaskConfig stCfg=Config(strRun,serverRejecting); PutFile(JoinPath(stCfg.m_strLocalDir,L"retry.csv"),"retry");
            CLogger loggerFile; loggerFile.Start(JoinPath(strRun,L"Log"),30); CTaskWorker workerTask(stCfg,strRun,loggerFile,30); workerTask.Start(); DWORD dwStart=GetTickCount(); while(workerTask.Status().m_unParked==0&&!TickElapsed(dwStart,6000))Sleep(30); Check(workerTask.Status().m_unParked==1,"permission should park"); StopWorker(workerTask); Check(workerTask.Start() && WaitOk(workerTask,1),"restart parked task"); StopWorker(workerTask); loggerFile.Stop(); ++nPassed; std::cout<<"PASS permission error / restart task"<<std::endl;
        }
        {
            CFtpUploaderToolDlg dlgMain; Check(!!dlgMain.Create(IDD_DLG_MAIN), "main MFC dialog creation");
            Check(dlgMain.GetDlgItem(IDC_LIST_TASKS)!=NULL && dlgMain.GetDlgItem(IDC_LIST_LOGS)!=NULL, "main controls");
            Check(dlgMain.GetDlgItem(1012)==NULL && dlgMain.GetDlgItem(1013)==NULL && dlgMain.GetDlgItem(1018)==NULL,"copy delete retry controls removed");
            CListCtrl* pTasks = (CListCtrl*)dlgMain.GetDlgItem(IDC_LIST_TASKS); Check((pTasks->GetExtendedStyle() & LVS_EX_CHECKBOXES)==0,"task enabled checkbox removed");
            dlgMain.SetWindowPos(NULL,0,0,1100,750,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            DWORD dwStart=GetTickCount(); while(!TickElapsed(dwStart,600)) { MSG stMessage; while(PeekMessage(&stMessage,NULL,0,0,PM_REMOVE)) { TranslateMessage(&stMessage); DispatchMessage(&stMessage); } Sleep(10); }
            Check(IsWindow(dlgMain.GetSafeHwnd())!=FALSE,"main survives initialization and timers"); dlgMain.DestroyWindow(); ++nPassed; std::cout<<"PASS main MFC lifecycle / resize / timers"<<std::endl;
        }
        std::cout<<"ALL PASSED: "<<nPassed<<std::endl;
        Check(IsWithin(CString(szTemp),strRoot),"cleanup path validation"); DeleteTree(strRoot,NULL); WSACleanup(); CoUninitialize(); return 0;
    } catch(const std::exception& stError) { std::cerr<<"FAIL after "<<nPassed<<": "<<stError.what()<<std::endl; std::wcerr<<(LPCWSTR)strRoot<<std::endl; return 1; }
    catch(CException* pError) { pError->Delete(); std::cerr<<"MFC exception"<<std::endl; return 1; }
}
