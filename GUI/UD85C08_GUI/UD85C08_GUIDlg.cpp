
// UD85C08_GUIDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "UD85C08_GUI.h"
#include "UD85C08_GUIDlg.h"
#include "Settings.h"
#include "afxdialogex.h"
#include <vector>
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// IMPORTANT: IDC_BUTTON_RELAY_ON_1..8, IDC_BUTTON_RELAY_OFF_1..8,
// IDC_LED_INPUT_1..8 and IDC_LED_RELAY_1..8 are all assumed to be
// contiguous ranges of 8 consecutive resource IDs (see resource.h).
// This is relied upon by ON_CONTROL_RANGE below, and by the
// "IDC_xxx_1 + i" arithmetic used throughout this file to address
// a given channel's control without a lookup table.
//
// If a new control is ever inserted via the resource editor between
// two IDs of one of these ranges, the assigned IDs may no longer be
// contiguous and this whole scheme will silently break (wrong
// control manipulated, or ON_CONTROL_RANGE missing/misfiring for a
// channel) without any compiler error. Check resource.h after any
// resource-editor change that touches these controls.

// CUD85C08_GUIDlg dialog

CUD85C08_GUIDlg::CUD85C08_GUIDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_UD85C08_GUI_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CUD85C08_GUIDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_LED_INPUT_1, m_ledInput1);
	DDX_Control(pDX, IDC_LED_INPUT_2, m_ledInput2);
	DDX_Control(pDX, IDC_LED_INPUT_3, m_ledInput3);
	DDX_Control(pDX, IDC_LED_INPUT_4, m_ledInput4);
	DDX_Control(pDX, IDC_LED_INPUT_5, m_ledInput5);
	DDX_Control(pDX, IDC_LED_INPUT_6, m_ledInput6);
	DDX_Control(pDX, IDC_LED_INPUT_7, m_ledInput7);
	DDX_Control(pDX, IDC_LED_INPUT_8, m_ledInput8);

	DDX_Control(pDX, IDC_LED_RELAY_1, m_ledRelay1);
	DDX_Control(pDX, IDC_LED_RELAY_2, m_ledRelay2);
	DDX_Control(pDX, IDC_LED_RELAY_3, m_ledRelay3);
	DDX_Control(pDX, IDC_LED_RELAY_4, m_ledRelay4);
	DDX_Control(pDX, IDC_LED_RELAY_5, m_ledRelay5);
	DDX_Control(pDX, IDC_LED_RELAY_6, m_ledRelay6);
	DDX_Control(pDX, IDC_LED_RELAY_7, m_ledRelay7);
	DDX_Control(pDX, IDC_LED_RELAY_8, m_ledRelay8);

	DDX_Control(pDX, IDC_COMBO_PORT, m_comboPort);

	DDX_Control(pDX, IDC_COMBO_MODE, m_comboMode);
	DDX_Control(pDX, IDC_COMBO_IO_RELATION, m_comboIoRelation);

	DDX_Control(pDX, IDC_EDIT_DELAY, m_editDelay);
}

BEGIN_MESSAGE_MAP(CUD85C08_GUIDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()

	ON_BN_CLICKED(IDC_BUTTON_CONNECT, &CUD85C08_GUIDlg::OnBnClickedButtonConnect)
	ON_BN_CLICKED(IDC_BUTTON_DISCONNECT, &CUD85C08_GUIDlg::OnBnClickedButtonDisconnect)
	ON_BN_CLICKED(IDC_BUTTON_REFRESH, &CUD85C08_GUIDlg::OnBnClickedButtonRefresh)

	ON_CONTROL_RANGE(BN_CLICKED, IDC_BUTTON_RELAY_ON_1, IDC_BUTTON_RELAY_ON_8, &CUD85C08_GUIDlg::OnRelayOnRangeClicked)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_BUTTON_RELAY_OFF_1, IDC_BUTTON_RELAY_OFF_8, &CUD85C08_GUIDlg::OnRelayOffRangeClicked)

	ON_BN_CLICKED(IDC_BUTTON_ALL_ON, &CUD85C08_GUIDlg::OnBnClickedButtonAllOn)
	ON_BN_CLICKED(IDC_BUTTON_ALL_OFF, &CUD85C08_GUIDlg::OnBnClickedButtonAllOff)

	ON_CBN_SELCHANGE(IDC_COMBO_MODE, &CUD85C08_GUIDlg::OnCbnSelchangeComboMode)
	ON_CBN_SELCHANGE(IDC_COMBO_IO_RELATION, &CUD85C08_GUIDlg::OnCbnSelchangeComboIoRelation)
END_MESSAGE_MAP()


// CUD85C08_GUIDlg message handlers

