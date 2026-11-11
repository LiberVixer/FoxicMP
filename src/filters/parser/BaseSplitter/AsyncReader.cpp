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

#include "stdafx.h"
#include "../../../../shared/progressiveclassicreadtest.h"
#include "AsyncReader.h"
#include "DSUtil/UrlParser.h"

//
// CAsyncFileReader
//

CAsyncFileReader::CAsyncFileReader(CString fn, HRESULT& hr, BOOL bSupportURL)
	: CUnknown(L"CAsyncFileReader", nullptr, &hr)
	, m_bSupportURL(bSupportURL)
{
	hr = Open(fn) ? S_OK : E_FAIL;
}

CAsyncFileReader::CAsyncFileReader(CHdmvClipInfo::CPlaylist& Items, HRESULT& hr)
	: CUnknown(L"CAsyncFileReader", nullptr, &hr)
{
	hr = OpenFiles(Items) ? S_OK : E_FAIL;
}

STDMETHODIMP CAsyncFileReader::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return
		QI(IAsyncReader)
		QI(ISyncReader)
		QI(IFileHandle)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

BOOL CAsyncFileReader::Open(LPCWSTR lpszFileName)
{
	if (::PathIsURLW(lpszFileName)) {
		CUrlParser urlParser;
		if (m_bSupportURL
				&& urlParser.Parse(lpszFileName)
				&& m_HTTPAsync.Connect(lpszFileName, http::connectTimeout) == S_OK) {
			const UINT64 ContentLength = m_HTTPAsync.GetLenght();
			if (ContentLength == 0) {
				return FALSE;
			}

			m_total = ContentLength;
			m_url = lpszFileName;
			m_sourcetype = SourceType::HTTP;

			return TRUE;
		}

		return FALSE;
	}

	return __super::Open(lpszFileName);
}

ULONGLONG CAsyncFileReader::GetLength()
{
	return m_total ? m_total : __super::GetLength();
}

// IAsyncReader

STDMETHODIMP CAsyncFileReader::SyncRead(LONGLONG llPosition, LONG lLength, BYTE* pBuffer)
{
	if ((ULONGLONG)llPosition + lLength > GetLength()) {
		return E_FAIL;
	}

	if (m_url.GetLength()) {
		auto RetryOnError = [&] {
			const DWORD dwError = GetLastError();
			if (dwError == ERROR_INTERNET_CONNECTION_RESET
					|| dwError == ERROR_HTTP_INVALID_SERVER_RESPONSE) {
				if (S_OK == m_HTTPAsync.Seek(llPosition)) {
					return true;
				}
			}
			return false;
		};

		for (;;) {
			if (m_pos != llPosition) {
				if (llPosition > m_pos && (llPosition - m_pos) <= 64 * KILOBYTE) {
					static std::vector<BYTE> pBufferTmp(64 * KILOBYTE);
					const DWORD lenght = llPosition - m_pos;

					DWORD dwSizeRead = 0;
					HRESULT hr = m_HTTPAsync.Read(pBufferTmp.data(), lenght, dwSizeRead, http::readTimeout);
					if (hr != S_OK || dwSizeRead != lenght) {
						if (RetryOnError()) {
							continue;
						}
						return E_FAIL;
					}
				} else {
					HRESULT hr = m_HTTPAsync.Seek(llPosition);
#ifdef DEBUG_OR_LOG
					DLog(L"CAsyncFileReader::SyncRead() : do HTTP seeking from %I64d to %I64d, hr = 0x%08x", m_pos, llPosition, hr);
#endif
					if (hr != S_OK) {
						return hr;
					}
				}

				m_pos = llPosition;
			}

			DWORD dwSizeRead = 0;
			HRESULT hr = m_HTTPAsync.Read(pBuffer, lLength, dwSizeRead, http::readTimeout);
			if (hr != S_OK || dwSizeRead != lLength) {
				if (RetryOnError()) {
					continue;
				}
				return E_FAIL;
			}
			m_pos += dwSizeRead;

			return S_OK;
		}

		return E_FAIL;
	}

	try {
		if ((ULONGLONG)llPosition != Seek(llPosition, FILE_BEGIN)) {
			return E_FAIL;
		}
		DWORD dwError;
		UINT readed = Read(pBuffer, lLength, dwError);
		if (readed < (UINT)lLength || dwError != ERROR_SUCCESS) {
			return E_FAIL;
		}

		return S_OK;
	}
	catch (CFileException* e) {
		m_lOsError = e->m_lOsError;
		e->Delete();

		return E_FAIL;
	}
}

