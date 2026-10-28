#include "stdafx.h"
#include "DialogHelpers.h"

// 额外注册对话框矩形区域，使禁用的编辑框及其标签也能查看说明。
void AddParameterTip(CToolTipCtrl& tipControl, CWnd* pDialog, int nControl, LPCWSTR pszText)
{
    CWnd* pControl = pDialog->GetDlgItem(nControl);
    tipControl.AddTool(pControl, pszText);
    CRect rectControl;
    pControl->GetWindowRect(&rectControl);
    pDialog->ScreenToClient(&rectControl);
    tipControl.AddTool(pDialog, pszText, &rectControl, nControl + 10000);
    CWnd* pLabel = pControl->GetWindow(GW_HWNDPREV);
    if (pLabel)
    {
        WCHAR szClass[32] = {0};
        GetClassNameW(pLabel->GetSafeHwnd(), szClass, _countof(szClass));
        if (lstrcmpiW(szClass, L"Static") == 0)
        {
            pLabel->GetWindowRect(&rectControl);
            pDialog->ScreenToClient(&rectControl);
            tipControl.AddTool(pDialog, pszText, &rectControl, nControl);
        }
    }
}
