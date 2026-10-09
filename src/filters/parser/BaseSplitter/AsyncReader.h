/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2024 see Authors.txt
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

#pragma once

#include "MultiFiles.h"
#include "DSUtil/HTTPAsync.h"
#include "DSUtil/HarvesterSession.h"

interface __declspec(uuid("6DDB4EE7-45A0-4459-A508-BD77B32C91B2"))
ISyncReader :
public IUnknown {
	STDMETHOD_(void, SetBreakEvent)(HANDLE hBreakEvent) PURE;
	STDMETHOD_(bool, HasErrors)() PURE;
	STDMETHOD_(void, ClearErrors)() PURE;
	STDMETHOD_(void, SetPTSOffset)(REFERENCE_TIME* rtPTSOffset) PURE;
	STDMETHOD_(int, GetSourceType)() PURE;
	STDMETHOD (ReOpen)(CHdmvClipInfo::CPlaylist& Items) PURE;
};

interface __declspec(uuid("7D55F67A-826E-40B9-8A7D-3DF0CBBD272D"))
IFileHandle :
public IUnknown {
	STDMETHOD_(HANDLE, GetFileHandle)() PURE;
	STDMETHOD_(LPCWSTR, GetFileName)() PURE;
	STDMETHOD_(BOOL, IsValidFileName)() PURE;
};

interface __declspec(uuid("671EC05B-2A27-4934-B253-9E82AFA7D1CC"))
IHarvesterReader : public IUnknown {
	STDMETHOD_(CHarvesterSession*, GetSession)() PURE;
	STDMETHOD_(int, GetTrack)() PURE;
	STDMETHOD(WaitForCompletion)() PURE;
    STDMETHOD(WaitForSample)(size_t index,harvester::Sample* sample) PURE;
};

class CAsyncFileReader : public CUnknown, public CMultiFiles, public IAsyncReader, public ISyncReader, public IFileHandle
{
public:
	enum SourceType {
		LOCAL,
		HTTP
	};

protected:
	HANDLE m_hBreakEvent = nullptr;
	LONG m_lOsError = 0; // CFileException::m_lOsError

	SourceType m_sourcetype = SourceType::LOCAL;

	BOOL m_bSupportURL = FALSE;
	CHTTPAsync m_HTTPAsync;
	ULONGLONG m_total = 0;
	LONGLONG m_pos = 0;
	CString m_url;

	virtual BOOL Open(LPCWSTR lpszFileName) final;
	virtual ULONGLONG GetLength() final;

public:

	CAsyncFileReader(CString fn, HRESULT& hr, BOOL bSupportURL);
	CAsyncFileReader(CHdmvClipInfo::CPlaylist& Items, HRESULT& hr);

	DECLARE_IUNKNOWN;
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// IAsyncReader
	STDMETHODIMP RequestAllocator(IMemAllocator* pPreferred, ALLOCATOR_PROPERTIES* pProps, IMemAllocator** ppActual) { return E_NOTIMPL; }
	STDMETHODIMP Request(IMediaSample* pSample, DWORD_PTR dwUser) { return E_NOTIMPL; }
	STDMETHODIMP WaitForNext(DWORD dwTimeout, IMediaSample** ppSample, DWORD_PTR* pdwUser) { return E_NOTIMPL; }
	STDMETHODIMP SyncReadAligned(IMediaSample* pSample) { return E_NOTIMPL; }
	STDMETHODIMP SyncRead(LONGLONG llPosition, LONG lLength, BYTE* pBuffer);
	STDMETHODIMP Length(LONGLONG* pTotal, LONGLONG* pAvailable);
	STDMETHODIMP BeginFlush() { return E_NOTIMPL; }
	STDMETHODIMP EndFlush() { return E_NOTIMPL; }