BOOL CUD85C08_GUIDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	m_editDelay.SetLimitText(4);

	PopulateComPortList();

	AppSettings settings = LoadSettings();

	int portIndex = m_comboPort.FindStringExact(0, settings.port);
	if (portIndex != CB_ERR)
		m_comboPort.SetCurSel(portIndex);
	// otherwise: leave the default selection (first port in the list) from
	// PopulateComPortList - the saved port is no longer present.

	CheckRadioButton(IDC_RADIO_PROTOCOL_TEXT, IDC_RADIO_PROTOCOL_BINARY,
		(settings.protocol == UD85C08::ProtocolMode::At) ? IDC_RADIO_PROTOCOL_TEXT
		: IDC_RADIO_PROTOCOL_BINARY);

	PopulateModeCombo(settings.protocol == UD85C08::ProtocolMode::At, settings.modeIndex);

	CString delayStr;
	delayStr.Format(_T("%d"), settings.delaySeconds);
	m_editDelay.SetWindowText(delayStr);

	m_comboIoRelation.SetCurSel(static_cast<int>(settings.ioRelation));

	UpdateChannelButtonsForMode(); // apply the default mode's button labels/visibility

	SetConnectedUIState(false);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CUD85C08_GUIDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CUD85C08_GUIDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

// UD85C08_GUIDlg.cpp

void CUD85C08_GUIDlg::PopulateComPortList()
{
	m_comboPort.ResetContent();

	HKEY hKey;
	LONG result = ::RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		_T("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
		0, KEY_READ, &hKey);

	if (result != ERROR_SUCCESS)
		return; // no serial ports registered, leave the combo empty

	std::vector<CString> portNames;

	TCHAR valueName[256];
	TCHAR portName[256];
	DWORD index = 0;

	for (;;)
	{
		DWORD valueNameSize = _countof(valueName);
		DWORD portNameSize = sizeof(portName);
		DWORD valueType = 0;

		LONG enumResult = ::RegEnumValue(hKey, index,
			valueName, &valueNameSize,
			nullptr, &valueType,
			reinterpret_cast<LPBYTE>(portName), &portNameSize);

		if (enumResult == ERROR_NO_MORE_ITEMS)
			break;

		if (enumResult == ERROR_SUCCESS && valueType == REG_SZ)
			portNames.push_back(portName); // e.g. "COM5"

		++index;
	}

	::RegCloseKey(hKey);

	// Numeric sort (COM2 before COM10), not a plain string sort.
	std::sort(portNames.begin(), portNames.end(),
		[](const CString& a, const CString& b)
		{
			int numA = _ttoi(a.Mid(3)); // skip the "COM" prefix
			int numB = _ttoi(b.Mid(3));
			return numA < numB;
		});

	for (const CString& name : portNames)
		m_comboPort.AddString(name);

	if (m_comboPort.GetCount() > 0)
		m_comboPort.SetCurSel(0);
}

std::wstring CUD85C08_GUIDlg::BuildPortPath(const CString& portName) const
{
	// "\\.\COMx" works for any COM port number (not just >= 10), so we
	// always use this extended form to sidestep the issue entirely.
	CString path = _T("\\\\.\\") + portName;
	return std::wstring(path.GetString());
}

UD85C08::ProtocolMode CUD85C08_GUIDlg::GetSelectedProtocol() const
{
	int checkedId = GetCheckedRadioButton(IDC_RADIO_PROTOCOL_TEXT, IDC_RADIO_PROTOCOL_BINARY);
	return (checkedId == IDC_RADIO_PROTOCOL_TEXT) ? UD85C08::ProtocolMode::At
		: UD85C08::ProtocolMode::Hex;
}

void CUD85C08_GUIDlg::SetConnectedUIState(bool connected)
{
	GetDlgItem(IDC_COMBO_PORT)->EnableWindow(!connected);
	GetDlgItem(IDC_BUTTON_REFRESH)->EnableWindow(!connected);
	GetDlgItem(IDC_RADIO_PROTOCOL_TEXT)->EnableWindow(!connected);
	GetDlgItem(IDC_RADIO_PROTOCOL_BINARY)->EnableWindow(!connected);
	GetDlgItem(IDC_BUTTON_CONNECT)->EnableWindow(!connected);
	GetDlgItem(IDC_BUTTON_DISCONNECT)->EnableWindow(connected);

	EnableChannelControls(connected);
	if (connected)
		UpdateChannelButtonsForMode(); // re-apply delay field state for current mode
}