STDMETHODIMP CAsyncFileReader::Length(LONGLONG* pTotal, LONGLONG* pAvailable)
{
	const LONGLONG len = GetLength();

	if (pTotal) {
		*pTotal = len;
	}
	if (pAvailable) {
		*pAvailable = len;
	}
	return S_OK;
}

CHarvesterFileReader::CHarvesterFileReader(std::shared_ptr<CHarvesterSession> session,int track,HRESULT& hr)
    : CUnknown(L"Harvester local reader",nullptr,&hr),m_session(std::move(session)),m_track(track) {
    m_path=m_session->Path(track);m_flush=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    hr=m_flush?S_OK:E_OUTOFMEMORY;
}
CHarvesterFileReader::~CHarvesterFileReader() {if(m_flush)CloseHandle(m_flush);}
STDMETHODIMP CHarvesterFileReader::NonDelegatingQueryInterface(REFIID riid,void** ppv) {
    CheckPointer(ppv,E_POINTER);
    return QI(IAsyncReader) QI(ISyncReader) QI(IFileHandle) QI(IHarvesterReader) __super::NonDelegatingQueryInterface(riid,ppv);
}
STDMETHODIMP CHarvesterFileReader::Length(LONGLONG* total,LONGLONG* available) {
    const auto s=m_session->Snapshot();if(total)*total=m_session->IsWebM(m_track) && !harvester::EndReady(s.tracks[m_track])?INT64_MAX:s.tracks[m_track].total?s.tracks[m_track].total:s.tracks[m_track].available;if(available)*available=s.tracks[m_track].available;return S_OK;
}
void CHarvesterFileReader::AdvanceFlushGeneration(bool active) {
    auto previous=m_flushGeneration.load();
    while(!m_flushGeneration.compare_exchange_weak(previous,((previous+2)&~uint64_t(1))|uint64_t(active))){}
}
bool CHarvesterFileReader::ReadInterrupted(uint64_t generation) const {
    const auto current=m_flushGeneration.load();
    const auto breaker=m_break.load();
    return current!=generation || (current&1) || WaitForSingleObject(m_session->CancelEvent(),0)==WAIT_OBJECT_0
        || WaitForSingleObject(m_flush,0)==WAIT_OBJECT_0
        || (breaker && WaitForSingleObject(breaker,0)==WAIT_OBJECT_0);
}
STDMETHODIMP CHarvesterFileReader::BeginFlush() {
    AdvanceFlushGeneration(true);SetEvent(m_flush);return S_OK;
}
STDMETHODIMP CHarvesterFileReader::EndFlush() {
    ResetEvent(m_flush);AdvanceFlushGeneration(false);return S_OK;
}
STDMETHODIMP CHarvesterFileReader::SyncRead(LONGLONG offset,LONG size,BYTE* data) {
    if(offset<0 || size<0 || uint64_t(size)>uint64_t(INT64_MAX)-uint64_t(offset) || (!data && size))return E_INVALIDARG;
    const auto generation=m_flushGeneration.load();
    const auto interrupted=[&]{return ReadInterrupted(generation);};
    if(interrupted())return E_ABORT;
    const auto s=m_session->Snapshot();
    if(s.tracks[m_track].total && !harvester::RangeReady(offset,size,s.tracks[m_track].total))return E_FAIL;
    if(m_opening && !harvester::RangeReady(offset,size,s.tracks[m_track].available))return E_FAIL;
    HRESULT hr=m_session->WaitRange(m_track,offset,size,m_flush,m_break.load());
    if(interrupted())return E_ABORT;
    if(hr!=S_OK || !size)return hr;
    if(m_session->HasVirtualReader()){
        auto result=m_session->ReadV2Guarded(m_track,uint64_t(offset),size_t(size),data,m_flush,m_break.load(),interrupted);
        if(interrupted())result=E_ABORT;
#ifdef FOXICMP_PRIVATE_CLASSIC_READ_TEST
        foxicmp::progressive::classic_read_test::Returned(this,result);
#endif
        return result;
    }
    OVERLAPPED ov{};ov.Offset=(DWORD)offset;ov.OffsetHigh=(DWORD)(uint64_t(offset)>>32);ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    DWORD read=0;BOOL ok=ReadFile(m_session->File(m_track),data,size,&read,&ov);
    const DWORD readError=ok?ERROR_SUCCESS:GetLastError();
#ifdef FOXICMP_PRIVATE_CLASSIC_READ_TEST
    foxicmp::progressive::classic_read_test::NativeIo(this,"submitted",m_session->File(m_track),&ov,readError);
#endif
    if(!ok && readError==ERROR_IO_PENDING) {
        HANDLE events[4]={ov.hEvent,m_session->CancelEvent(),m_flush,m_break.load()};DWORD n=events[3]?4:3;
        DWORD result=WAIT_TIMEOUT;
        for(;;) {
            // EndFlush can reset the event before this wait sees BeginFlush.
            // Recheck the captured generation while the OS request is pending.
            if(interrupted()){result=WAIT_OBJECT_0+1;break;}
#ifdef FOXICMP_PRIVATE_CLASSIC_READ_TEST
            foxicmp::progressive::classic_read_test::NativeIo(this,"waiting",m_session->File(m_track),&ov,readError);
#endif
            result=WaitForMultipleObjects(n,events,FALSE,50);
            if(result!=WAIT_TIMEOUT)break;
        }
        if(result==WAIT_OBJECT_0)ok=GetOverlappedResult(m_session->File(m_track),&ov,&read,FALSE);
        else {
            CancelIoEx(m_session->File(m_track),&ov);const BOOL drained=GetOverlappedResult(m_session->File(m_track),&ov,&read,TRUE);(void)drained;
#ifdef FOXICMP_PRIVATE_CLASSIC_READ_TEST
            foxicmp::progressive::classic_read_test::NativeIo(this,"cancelled",m_session->File(m_track),&ov,drained?ERROR_SUCCESS:GetLastError());
#endif
            CloseHandle(ov.hEvent);return E_ABORT;
        }
    }
#ifdef FOXICMP_PRIVATE_CLASSIC_READ_TEST
    foxicmp::progressive::classic_read_test::NativeIo(this,"settled",m_session->File(m_track),&ov,ok?ERROR_SUCCESS:GetLastError());
#endif
    CloseHandle(ov.hEvent);
    if(interrupted())return E_ABORT;
    if(!ok || read!=(DWORD)size) {m_error=true;m_session->Fail("Local track I/O failed or confirmed bytes were truncated");return E_FAIL;}
    return S_OK;
}

STDMETHODIMP CHarvesterFileReader::WaitForCompletion() {
    const auto track=m_session->Snapshot().tracks[m_track];
    m_session->BeginCompletionWait();
    const auto hr=m_session->WaitRange(m_track,track.total,0,m_flush,m_break.load(),true);
    m_session->EndCompletionWait();
    return hr;
}

STDMETHODIMP CHarvesterFileReader::WaitForSample(size_t index,harvester::Sample* sample) {
    if(!sample)return E_POINTER;
    for(;;) {
        HANDLE events[]={m_session->CancelEvent(),m_flush,m_break.load()};const DWORD count=events[2]?3:2;
        if(WaitForMultipleObjects(count,events,FALSE,0)!=WAIT_TIMEOUT)return E_ABORT;
        const auto status=m_session->SampleAt(m_track,index,*sample);
        if(status)return status==1?S_OK:S_FALSE;
        if(WaitForMultipleObjects(count,events,FALSE,50)!=WAIT_TIMEOUT)return E_ABORT;
    }
}
