#include "stdafx.h"
#include "Common.h"

CString ExeDir()
{
    WCHAR szBuffer[32768] = {0};
    GetModuleFileNameW(NULL, szBuffer, _countof(szBuffer));
    CString strPath(szBuffer);
    return strPath.Left(strPath.ReverseFind(L'\\'));
}

CString JoinPath(const CString& strA, const CString& strB)
{
    if (strA.IsEmpty())
        return strB;
    return strA + (strA.Right(1) == L"\\" ? L"" : L"\\") + strB;
}

CString FullPath(const CString& strPath)
{
    WCHAR szBuffer[32768] = {0};
    DWORD dwN = GetFullPathNameW(strPath, _countof(szBuffer), szBuffer, NULL);
    if (!dwN || dwN >= _countof(szBuffer))
        return CString();
    CString strResult(szBuffer);
    strResult.TrimRight(L"\\/");
    return strResult;
}

// 补上目录分隔边界，避免把同前缀的相邻目录误认为父子目录。
bool IsWithin(const CString& strParent, const CString& strChild)
{
    CString strA = FullPath(strParent);
    CString strB = FullPath(strChild);
    if (strA.IsEmpty() || strB.IsEmpty())
        return false;
    return strA.CompareNoCase(strB) == 0 ||
           (strB.GetLength() > strA.GetLength() && strB.Left(strA.GetLength()).CompareNoCase(strA) == 0 &&
            strB[strA.GetLength()] == L'\\');
}

bool MakeDirs(const CString& strPath)
{
    int nError = SHCreateDirectoryExW(NULL, strPath, NULL);
    return nError == ERROR_SUCCESS || nError == ERROR_ALREADY_EXISTS || nError == ERROR_FILE_EXISTS;
}

CString NewId()
{
    GUID stId;
    CoCreateGuid(&stId);
    WCHAR szText[40] = {0};
    StringFromGUID2(stId, szText, _countof(szText));
    CString strResult(szText);
    strResult.Trim(L"{}");
    return strResult;
}

CStringA ToBytes(const CString& strText, UINT unCodePage)
{
    if (strText.IsEmpty())
        return CStringA();
    int nResult = WideCharToMultiByte(unCodePage, 0, strText, strText.GetLength(), NULL, 0, NULL, NULL);
    CStringA strAOutput;
    char* pBuffer = strAOutput.GetBuffer(nResult);
    WideCharToMultiByte(unCodePage, 0, strText, strText.GetLength(), pBuffer, nResult, NULL, NULL);
    strAOutput.ReleaseBuffer(nResult);
    return strAOutput;
}

CString FromBytes(const CStringA& strABytes, UINT unCodePage)
{
    if (strABytes.IsEmpty())
        return CString();
    int nResult = MultiByteToWideChar(unCodePage, 0, strABytes, strABytes.GetLength(), NULL, 0);
    CString strOutput;
    WCHAR* pBuffer = strOutput.GetBuffer(nResult);
    MultiByteToWideChar(unCodePage, 0, strABytes, strABytes.GetLength(), pBuffer, nResult);
    strOutput.ReleaseBuffer(nResult);
    return strOutput;
}

// 处理 WriteFile 的部分写入，直到所有字节写完或发生错误。
bool WriteBytes(HANDLE hFile, const void* pData, DWORD dwSize)
{
    const BYTE* pBuffer = (const BYTE*)pData;
    while (dwSize)
    {
        DWORD dwWritten = 0;
        if (!WriteFile(hFile, pBuffer, dwSize, &dwWritten, NULL) || !dwWritten)
            return false;
        pBuffer += dwWritten;
        dwSize -= dwWritten;
    }
    return true;
}

// 临时内容完整落盘后再替换目标，减少异常退出造成的半文件。
bool AtomicWrite(const CString& strPath, const CStringA& strAData)
{
    CString strTemp = strPath + L".tmp";
    CAutoHandle hFile(CreateFileW(strTemp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
    if (!hFile.Valid() || !WriteBytes(hFile.Get(), (LPCSTR)strAData, strAData.GetLength()) ||
        !FlushFileBuffers(hFile.Get()))
        return false;
    hFile.Close();
    return MoveFileExW(strTemp, strPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

CString TimeText()
{
    SYSTEMTIME stTime;
    GetLocalTime(&stTime);
    CString strText;
    strText.Format(L"%04u-%02u-%02u %02u:%02u:%02u.%03u", stTime.wYear, stTime.wMonth, stTime.wDay, stTime.wHour,
                   stTime.wMinute, stTime.wSecond, stTime.wMilliseconds);
    return strText;
}

CString DateText()
{
    return TimeText().Left(10);
}

CString WinError(DWORD dwError)
{
    WCHAR szBuffer[512] = {0};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwError, 0, szBuffer,
                   _countof(szBuffer), NULL);
    CString strText(szBuffer);
    strText.Trim();
    CString strResult;
    strResult.Format(L"%s (%lu)", (LPCWSTR)strText, dwError);
    return strResult;
}

// 兼容历史 ENC: 密码；解密缓冲在使用后清零，配置保存仍使用明文。
bool DecryptPassword(const CString& strEncrypted, CString& strPlain)
{
    strPlain.Empty();
    if (strEncrypted.IsEmpty())
        return true;
    if (strEncrypted.Left(4) != L"ENC:")
        return false;
    DWORD dwLength = 0;
    if (!CryptStringToBinaryW(strEncrypted.Mid(4), 0, CRYPT_STRING_BASE64, NULL, &dwLength, NULL, NULL))
        return false;
    std::vector<BYTE> vecBuffer(dwLength);
    if (!dwLength ||
        !CryptStringToBinaryW(strEncrypted.Mid(4), 0, CRYPT_STRING_BASE64, &vecBuffer[0], &dwLength, NULL, NULL))
        return false;
    DATA_BLOB stIn = {dwLength, &vecBuffer[0]};
    DATA_BLOB stOut = {0};
    if (!CryptUnprotectData(&stIn, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &stOut))
        return false;
    bool bValid = stOut.cbData >= sizeof(WCHAR) && stOut.cbData % sizeof(WCHAR) == 0 &&
                  ((WCHAR*)stOut.pbData)[stOut.cbData / sizeof(WCHAR) - 1] == 0;
    if (bValid)
        strPlain = (WCHAR*)stOut.pbData;
    SecureZeroMemory(stOut.pbData, stOut.cbData);
    LocalFree(stOut.pbData);
    return bValid;
}

CString SafeFileName(const CString& strText)
{
    CString strResult(strText);
    LPCWSTR pszBad = L"\\/:*?\"<>|\r\n";
    for (int nIndex = 0; nIndex < strResult.GetLength(); ++nIndex)
        if (wcschr(pszBad, strResult[nIndex]))
            strResult.SetAt(nIndex, L'_');
    return strResult;
}