void CUD85C08_GUIDlg::OnBnClickedButtonConnect()
{
	if (m_controller) // already connected (or a click slipped through before the UI grayed out)
		return;

	int selectedIndex = m_comboPort.GetCurSel();
	if (selectedIndex == CB_ERR)
	{
		AfxMessageBox(_T("No COM port selected."), MB_ICONWARNING);
		return;
	}

	CString portName;
	m_comboPort.GetLBText(m_comboPort.GetCurSel(), portName);

	if (portName.IsEmpty())
	{
		AfxMessageBox(_T("No COM port selected."), MB_ICONWARNING);
		return;
	}

	UD85C08::ProtocolMode protocol = GetSelectedProtocol();
	m_controller = std::make_unique<UD85C08::Controller>(protocol);

	UD85C08::OperationResult result = m_controller->Connect(BuildPortPath(portName));

	if (!result.IsOk())
	{
		m_controller.reset();
		AfxMessageBox(_T("Could not open the selected COM port."), MB_ICONERROR);
		return;
	}

	int currentModeIndex = m_comboMode.GetCurSel(); // preserve whatever was selected/restored
	PopulateModeCombo(m_controller->SupportsDelay(), currentModeIndex);

	AppSettings settings = LoadSettings();
	settings.port = portName;
	settings.protocol = protocol;
	SaveSettings(settings);

	SetConnectedUIState(true);
}

void CUD85C08_GUIDlg::OnBnClickedButtonDisconnect()
{
	if (m_controller)
	{
		m_controller->StopInputPolling(); // safe no-op if never started
		m_controller->Disconnect();
		m_controller.reset();
	}

	SetConnectedUIState(false);

}

void CUD85C08_GUIDlg::OnBnClickedButtonRefresh()
{
	PopulateComPortList();
}

void CUD85C08_GUIDlg::OnDestroy()
{
	AppSettings settings;

	if (m_comboPort.GetCurSel() != CB_ERR)
	{
		CString portText;
		m_comboPort.GetLBText(m_comboPort.GetCurSel(), portText);
		settings.port = portText;
	}
	settings.protocol = GetSelectedProtocol();
	settings.modeIndex = (m_comboMode.GetCurSel() != CB_ERR) ? m_comboMode.GetCurSel() : ModeNormal;

	CString delayText;
	m_editDelay.GetWindowText(delayText);
	settings.delaySeconds = _ttoi(delayText);

	int ioIndex = m_comboIoRelation.GetCurSel();
	settings.ioRelation = static_cast<UD85C08::InputOutputRelation>(ioIndex != CB_ERR ? ioIndex : 0);

	SaveSettings(settings);

	if (m_controller)
	{
		m_controller->StopInputPolling();
		m_controller->Disconnect();
		m_controller.reset();
	}

	CDialogEx::OnDestroy();
}

void CUD85C08_GUIDlg::UpdateChannelButtonsForMode()
{
	int modeIndex = m_comboMode.GetCurSel();
	bool isNormal = (modeIndex == ModeNormal);
	bool isDelay = (modeIndex == ModeDelay);

	LPCTSTR onLabel = _T("ON");
	switch (modeIndex)
	{
	case ModeNormal:    onLabel = _T("ON");     break;
	case ModeMomentary: onLabel = _T("Pulse");  break;
	case ModeToggle:    onLabel = _T("Toggle"); break;
	case ModeLatch:     onLabel = _T("Latch");  break;
	case ModeDelay:     onLabel = _T("Pulse");  break;
	}

	for (int i = 0; i < 8; ++i)
	{
		GetDlgItem(IDC_BUTTON_RELAY_ON_1 + i)->SetWindowText(onLabel);
		GetDlgItem(IDC_BUTTON_RELAY_OFF_1 + i)->ShowWindow(isNormal ? SW_SHOW : SW_HIDE);
	}

	m_editDelay.EnableWindow(isDelay);

	// Show a sensible default the first time Delay mode is selected,
	// rather than leaving the field empty.
	if (isDelay)
	{
		CString current;
		m_editDelay.GetWindowText(current);
		if (current.IsEmpty())
			m_editDelay.SetWindowText(_T("10"));
	}
}

void CUD85C08_GUIDlg::EnableChannelControls(bool enable)
{
	for (int i = 0; i < 8; ++i)
	{
		GetDlgItem(IDC_BUTTON_RELAY_ON_1 + i)->EnableWindow(enable);
		GetDlgItem(IDC_BUTTON_RELAY_OFF_1 + i)->EnableWindow(enable);
	}
	GetDlgItem(IDC_BUTTON_ALL_ON)->EnableWindow(enable);
	GetDlgItem(IDC_BUTTON_ALL_OFF)->EnableWindow(enable);
	GetDlgItem(IDC_COMBO_MODE)->EnableWindow(enable);
	GetDlgItem(IDC_COMBO_IO_RELATION)->EnableWindow(enable);
	if (!enable)
		GetDlgItem(IDC_EDIT_DELAY)->EnableWindow(FALSE);
}

