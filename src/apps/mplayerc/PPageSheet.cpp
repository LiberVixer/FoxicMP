/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2025 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "PPageSheet.h"
#include <HighDPI.h>

// CPPageSheet

IMPLEMENT_DYNAMIC(CPPageSheet, CTreePropSheet)

CPPageSheet::CPPageSheet(LPCWSTR pszCaption, CWnd* pParentWnd, UINT idPage)
	: CTreePropSheet(pszCaption, pParentWnd, 0)
{
	int nWidth = 210;

	if (CDPI* pDpi = dynamic_cast<CDPI*>(AfxGetMainWnd())) {
		nWidth = pDpi->ScaleX(nWidth);
	} else {
		// this panel can be created without the main window.
		pDpi = DNew CDPI();
		nWidth = pDpi->ScaleX(nWidth);
		delete pDpi;
	}

	SetTreeWidth(nWidth);
	m_treeTitles.reserve(26);
	AddCategorizedPage(&m_player, L"General", L"Startup and tray");
	AddCategorizedPage(&m_formats, L"General", L"Integration");
	AddCategorizedPage(&m_logo, L"Interface and window", L"Logo");
	AddCategorizedPage(&m_interface, L"Interface and window", L"Appearance");
	AddCategorizedPage(&m_osd, L"Interface and window", L"OSD");
	AddCategorizedPage(&m_windowsize, L"Interface and window", L"Window");
	AddCategorizedPage(&m_fullscreen, L"Interface and window", L"Fullscreen");
	AddCategorizedPage(&m_acceltbl, L"Control", L"Keyboard");
	AddCategorizedPage(&m_mouse, L"Control", L"Mouse");
	AddCategorizedPage(&m_playback, L"Playback", L"Playback");
	AddCategorizedPage(&m_sync, L"Playback", L"Synchronization");
	AddCategorizedPage(&m_video, L"Video", L"Output");
	AddCategorizedPage(&m_color, L"Video", L"Color");
	AddCategorizedPage(&m_internalfilters, L"Video", L"Decoding");
	AddCategorizedPage(&m_externalfilters, L"Video", L"External filters");
	AddCategorizedPage(&m_filterspriority, L"Video", L"Filter priority");
	AddCategorizedPage(&m_audio, L"Audio", L"Output");
	AddCategorizedPage(&m_soundprocessing, L"Audio", L"Processing");
	AddCategorizedPage(&m_subtitles, L"Subtitles", L"Files and languages");
	AddCategorizedPage(&m_substyle, L"Subtitles", L"Appearance");
	AddCategorizedPage(&m_subMisc, L"Subtitles", L"Rendering");
	AddCategorizedPage(&m_dvd, L"Capture and devices", L"Discs");
	AddCategorizedPage(&m_tuner, L"Capture and devices", L"TV and capture");
	AddCategorizedPage(&m_youtube, L"Online and network", L"Online video");
	AddCategorizedPage(&m_webserver, L"Online and network", L"Web control");
	AddCategorizedPage(&m_misc, L"Service", L"Diagnostics");

	EnableStackedTabs(FALSE);

	SetTreeViewMode(TRUE, TRUE, FALSE);

	if (!idPage) {
		idPage = AfxGetAppSettings().nLastUsedPage;
	}

	if (idPage) {
		for (int i = 0; i < GetPageCount(); i++) {
			if (GetPage(i)->m_pPSP->pszTemplate == MAKEINTRESOURCEW(idPage)) {
				SetActivePage(i);

				break;
			}
		}
	}
}

CPPageSheet::~CPPageSheet()
{
}

CTreeCtrl* CPPageSheet::CreatePageTreeObject()
{
	return DNew CTreePropSheetTreeCtrl();
}

BEGIN_MESSAGE_MAP(CPPageSheet, CTreePropSheet)
	ON_WM_CONTEXTMENU()
	ON_CONTROL(EN_CHANGE, 0x7EEF, OnSearchChanged)
	ON_MESSAGE(WM_APP + 61, OnFinishLoading)
END_MESSAGE_MAP()

void CPPageSheet::AddCategorizedPage(CPropertyPage* page, LPCWSTR category, LPCWSTR name)
{
	const bool russian = AfxGetAppSettings().iCurrentLanguage == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_RUSSIAN);
	CString localized(category);
	if (russian) {
		if (localized == L"General") localized = L"\x041E\x0431\x0449\u0438\u0435";
		else if (localized == L"Interface and window") localized = L"\x0418\x043D\x0442\u0435\u0440\u0444\u0435\u0439\x0441 \x0438 \x043E\x043A\u043D\u043E";
		else if (localized == L"Control") localized = L"\x0423\x043F\u0440\u0430\u0432\u043B\u0435\u043D\u0438\u0435";
		else if (localized == L"Playback") localized = L"\x0412\x043E\x0441\x043F\x0440\u043E\u0438\u0437\u0432\u0435\u0434\u0435\u043D\u0438\u0435";
		else if (localized == L"Video") localized = L"\x0412\x0438\x0434\x0435\u043E";
		else if (localized == L"Audio") localized = L"\x0410\x0443\u0434\u0438\u043E";
		else if (localized == L"Subtitles") localized = L"\x0421\x0443\u0431\u0442\u0438\u0442\u0440\u044b";
		else if (localized == L"Capture and devices") localized = L"\x0421\x043D\u0438\u043C\u043A\u0438 \x0438 \x0443\u0441\u0442\u0440\u043E\u0439\u0441\u0442\u0432\u0430";
		else if (localized == L"Online and network") localized = L"\x041E\x043D\u043B\u0430\u0439\u043D \x0438 \x0441\u0435\u0442\u044C";
		else if (localized == L"Service") localized = L"\x0421\u0435\u0440\u0432\u0438\u0441";
	}
	CString title; title.Format(L"%s::%s", localized.GetString(), name);
	m_treeTitles.push_back(title);
	page->m_pPSP->pszTitle = m_treeTitles.back();
	page->m_psp.dwFlags |= PSP_USETITLE;
	AddPage(page);
}

