// LedControl.cpp

#include "pch.h"
#include "LedControl.h"

namespace
{
    constexpr COLORREF ColorOffFill   = RGB(160, 160, 160);
    constexpr COLORREF ColorOffBorder = RGB(100, 100, 100);
    constexpr COLORREF ColorOnFill    = RGB(220, 40, 40);
    constexpr COLORREF ColorOnBorder  = RGB(120, 15, 15);
}

CLedControl::CLedControl()
{
}

CLedControl::~CLedControl()
{
}

BEGIN_MESSAGE_MAP(CLedControl, CStatic)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CLedControl::SetState(bool isOn)
{
    if (isOn == m_isOn)
        return;

    m_isOn = isOn;

    if (::IsWindow(m_hWnd))
        Invalidate(FALSE); // FALSE: no need to erase, OnEraseBkgnd is a no-op
}

BOOL CLedControl::OnEraseBkgnd(CDC* /*pDC*/)
{
    // Painting is fully handled (and double-buffered) in OnPaint/DrawLed;
    // erasing here separately would just cause flicker.
    return TRUE;
}

void CLedControl::OnPaint()
{
    CPaintDC dc(this);
    DrawLed(&dc);
}

void CLedControl::DrawLed(CDC* pDC)
{
    CRect rect;
    GetClientRect(&rect);

    // Double buffering: draw into an off-screen bitmap first, then blit
    // the whole thing in one go, to avoid any visible flicker.
    CDC memDC;
    memDC.CreateCompatibleDC(pDC);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
    CBitmap* pOldBitmap = memDC.SelectObject(&bitmap);

    // Background: use the dialog's own face color so the LED blends in
    // with the surrounding controls rather than showing a white square.
    memDC.FillSolidRect(&rect, ::GetSysColor(COLOR_BTNFACE));

    COLORREF fillColor = m_isOn ? ColorOnFill : ColorOffFill;
    COLORREF borderColor = m_isOn ? ColorOnBorder : ColorOffBorder;

    CBrush brush(fillColor);
    CBrush* pOldBrush = memDC.SelectObject(&brush);
    CPen pen(PS_SOLID, 1, borderColor);
    CPen* pOldPen = memDC.SelectObject(&pen);

    // Center a circle in the control's client area, slightly inset so
    // the border is fully visible.
    int diameter = (rect.Width() < rect.Height()) ? rect.Width() : rect.Height();
    diameter -= 2;
    if (diameter < 1)
        diameter = 1;

    CRect circleRect;
    circleRect.left = rect.left + (rect.Width() - diameter) / 2;
    circleRect.top = rect.top + (rect.Height() - diameter) / 2;
    circleRect.right = circleRect.left + diameter;
    circleRect.bottom = circleRect.top + diameter;

    memDC.Ellipse(&circleRect);

    memDC.SelectObject(pOldPen);
    memDC.SelectObject(pOldBrush);

    pDC->BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

    memDC.SelectObject(pOldBitmap);
}