	// ISyncReader
	STDMETHODIMP_(void) SetBreakEvent(HANDLE hBreakEvent) { m_hBreakEvent = hBreakEvent; }
	STDMETHODIMP_(bool) HasErrors() { return m_lOsError != 0; }
	STDMETHODIMP_(void) ClearErrors() { m_lOsError = 0; }
	STDMETHODIMP_(void) SetPTSOffset(REFERENCE_TIME* rtPTSOffset) { m_pCurrentPTSOffset = rtPTSOffset; };
	STDMETHODIMP_(int) GetSourceType() { return (int)m_sourcetype; }
	STDMETHODIMP ReOpen(CHdmvClipInfo::CPlaylist& Items) { return OpenFiles(Items) ? S_OK : E_FAIL; }

	// IFileHandle
	STDMETHODIMP_(HANDLE) GetFileHandle() { return m_hFile; }
	STDMETHODIMP_(LPCWSTR) GetFileName() { return !m_url.IsEmpty() ? m_url : (m_nCurPart != -1 ? m_strFiles[m_nCurPart] : m_strFiles[0]); }
	STDMETHODIMP_(BOOL) IsValidFileName() { return !m_url.IsEmpty() || !m_strFiles.empty(); }
};

// Used only by the explicit Harvester session graph; ordinary readers are unchanged.
class CHarvesterFileReader final : public CUnknown, public IAsyncReader, public ISyncReader, public IFileHandle, public IHarvesterReader {
    std::shared_ptr<CHarvesterSession> m_session;
    int m_track;
    HANDLE m_flush=nullptr;
    std::atomic<HANDLE> m_break{nullptr};
    bool m_opening=true;
    std::wstring m_path;
    std::atomic<bool> m_error{false};
public:
    CHarvesterFileReader(std::shared_ptr<CHarvesterSession> session,int track,HRESULT& hr);
    ~CHarvesterFileReader();
    void StartStreaming() { m_opening=false; }
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid,void** ppv);
    STDMETHODIMP RequestAllocator(IMemAllocator*,ALLOCATOR_PROPERTIES*,IMemAllocator**) {return E_NOTIMPL;}
    STDMETHODIMP Request(IMediaSample*,DWORD_PTR) {return E_NOTIMPL;}
    STDMETHODIMP WaitForNext(DWORD,IMediaSample**,DWORD_PTR*) {return E_NOTIMPL;}
    STDMETHODIMP SyncReadAligned(IMediaSample*) {return E_NOTIMPL;}
    STDMETHODIMP SyncRead(LONGLONG offset,LONG size,BYTE* data);
    STDMETHODIMP Length(LONGLONG* total,LONGLONG* available);
    STDMETHODIMP BeginFlush() {SetEvent(m_flush);return S_OK;}
    STDMETHODIMP EndFlush() {ResetEvent(m_flush);return S_OK;}
    STDMETHODIMP_(void) SetBreakEvent(HANDLE h) {m_break=h;}
    STDMETHODIMP_(bool) HasErrors() {return m_error;}
    STDMETHODIMP_(void) ClearErrors() {m_error=false;}
    STDMETHODIMP_(void) SetPTSOffset(REFERENCE_TIME*) {}
    STDMETHODIMP_(int) GetSourceType() {return CAsyncFileReader::LOCAL;}
    STDMETHODIMP ReOpen(CHdmvClipInfo::CPlaylist&) {return E_NOTIMPL;}
    STDMETHODIMP_(HANDLE) GetFileHandle() {return m_session->File(m_track);}
    STDMETHODIMP_(LPCWSTR) GetFileName() {return m_path.c_str();}
    STDMETHODIMP_(BOOL) IsValidFileName() {return TRUE;}
    STDMETHODIMP_(CHarvesterSession*) GetSession() {return m_session.get();}
    STDMETHODIMP_(int) GetTrack() {return m_track;}
    STDMETHODIMP WaitForCompletion();
    STDMETHODIMP WaitForSample(size_t index,harvester::Sample* sample);
};
