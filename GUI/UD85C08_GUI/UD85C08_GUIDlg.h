
// UD85C08_GUIDlg.h : header file
//

#pragma once

#include "LedControl.h"
#include "UD85C08_Controller.h" // public API, no MFC dependency
#include <memory>

// CUD85C08_GUIDlg dialog
class CUD85C08_GUIDlg : public CDialogEx
{
// Construction
public:
	CUD85C08_GUIDlg(CWnd* pParent = nullptr);	// standard constructor

	afx_msg void OnDestroy();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_UD85C08_GUI_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

private:
	// Combo box item order must match: Normal, Momentary, Toggle, Latch, Delay
	enum RelayModeIndex { ModeNormal = 0, ModeMomentary, ModeToggle, ModeLatch, ModeDelay };

	CLedControl m_ledInput1;
	CLedControl m_ledInput2;
	CLedControl m_ledInput3;
	CLedControl m_ledInput4;
	CLedControl m_ledInput5;
	CLedControl m_ledInput6;
	CLedControl m_ledInput7;
	CLedControl m_ledInput8;

	CLedControl m_ledRelay1;
	CLedControl m_ledRelay2;
	CLedControl m_ledRelay3;
	CLedControl m_ledRelay4;
	CLedControl m_ledRelay5;
	CLedControl m_ledRelay6;
	CLedControl m_ledRelay7;
	CLedControl m_ledRelay8;

	CComboBox m_comboMode;
	void PopulateModeCombo(bool includeDelay, int preferredIndex = ModeNormal);

	CComboBox m_comboIoRelation;

	CComboBox m_comboPort;
	void PopulateComPortList();

	CEdit m_editDelay;

	std::unique_ptr<UD85C08::Controller> m_controller;

	// Builds the device path CSerialPort2 expects. Always using the
	// "\\.\COMx" extended form (valid for any COM number, not just >=10)
	// avoids the ambiguity noted in HARDWARE_ASSUMPTIONS.md about port
	// names above COM9.
	std::wstring BuildPortPath(const CString& portName) const;

	// Reads which protocol radio button is currently checked.
	UD85C08::ProtocolMode GetSelectedProtocol() const;

	// Enables/disables the controls that must be locked while connected
	// (port combo, protocol radios) vs. the ones that only make sense
	// once connected (Disconnect button, per-channel controls later on).
	void SetConnectedUIState(bool connected);


	void UpdateChannelButtonsForMode();
	void EnableChannelControls(bool enable);
	void OnRelayButtonClicked(int channel, bool isOnButton);

	afx_msg void OnBnClickedButtonConnect();
	afx_msg void OnBnClickedButtonDisconnect();
	afx_msg void OnBnClickedButtonRefresh();

	afx_msg void OnRelayOnRangeClicked(UINT nID);
	afx_msg void OnRelayOffRangeClicked(UINT nID);

	afx_msg void OnBnClickedButtonAllOn();
	afx_msg void OnBnClickedButtonAllOff();

	afx_msg void OnCbnSelchangeComboMode();
	afx_msg void OnCbnSelchangeComboIoRelation();
};