HTREEITEM CPPageSheet::FindPage(HTREEITEM root, const CString& query)
{
	if (!root) return nullptr;
	CTreeCtrl* tree = GetPageTreeControl();
	for (HTREEITEM item = root; item; item = tree->GetNextItem(item, TVGN_NEXT)) {
		CString text = tree->GetItemText(item); text.MakeLower();
		if (tree->GetItemData(item) != static_cast<DWORD_PTR>(-1) && text.Find(query) >= 0) return item;
		if (HTREEITEM child = FindPage(tree->GetChildItem(item), query)) return child;
	}
	return nullptr;
}

void CPPageSheet::OnSearchChanged()
{
	CString query; m_search.GetWindowText(query); query.Trim(); query.MakeLower();
	CTreeCtrl* tree = GetPageTreeControl(); if (!tree) return;
	if (query.IsEmpty()) { m_searchHint.SetWindowTextW(L"Type a settings page name"); return; }
	HTREEITEM found = FindPage(tree->GetRootItem(), query);
	if (found) {
		for (HTREEITEM root = tree->GetRootItem(); root; root = tree->GetNextItem(root, TVGN_NEXT)) tree->Expand(root, TVE_COLLAPSE);
		tree->Expand(tree->GetParentItem(found), TVE_EXPAND); tree->EnsureVisible(found); tree->SelectItem(found);
		CString status; status.Format(L"Found: %s", tree->GetItemText(found)); m_searchHint.SetWindowTextW(status);
	} else m_searchHint.SetWindowTextW(L"Nothing found");
}

LRESULT CPPageSheet::OnFinishLoading(WPARAM, LPARAM)
{
	m_loading.ShowWindow(SW_HIDE); m_loadingText.ShowWindow(SW_HIDE);
	if (!m_bLockPage) { m_search.EnableWindow(TRUE); if (GetPageTreeControl()) GetPageTreeControl()->EnableWindow(TRUE); }
	return 0;
}

BOOL CPPageSheet::OnInitDialog()
{
	BOOL bResult = __super::OnInitDialog();

	if (CTreeCtrl* pTree = GetPageTreeControl()) {
		CRect rect; pTree->GetWindowRect(&rect); ScreenToClient(&rect);
		m_search.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, CRect(rect.left, rect.top, rect.right, rect.top + 22), this, 0x7EEF);
		m_search.SetFont(GetFont());
		m_searchHint.Create(L"Type a settings page name", WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(rect.left + 3, rect.top + 23, rect.right, rect.top + 40), this);
		m_searchHint.SetFont(GetFont()); rect.top += 42; pTree->MoveWindow(&rect);
		m_search.EnableWindow(FALSE); pTree->EnableWindow(FALSE);
		CRect client; GetClientRect(&client);
		m_loadingText.Create(L"Loading settings...", WS_CHILD | WS_VISIBLE | SS_CENTER, CRect(rect.right + 8, client.top + 8, client.right - 8, client.top + 30), this);
		m_loading.Create(WS_CHILD | WS_VISIBLE | PBS_MARQUEE, CRect(rect.right + 24, client.top + 34, client.right - 24, client.top + 48), this, 0x7EF0);
		m_loading.SetMarquee(TRUE, 30); PostMessage(WM_APP + 61);
	}

	if (CTreeCtrl* pTree = GetPageTreeControl()) {
		for (HTREEITEM node = pTree->GetRootItem(); node; node = pTree->GetNextSiblingItem(node)) {
			pTree->Expand(node, TVE_EXPAND);
		}
	}

	if (m_bLockPage) {
		GetPageTreeControl()->EnableWindow (FALSE);
	}

	return bResult;
}

void CPPageSheet::OnContextMenu(CWnd* /*pWnd*/, CPoint /*point*/)
{
}

// CTreePropSheetTreeCtrl

IMPLEMENT_DYNAMIC(CTreePropSheetTreeCtrl, CTreeCtrl)
CTreePropSheetTreeCtrl::CTreePropSheetTreeCtrl()
{
}

CTreePropSheetTreeCtrl::~CTreePropSheetTreeCtrl()
{
}

BEGIN_MESSAGE_MAP(CTreePropSheetTreeCtrl, CTreeCtrl)
END_MESSAGE_MAP()

// CTreePropSheetTreeCtrl message handlers

BOOL CTreePropSheetTreeCtrl::PreCreateWindow(CREATESTRUCT& cs)
{
	cs.dwExStyle |= WS_EX_CLIENTEDGE;
	//cs.style &= ~TVS_LINESATROOT;

	return __super::PreCreateWindow(cs);
}
