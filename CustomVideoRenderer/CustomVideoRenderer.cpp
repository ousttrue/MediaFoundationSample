#include "CustomVideoRenderer.h"
#include <mfidl.h>
#include <Mferror.h>
#include <wrl/client.h>


class CCritSec
{
public:
    CCritSec(void) :
        m_cs()
    {
        InitializeCriticalSection(&m_cs);
    }

    ~CCritSec(void)
    {
        DeleteCriticalSection(&m_cs);
    }

    _Acquires_lock_(this->m_cs)
        void Lock(void)
        {
            EnterCriticalSection(&m_cs);
        }

    _Releases_lock_(this->m_cs)
        void Unlock(void)
        {
            LeaveCriticalSection(&m_cs);
        }
private:
    CRITICAL_SECTION m_cs;
};


class CAutoLock
{
public:
    _Acquires_lock_(this->m_pLock->m_cs)
        CAutoLock(CCritSec* pLock) :
            m_pLock(pLock)
    {
        m_pLock->Lock();
    }

    _Releases_lock_(this->m_pLock->m_cs)
        ~CAutoLock(void)
        {
            m_pLock->Unlock();
        }
private:
    CCritSec* m_pLock;
};


class CustomVideoStreamSink: public IMFStreamSink
{
    ULONG m_nRefCount = 1;

    const DWORD                 STREAM_ID;
    CCritSec&                   m_critSec;                      // critical section for thread safety
    Microsoft::WRL::ComPtr<IMFMediaSink>               m_pSink; 

public:
    CustomVideoStreamSink(DWORD dwStreamId, CCritSec& critSec
            , IMFMediaSink *parent)
        : STREAM_ID(dwStreamId)
          , m_critSec(critSec)
          , m_pSink(parent)
    {
    }

    // IUnknown
    STDMETHODIMP_(ULONG) AddRef(void)override
    {
        return InterlockedIncrement(&m_nRefCount);
    }

    STDMETHODIMP QueryInterface(REFIID iid, __RPC__deref_out _Result_nullonfailure_ void** ppv)override
    {
        if (!ppv)
        {
            return E_POINTER;
        }
        if (iid == IID_IUnknown)
        {
            *ppv = static_cast<IUnknown*>(static_cast<IMFStreamSink*>(this));
        }
        else if (iid == __uuidof(IMFMediaSink))
        {
            *ppv = static_cast<IMFStreamSink*>(this);
        }
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) Release(void)override
    {
        ULONG uCount = InterlockedDecrement(&m_nRefCount);
        if (uCount == 0)
        {
            delete this;
        }
        // For thread safety, return a temporary variable.
        return uCount;
    }

    // IMFStreamSink
    STDMETHODIMP Flush(void)override
    {
        return E_FAIL;
    }

    STDMETHODIMP GetIdentifier(__RPC__out DWORD* pdwIdentifier)override
    {
        return E_FAIL;
    }

    STDMETHODIMP GetMediaSink(__RPC__deref_out_opt IMFMediaSink** ppMediaSink)override
    {
        return E_FAIL;
    }

    STDMETHODIMP GetMediaTypeHandler(__RPC__deref_out_opt IMFMediaTypeHandler** ppHandler)override
    {
        return E_FAIL;
    }

    STDMETHODIMP PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, __RPC__in const PROPVARIANT* pvarMarkerValue, __RPC__in const PROPVARIANT* pvarContextValue)override
    {
        return E_FAIL;
    }

    STDMETHODIMP ProcessSample(__RPC__in_opt IMFSample* pSample)override
    {
        return E_FAIL;
    }

    // IMFMediaEventGenerator (from IMFStreamSink)
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback,IUnknown* punkState)override
    {
        return E_FAIL;
    }

    STDMETHODIMP EndGetEvent(IMFAsyncResult* pResult, _Out_ IMFMediaEvent** ppEvent)override
    {
        return E_FAIL;
    }

    STDMETHODIMP GetEvent(DWORD dwFlags, __RPC__deref_out_opt IMFMediaEvent** ppEvent)override
    {
        return E_FAIL;
    }

    STDMETHODIMP QueueEvent(MediaEventType met, __RPC__in REFGUID guidExtendedType, HRESULT hrStatus, __RPC__in_opt const PROPVARIANT* pvValue)override
    {
        return E_FAIL;
    }
};


class CustomVideoRenderer: public IMFMediaSink
{
    ULONG m_nRefCount = 1;
    Microsoft::WRL::ComPtr<CustomVideoStreamSink> m_pStream;
    CCritSec m_csStreamSinkAndScheduler;
    bool m_IsShutdown=false;
    CCritSec m_csMediaSink;
    const DWORD STREAM_ID = 1;

    CustomVideoRenderer()
    {
    }

    HRESULT Initialize()
    {
        IUnknown **pp = &m_pStream;
        *pp=new CustomVideoStreamSink(STREAM_ID, m_csStreamSinkAndScheduler, this);

        return S_OK;
    }

