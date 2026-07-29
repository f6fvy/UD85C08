
// UD85C08_GUI.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CUD85C08GUIApp:
// See UD85C08_GUI.cpp for the implementation of this class
//

class CUD85C08GUIApp : public CWinApp
{
public:
	CUD85C08GUIApp();

// Overrides
public:
	virtual BOOL InitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
};

extern CUD85C08GUIApp theApp;
