#pragma once
#include "stdafx.h"

// 同时覆盖控件、禁用控件区域及前置标签，使参数说明保持可访问。
void AddParameterTip(CToolTipCtrl& tipControl, CWnd* pDialog, int nControl, LPCWSTR pszText);