    HRESULT CheckShutdown(void) const
    {
        if (m_IsShutdown)
        {
            return MF_E_SHUTDOWN;
        }
        else
        {
            return S_OK;
        }
    }

public:
    // Static method to create the object.
    static HRESULT CreateInstance(_In_ REFIID iid, _COM_Outptr_ void** ppSink)
    {
        if (ppSink == NULL)
        {
            return E_POINTER;
        }

        *ppSink = NULL;

        HRESULT hr = S_OK;
        auto pSink = new CustomVideoRenderer(); // Created with ref count = 1.

        if (pSink == NULL)
        {
            hr = E_OUTOFMEMORY;
        }

        if (SUCCEEDED(hr))
        {
            hr = pSink->Initialize();
        }

        if (SUCCEEDED(hr))
        {
            // AddRef
            hr = pSink->QueryInterface(iid, ppSink);
        }

        // SAFERELEASE
        if(pSink){
            pSink->Release();
            pSink=nullptr;
        }

        return hr;
    }

    // IUnknown
    STDMETHODIMP_(ULONG) AddRef(void)override
    {
        return InterlockedIncrement(&m_nRefCount);
    }

    STDMETHODIMP QueryInterface(REFIID iid, __RPC__deref_out _Result_nullonfailure_ void** ppv)override
    {
        if (!ppv)
        {
            return E_POINTER;
        }
        if (iid == IID_IUnknown)
        {
            *ppv = static_cast<IUnknown*>(static_cast<IMFMediaSink*>(this));
        }
        else if (iid == __uuidof(IMFMediaSink))
        {
            *ppv = static_cast<IMFMediaSink*>(this);
        }
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) Release(void)override
    {
        ULONG uCount = InterlockedDecrement(&m_nRefCount);
        if (uCount == 0)
        {
            delete this;
        }
        // For thread safety, return a temporary variable.
        return uCount;
    }

    // IMFMediaSink methods
    STDMETHODIMP AddStreamSink(DWORD dwStreamSinkIdentifier, __RPC__in_opt IMFMediaType* pMediaType, __RPC__deref_out_opt IMFStreamSink** ppStreamSink)override
    {
        return MF_E_STREAMSINKS_FIXED;
    }

    STDMETHODIMP GetCharacteristics(__RPC__out DWORD* pdwCharacteristics)override
    {
        CAutoLock lock(&m_csMediaSink);

        if (pdwCharacteristics == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = CheckShutdown();

        if (SUCCEEDED(hr))
        {
            *pdwCharacteristics = MEDIASINK_FIXED_STREAMS | MEDIASINK_CAN_PREROLL;
        }

        return hr;
    }

    STDMETHODIMP GetPresentationClock(__RPC__deref_out_opt IMFPresentationClock** ppPresentationClock)override
    {
        return E_FAIL;
    }

    STDMETHODIMP GetStreamSinkById(DWORD dwStreamSinkIdentifier
            , __RPC__deref_out_opt IMFStreamSink** ppStreamSink)override
    {
        CAutoLock lock(&m_csMediaSink);

        if (ppStreamSink == NULL)
        {
            return E_POINTER;
        }

        // Fixed stream ID.
        if (dwStreamSinkIdentifier != STREAM_ID)
        {
            return MF_E_INVALIDSTREAMNUMBER;
        }

        HRESULT hr = CheckShutdown();

        if (SUCCEEDED(hr))
        {
            *ppStreamSink = m_pStream.Get();
            (*ppStreamSink)->AddRef();
        }

        return E_FAIL;
    }

    STDMETHODIMP GetStreamSinkByIndex(DWORD dwIndex
            , __RPC__deref_out_opt IMFStreamSink** ppStreamSink)override
    {
        CAutoLock lock(&m_csMediaSink);

        if (ppStreamSink == NULL)
        {
            return E_POINTER;
        }

        // Fixed stream: Index 0.
        if (dwIndex > 0)
        {
            return MF_E_INVALIDINDEX;
        }

        HRESULT hr = CheckShutdown();
        if (SUCCEEDED(hr))
        {
            *ppStreamSink = m_pStream.Get();
            (*ppStreamSink)->AddRef();
        }

        return hr;
    }

    STDMETHODIMP GetStreamSinkCount(__RPC__out DWORD* pcStreamSinkCount)override
    {
        CAutoLock lock(&m_csMediaSink);

        if (pcStreamSinkCount == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = CheckShutdown();

        if (SUCCEEDED(hr))
        {
            *pcStreamSinkCount = 1;  // Fixed number of streams.
        }

        return hr;
    }

    STDMETHODIMP RemoveStreamSink(DWORD dwStreamSinkIdentifier)override
    {
        return MF_E_STREAMSINKS_FIXED;
    }

    STDMETHODIMP SetPresentationClock(__RPC__in_opt IMFPresentationClock* pPresentationClock)override
    {
        return E_FAIL;
    }

    STDMETHODIMP Shutdown(void)override
    {
        return E_FAIL;
    }
};


STDAPI CreateCustomVideoRenderer(REFIID riid, void **ppvObject)
{
    return CustomVideoRenderer::CreateInstance(riid, ppvObject);
}