void CUD85C08_GUIDlg::OnRelayButtonClicked(int channel, bool isOnButton)
{
	if (!m_controller)
		return;

	UD85C08::OperationResult result;
	int modeIndex = m_comboMode.GetCurSel();

	switch (modeIndex)
	{
	case ModeNormal:
		result = isOnButton ? m_controller->SendOpen(channel) : m_controller->SendClose(channel);
		break;
	case ModeMomentary:
		result = m_controller->SendMomentary(channel);
		break;
	case ModeToggle:
		result = m_controller->SendToggle(channel);
		break;
	case ModeLatch:
		result = m_controller->SendLatch(channel);
		break;
	case ModeDelay:
		{
			if (!m_controller->SupportsDelay())
			{
				AfxMessageBox(_T("Delay is only available with the Text (AT) protocol."), MB_ICONWARNING);
				return;
			}
			CString delayText;
			m_editDelay.GetWindowText(delayText);
			int seconds = _ttoi(delayText);
			if (seconds < 0 || seconds > 9999)
			{
				AfxMessageBox(_T("Delay must be between 0 and 9999 seconds."), MB_ICONWARNING);
				return;
			}
			result = m_controller->SendDelay(channel, seconds);
			break;
		}
	default:
		return;
	}

	if (!result.IsOk())
	{
		CString msg;
		msg.Format(_T("Failed to send the command (%hs)."), UD85C08::ToString(result.code));
		AfxMessageBox(msg, MB_ICONERROR);
	}
}

void CUD85C08_GUIDlg::OnRelayOnRangeClicked(UINT nID)
{
	int channel = static_cast<int>(nID - IDC_BUTTON_RELAY_ON_1) + 1;
	OnRelayButtonClicked(channel, true);
}

void CUD85C08_GUIDlg::OnRelayOffRangeClicked(UINT nID)
{
	int channel = static_cast<int>(nID - IDC_BUTTON_RELAY_OFF_1) + 1;
	OnRelayButtonClicked(channel, false);
}

void CUD85C08_GUIDlg::OnCbnSelchangeComboMode()
{
	UpdateChannelButtonsForMode();
}

void CUD85C08_GUIDlg::OnCbnSelchangeComboIoRelation()
{
	if (!m_controller)
		return;

	int index = m_comboIoRelation.GetCurSel();
	if (index < 0)
		return;

	// Combo item order matches UD85C08::InputOutputRelation's numeric
	// values exactly: Independent=0, Self-locking=1, Interlocking=2,
	// Momentary=3 - no lookup table needed, just a direct cast.
	auto relation = static_cast<UD85C08::InputOutputRelation>(index);

	UD85C08::OperationResult result = m_controller->SendSetInputOutputRelation(relation);
	if (!result.IsOk())
	{
		CString msg;
		msg.Format(_T("Failed to send the I/O relation command (%hs)."), UD85C08::ToString(result.code));
		AfxMessageBox(msg, MB_ICONERROR);
	}
}

void CUD85C08_GUIDlg::OnBnClickedButtonAllOn()
{
	if (!m_controller)
		return;

	UD85C08::OperationResult result = m_controller->SendAllOpen();
	if (!result.IsOk())
	{
		CString msg;
		msg.Format(_T("Failed to send the 'all relays ON' command (%hs)."), UD85C08::ToString(result.code));
		AfxMessageBox(msg, MB_ICONERROR);
	}
}

void CUD85C08_GUIDlg::OnBnClickedButtonAllOff()
{
	if (!m_controller)
		return;

	UD85C08::OperationResult result = m_controller->SendAllClose();
	if (!result.IsOk())
	{
		CString msg;
		msg.Format(_T("Failed to send the 'all relays OFF' command (%hs)."), UD85C08::ToString(result.code));
		AfxMessageBox(msg, MB_ICONERROR);
	}
}

void CUD85C08_GUIDlg::PopulateModeCombo(bool includeDelay, int preferredIndex)
{
	m_comboMode.ResetContent();
	m_comboMode.AddString(_T("Normal"));
	m_comboMode.AddString(_T("Momentary"));
	m_comboMode.AddString(_T("Toggle"));
	m_comboMode.AddString(_T("Latch"));
	if (includeDelay)
		m_comboMode.AddString(_T("Delay"));

	int count = m_comboMode.GetCount();
	int index = (preferredIndex >= 0 && preferredIndex < count) ? preferredIndex : ModeNormal;
	m_comboMode.SetCurSel(index);
	UpdateChannelButtonsForMode();
}