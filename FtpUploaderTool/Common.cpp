#include "stdafx.h"
#include "Common.h"

CString ExeDir() {
    WCHAR szBuffer[32768] = {0}; GetModuleFileNameW(NULL, szBuffer, _countof(szBuffer));
    CString strPath(szBuffer); return strPath.Left(strPath.ReverseFind(L'\\'));
}
CString JoinPath(const CString& strA, const CString& strB) {
    if (strA.IsEmpty()) return strB;
    return strA + (strA.Right(1) == L"\\" ? L"" : L"\\") + strB;
}
CString FullPath(const CString& strPath) {
    WCHAR szBuffer[32768] = {0};
    DWORD dwN = GetFullPathNameW(strPath, _countof(szBuffer), szBuffer, NULL);
    if (!dwN || dwN >= _countof(szBuffer)) return CString();
    CString strResult(szBuffer); strResult.TrimRight(L"\\/"); return strResult;
}
bool IsWithin(const CString& strParent, const CString& strChild) {
    CString strA = FullPath(strParent), strB = FullPath(strChild);
    if (strA.IsEmpty() || strB.IsEmpty()) return false;
    return strA.CompareNoCase(strB) == 0 || (strB.GetLength() > strA.GetLength() &&
        strB.Left(strA.GetLength()).CompareNoCase(strA) == 0 && strB[strA.GetLength()] == L'\\');
}
bool MakeDirs(const CString& strPath) {
    int nError = SHCreateDirectoryExW(NULL, strPath, NULL);
    return nError == ERROR_SUCCESS || nError == ERROR_ALREADY_EXISTS || nError == ERROR_FILE_EXISTS;
}
CString NewId() {
    GUID stId; CoCreateGuid(&stId); WCHAR szText[40] = {0}; StringFromGUID2(stId, szText, _countof(szText));
    CString strResult(szText); strResult.Trim(L"{}"); return strResult;
}
CStringA ToBytes(const CString& strText, UINT unCodePage) {
    if (strText.IsEmpty()) return CStringA();
    int nResult = WideCharToMultiByte(unCodePage, 0, strText, strText.GetLength(), NULL, 0, NULL, NULL);
    CStringA strAOutput; char* pBuffer = strAOutput.GetBuffer(nResult);
    WideCharToMultiByte(unCodePage, 0, strText, strText.GetLength(), pBuffer, nResult, NULL, NULL); strAOutput.ReleaseBuffer(nResult); return strAOutput;
}
CString FromBytes(const CStringA& strABytes, UINT unCodePage) {
    if (strABytes.IsEmpty()) return CString();
    int nResult = MultiByteToWideChar(unCodePage, 0, strABytes, strABytes.GetLength(), NULL, 0);
    CString strOutput; WCHAR* pBuffer = strOutput.GetBuffer(nResult);
    MultiByteToWideChar(unCodePage, 0, strABytes, strABytes.GetLength(), pBuffer, nResult); strOutput.ReleaseBuffer(nResult); return strOutput;
}
bool WriteBytes(HANDLE hFile, const void* pData, DWORD dwSize) {
    const BYTE* pBuffer = (const BYTE*)pData;
    while (dwSize) { DWORD dwWritten = 0; if (!WriteFile(hFile, pBuffer, dwSize, &dwWritten, NULL) || !dwWritten) return false; pBuffer += dwWritten; dwSize -= dwWritten; }
    return true;
}
bool AtomicWrite(const CString& strPath, const CStringA& strAData) {
    CString strTemp = strPath + L".tmp";
    CAutoHandle hFile(CreateFileW(strTemp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (!hFile.Valid() || !WriteBytes(hFile.Get(), (LPCSTR)strAData, strAData.GetLength()) || !FlushFileBuffers(hFile.Get())) return false;
    hFile.Close();
    return MoveFileExW(strTemp, strPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
CString TimeText() { SYSTEMTIME stTime; GetLocalTime(&stTime); CString strText; strText.Format(L"%04u-%02u-%02u %02u:%02u:%02u.%03u", stTime.wYear, stTime.wMonth, stTime.wDay, stTime.wHour, stTime.wMinute, stTime.wSecond, stTime.wMilliseconds); return strText; }
CString DateText() { return TimeText().Left(10); }
CString WinError(DWORD dwError) {
    WCHAR szBuffer[512] = {0}; FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwError, 0, szBuffer, _countof(szBuffer), NULL);
    CString strText(szBuffer); strText.Trim(); CString strResult; strResult.Format(L"%s (%lu)", (LPCWSTR)strText, dwError); return strResult;
}
CString EncryptPassword(const CString& strPlain) {
    if (strPlain.IsEmpty()) return CString();
    DATA_BLOB stIn, stOut = {0}; stIn.pbData = (BYTE*)(LPCWSTR)strPlain; stIn.cbData = (strPlain.GetLength() + 1) * sizeof(WCHAR);
    if (!CryptProtectData(&stIn, L"FtpUploaderTool", NULL, NULL, NULL, CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &stOut)) return CString();
    DWORD dwLength = 0; CryptBinaryToStringW(stOut.pbData, stOut.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &dwLength);
    CString strValue; WCHAR* pBuffer = strValue.GetBuffer(dwLength);
    BOOL bOk = CryptBinaryToStringW(stOut.pbData, stOut.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, pBuffer, &dwLength);
    strValue.ReleaseBuffer(); SecureZeroMemory(stOut.pbData, stOut.cbData); LocalFree(stOut.pbData);
    return bOk ? L"ENC:" + strValue : CString();
}
bool DecryptPassword(const CString& strEncrypted, CString& strPlain) {
    strPlain.Empty(); if (strEncrypted.IsEmpty()) return true;
    if (strEncrypted.Left(4) != L"ENC:") return false;
    DWORD dwLength = 0;
    if (!CryptStringToBinaryW(strEncrypted.Mid(4), 0, CRYPT_STRING_BASE64, NULL, &dwLength, NULL, NULL)) return false;
    std::vector<BYTE> vecBuffer(dwLength);
    if (!dwLength || !CryptStringToBinaryW(strEncrypted.Mid(4), 0, CRYPT_STRING_BASE64, &vecBuffer[0], &dwLength, NULL, NULL)) return false;
    DATA_BLOB stIn = {dwLength, &vecBuffer[0]}, stOut = {0};
    if (!CryptUnprotectData(&stIn, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &stOut)) return false;
    bool bValid = stOut.cbData >= sizeof(WCHAR) && stOut.cbData % sizeof(WCHAR) == 0 && ((WCHAR*)stOut.pbData)[stOut.cbData / sizeof(WCHAR) - 1] == 0;
    if (bValid) strPlain = (WCHAR*)stOut.pbData;
    SecureZeroMemory(stOut.pbData, stOut.cbData); LocalFree(stOut.pbData); return bValid;
}
CString SafeFileName(const CString& strText) {
    CString strResult(strText); LPCWSTR pszBad = L"\\/:*?\"<>|\r\n";
    for (int nIndex = 0; nIndex < strResult.GetLength(); ++nIndex) if (wcschr(pszBad, strResult[nIndex])) strResult.SetAt(nIndex, L'_');
    return strResult;
}
bool DeleteTree(const CString& strPath, HANDLE hStop) {
    WIN32_FIND_DATAW stFd; HANDLE hHandle = FindFirstFileW(JoinPath(strPath, L"*"), &stFd);
    if (hHandle == INVALID_HANDLE_VALUE) return false;
    CFindHandle hFindFind(hHandle);
    do {
        if (Stopped(hStop)) return false;
        if (wcscmp(stFd.cFileName, L".") == 0 || wcscmp(stFd.cFileName, L"..") == 0) continue;
        if (stFd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
        CString strChild = JoinPath(strPath, stFd.cFileName);
        if (stFd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!DeleteTree(strChild, hStop)) return false; }
        else if (!DeleteFileW(strChild)) return false;
    } while (FindNextFileW(hHandle, &stFd));
    return RemoveDirectoryW(strPath) != FALSE;
}
