// LedControl.h
// Simple two-state LED indicator control, derived from CStatic.
// Off = gray, On = red. No external bitmap resource required - the LED
// is drawn directly with GDI (filled circle), double-buffered to avoid
// flicker.
//
// Written from scratch for UD85C08_GUI (no third-party code), after
// deciding not to reuse an old CodeGuru CLed class whose licensing
// terms were unclear (see project discussion).
//
// Deliberately minimal for now: a single boolean state (SetState). If a
// richer version is ever needed (multiple colors, shapes, a "disabled"
// state...), that should become a separate, more general-purpose class
// rather than growing this one - see project notes.
//
// Usage: place a "Picture Control" (or any generic static frame) in the
// dialog editor, give it an ID, then bind it to a CLedControl member via
// DDX_Control in the dialog's DoDataExchange, exactly like any other
// subclassed control.

#pragma once

class CLedControl : public CStatic
{
public:
    CLedControl();
    virtual ~CLedControl();

    // Sets the LED on/off and repaints if the state actually changed.
    void SetState(bool isOn);
    bool GetState() const noexcept { return m_isOn; }

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

private:
    void DrawLed(CDC* pDC);

    bool m_isOn = false;
};
