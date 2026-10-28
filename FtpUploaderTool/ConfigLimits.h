#pragma once
#include "Common.h"

// 集中定义界面校验与 INI 兼容读取使用的参数范围。
namespace ConfigLimits
{
    const UINT m_unPortMin = 1;
    const UINT m_unPortMax = 65535;
    const UINT m_unIntervalMin = 1;
    const UINT m_unIntervalMax = 86400;
    const UINT m_unRetryMin = 1;
    const UINT m_unRetryMax = 1000;
    const UINT m_unEmptyDirDaysMin = 1;
    const UINT m_unEmptyDirDaysMax = 36500;
    const UINT m_unLogSizeMin = 1;
    const UINT m_unLogSizeMax = 1024;
    const UINT m_unArchiveMin = 1;
    const UINT m_unArchiveMax = 1000;
    const UINT m_unUiLinesMin = 100;
    const UINT m_unUiLinesMax = 10000;
}
