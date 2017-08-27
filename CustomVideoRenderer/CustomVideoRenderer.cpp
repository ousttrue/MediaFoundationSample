#include "CustomVideoRenderer.h"
#include <mfidl.h>
#include <mfapi.h>
#include <Mferror.h>
#include <wrl/client.h>
#include <wmcodecdsp.h> // for MEDIASUBTYPE_V216

GUID const* const s_pVideoFormats[] =
{
    &MFVideoFormat_NV12,
    &MFVideoFormat_IYUV,
    &MFVideoFormat_YUY2,
    &MFVideoFormat_YV12,
    &MFVideoFormat_RGB32,
    &MFVideoFormat_RGB32,
    &MFVideoFormat_RGB24,
    &MFVideoFormat_RGB555,
    &MFVideoFormat_RGB565,
    &MFVideoFormat_RGB8,
    &MFVideoFormat_AYUV,
    &MFVideoFormat_UYVY,
    &MFVideoFormat_YVYU,
    &MFVideoFormat_YVU9,
    &MEDIASUBTYPE_V216,
    &MFVideoFormat_v410,
    &MFVideoFormat_I420,
    &MFVideoFormat_NV11,
    &MFVideoFormat_420O
};
const DWORD s_dwNumVideoFormats = sizeof(s_pVideoFormats) / sizeof(s_pVideoFormats[0]);


struct FormatEntry
{
    GUID            Subtype;
    DXGI_FORMAT     DXGIFormat;
};
const FormatEntry s_DXGIFormatMapping[] =
{
    { MFVideoFormat_RGB32,      DXGI_FORMAT_B8G8R8X8_UNORM },
    { MFVideoFormat_ARGB32,     DXGI_FORMAT_R8G8B8A8_UNORM },
    { MFVideoFormat_AYUV,      DXGI_FORMAT_AYUV },
    { MFVideoFormat_YUY2,      DXGI_FORMAT_YUY2 },
    { MFVideoFormat_NV12,      DXGI_FORMAT_NV12 },
    { MFVideoFormat_NV11,      DXGI_FORMAT_NV11 },
    { MFVideoFormat_AI44,      DXGI_FORMAT_AI44 },
    { MFVideoFormat_P010,      DXGI_FORMAT_P010 },
    { MFVideoFormat_P016,      DXGI_FORMAT_P016 },
    { MFVideoFormat_Y210,      DXGI_FORMAT_Y210 },
    { MFVideoFormat_Y216,      DXGI_FORMAT_Y216 },
    { MFVideoFormat_Y410,      DXGI_FORMAT_Y410 },
    { MFVideoFormat_Y416,      DXGI_FORMAT_Y416 },
    { MFVideoFormat_420O,      DXGI_FORMAT_420_OPAQUE }
};


// State enum: Defines the current state of the stream.
enum class State
{
    State_TypeNotSet = 0,   // No media type is set
    State_Ready,            // Media type is set, Start has never been called.
    State_Started,
    State_Paused,
    State_Stopped,

    State_Count             // Number of states
};

// StreamOperation: Defines various operations that can be performed on the stream.
enum class StreamOperation
{
    OpSetMediaType = 0,
    OpStart,
    OpRestart,
    OpPause,
    OpStop,
    OpProcessSample,
    OpPlaceMarker,

    Op_Count                // Number of operations
};


BOOL ValidStateMatrix[State::State_Count][StreamOperation::Op_Count] =
{
    // States:    Operations:
    //            SetType   Start     Restart   Pause     Stop      Sample    Marker
    /* NotSet */  TRUE,     FALSE,    FALSE,    FALSE,    FALSE,    FALSE,    FALSE,

    /* Ready */   TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     FALSE,    TRUE,

    /* Start */   TRUE,     TRUE,     FALSE,    TRUE,     TRUE,     TRUE,     TRUE,

    /* Pause */   TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,

    /* Stop */    TRUE,     TRUE,     FALSE,    FALSE,    TRUE,     FALSE,    TRUE

    // Note about states:
    // 1. OnClockRestart should only be called from paused state.
    // 2. While paused, the sink accepts samples but does not process them.

};


//-------------------------------------------------------------------
// Name: ValidateOperation
// Description: Checks if an operation is valid in the current state.
//-------------------------------------------------------------------

static HRESULT ValidateOperation(State state, StreamOperation op)
{
    HRESULT hr = S_OK;

    BOOL bTransitionAllowed = ValidStateMatrix[(int)state][(int)op];

    if (bTransitionAllowed)
    {
        return S_OK;
    }
    else
    {
        return MF_E_INVALIDREQUEST;
    }
}


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


class CustomVideoStreamSink: public IMFStreamSink, public IMFMediaTypeHandler
{
    ULONG m_nRefCount = 1;

    const DWORD                 STREAM_ID;
    CCritSec&                   m_critSec;                      // critical section for thread safety
    Microsoft::WRL::ComPtr<IMFMediaSink>               m_pSink; 
    bool m_IsShutdown = false;
    Microsoft::WRL::ComPtr<IMFMediaType> m_pCurrentType;

    State m_state = State::State_TypeNotSet;

    struct sFraction
    {
        DWORD Numerator;
        DWORD Denominator;
    };
    sFraction m_imageBytesPP = { 1, 1 };
    DXGI_FORMAT                 m_dxgiFormat = DXGI_FORMAT_UNKNOWN;

public:
    CustomVideoStreamSink(DWORD dwStreamId, CCritSec& critSec
            , IMFMediaSink *parent)
        : STREAM_ID(dwStreamId)
          , m_critSec(critSec)
          , m_pSink(parent)
    {
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
        else if (iid == __uuidof(IMFStreamSink))
        {
            *ppv = static_cast<IMFStreamSink*>(this);
        }
        else if (iid == __uuidof(IMFMediaEventGenerator))
        {
            *ppv = static_cast<IMFMediaEventGenerator*>(this);
        }
        else if (iid == __uuidof(IMFMediaTypeHandler))
        {
            *ppv = static_cast<IMFMediaTypeHandler*>(this);
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
        CAutoLock lock(&m_critSec);

        if (ppMediaSink == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = CheckShutdown();

        if (SUCCEEDED(hr))
        {
            *ppMediaSink = m_pSink.Get();
            (*ppMediaSink)->AddRef();
        }

        return hr;
    }

    STDMETHODIMP GetMediaTypeHandler(__RPC__deref_out_opt IMFMediaTypeHandler** ppHandler)override
    {
        CAutoLock lock(&m_critSec);

        if (ppHandler == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = CheckShutdown();

        // This stream object acts as its own type handler, so we QI ourselves.
        if (SUCCEEDED(hr))
        {
            hr = this->QueryInterface(IID_IMFMediaTypeHandler, (void**)ppHandler);
        }

        return hr;
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

    // IMFMediaTypeHandler
    STDMETHODIMP GetCurrentMediaType(_Outptr_ IMFMediaType** ppMediaType)override
    {
        CAutoLock lock(&m_critSec);

        if (ppMediaType == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = CheckShutdown();

        if (SUCCEEDED(hr))
        {
            if (m_pCurrentType == NULL)
            {
                hr = MF_E_NOT_INITIALIZED;
            }
        }

        if (SUCCEEDED(hr))
        {
            *ppMediaType = m_pCurrentType.Get();
            (*ppMediaType)->AddRef();
        }

        return hr;
    }

    STDMETHODIMP GetMajorType(__RPC__out GUID* pguidMajorType)override
    {
        if (pguidMajorType == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = CheckShutdown();
        if (FAILED(hr))
        {
            return hr;
        }

        if (m_pCurrentType == NULL)
        {
            return MF_E_NOT_INITIALIZED;
        }

        return m_pCurrentType->GetGUID(MF_MT_MAJOR_TYPE, pguidMajorType);
    }

    STDMETHODIMP GetMediaTypeByIndex(DWORD dwIndex, _Outptr_ IMFMediaType** ppType)override
    {
        HRESULT hr = S_OK;

        do
        {
            if (ppType == NULL)
            {
                hr = E_POINTER;
                break;
            }

            hr = CheckShutdown();
            if (FAILED(hr))
            {
                break;
            }

            if (dwIndex >= s_dwNumVideoFormats)
            {
                hr = MF_E_NO_MORE_TYPES;
                break;
            }

            IMFMediaType* pVideoMediaType = NULL;

            do
            {
                hr = MFCreateMediaType(&pVideoMediaType);
                if (FAILED(hr))
                {
                    break;
                }

                hr = pVideoMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                if (FAILED(hr))
                {
                    break;
                }

                hr = pVideoMediaType->SetGUID(MF_MT_SUBTYPE, *(s_pVideoFormats[dwIndex]));
                if (FAILED(hr))
                {
                    break;
                }

                pVideoMediaType->AddRef();
                *ppType = pVideoMediaType;
            } while (FALSE);

            //SafeRelease(pVideoMediaType);

            if (FAILED(hr))
            {
                break;
            }
        } while (FALSE);

        return hr;
    }

    STDMETHODIMP GetMediaTypeCount(__RPC__out DWORD* pdwTypeCount)override
    {
        HRESULT hr = S_OK;

        do
        {
            if (pdwTypeCount == NULL)
            {
                hr = E_POINTER;
                break;
            }

            hr = CheckShutdown();
            if (FAILED(hr))
            {
                break;
            }

            *pdwTypeCount = s_dwNumVideoFormats;
        } while (FALSE);

        return hr;
    }

    STDMETHODIMP IsMediaTypeSupported(IMFMediaType* pMediaType, _Outptr_opt_result_maybenull_ IMFMediaType** ppMediaType)override
    {
        HRESULT hr = S_OK;
        GUID subType = GUID_NULL;

        do
        {
            hr = CheckShutdown();
            if (FAILED(hr))
            {
                break;
            }

            if (pMediaType == NULL)
            {
                hr = E_POINTER;
                break;
            }

            hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subType);
            if (FAILED(hr))
            {
                break;
            }

            hr = MF_E_INVALIDMEDIATYPE; // This will be set to OK if we find the subtype is accepted

            for (DWORD i = 0; i < s_dwNumVideoFormats; i++)
            {
                if (subType == (*s_pVideoFormats[i]))
                {
                    hr = S_OK;
                    break;
                }
            }

            if (FAILED(hr))
            {
                break;
            }

            for (DWORD i = 0; i < ARRAYSIZE(s_DXGIFormatMapping); i++)
            {
                const FormatEntry& e = s_DXGIFormatMapping[i];
                if (e.Subtype == subType)
                {
                    m_dxgiFormat = e.DXGIFormat;
                    break;
                }
            }

            /*
            hr = m_pPresenter->IsMediaTypeSupported(pMediaType, m_dxgiFormat);
            if (FAILED(hr))
            {
                break;
            }
            */
        } while (FALSE);

        // We don't return any "close match" types.
        if (ppMediaType)
        {
            *ppMediaType = NULL;
        }

        return hr;
    }

    STDMETHODIMP SetCurrentMediaType(IMFMediaType* pMediaType)override
    {
        if (pMediaType == NULL)
        {
            return E_POINTER;
        }

        HRESULT hr = S_OK;
        MFRatio fps = { 0, 0 };
        GUID guidSubtype = GUID_NULL;

        CAutoLock lock(&m_critSec);

        do
        {
            hr = CheckShutdown();
            if (FAILED(hr))
            {
                break;
            }

            hr = ValidateOperation(m_state, StreamOperation::OpSetMediaType);
            if (FAILED(hr))
            {
                break;
            }

            hr = IsMediaTypeSupported(pMediaType, NULL);
            if (FAILED(hr))
            {
                break;
            }

            m_pCurrentType = pMediaType;

            pMediaType->GetGUID(MF_MT_SUBTYPE, &guidSubtype);

            if ((guidSubtype == MFVideoFormat_NV12) ||
                (guidSubtype == MFVideoFormat_YV12) ||
                (guidSubtype == MFVideoFormat_IYUV) ||
                (guidSubtype == MFVideoFormat_YVU9) ||
                (guidSubtype == MFVideoFormat_I420))
            {
                m_imageBytesPP.Numerator = 3;
                m_imageBytesPP.Denominator = 2;
            }
            else if ((guidSubtype == MFVideoFormat_YUY2) ||
                (guidSubtype == MFVideoFormat_RGB555) ||
                (guidSubtype == MFVideoFormat_RGB565) ||
                (guidSubtype == MFVideoFormat_UYVY) ||
                (guidSubtype == MFVideoFormat_YVYU) ||
                (guidSubtype == MEDIASUBTYPE_V216))
            {
                m_imageBytesPP.Numerator = 2;
                m_imageBytesPP.Denominator = 1;
            }
            else if (guidSubtype == MFVideoFormat_RGB24)
            {
                m_imageBytesPP.Numerator = 3;
                m_imageBytesPP.Denominator = 1;
            }
            else if (guidSubtype == MFVideoFormat_RGB32)
            {
                m_imageBytesPP.Numerator = 4;
                m_imageBytesPP.Denominator = 1;
            }
            else if (guidSubtype == MFVideoFormat_v410)
            {
                m_imageBytesPP.Numerator = 5;
                m_imageBytesPP.Denominator = 4;
            }
            else // includes:
                 // MFVideoFormat_RGB8
                 // MFVideoFormat_AYUV
                 // MFVideoFormat_NV11
            {
                // This is just a fail-safe
                m_imageBytesPP.Numerator = 1;
                m_imageBytesPP.Denominator = 1;
            }

            /*
            pMediaType->GetUINT32(MF_MT_INTERLACE_MODE, &m_unInterlaceMode);

            // Set the frame rate on the scheduler.
            if (SUCCEEDED(GetFrameRate(pMediaType, &fps)) && (fps.Numerator != 0) && (fps.Denominator != 0))
            {
                if (MFVideoInterlace_FieldInterleavedUpperFirst == m_unInterlaceMode ||
                    MFVideoInterlace_FieldInterleavedLowerFirst == m_unInterlaceMode ||
                    MFVideoInterlace_FieldSingleUpper == m_unInterlaceMode ||
                    MFVideoInterlace_FieldSingleLower == m_unInterlaceMode ||
                    MFVideoInterlace_MixedInterlaceOrProgressive == m_unInterlaceMode)
                {
                    fps.Numerator *= 2;
                }

                m_pScheduler->SetFrameRate(fps);
            }
            else
            {
                // NOTE: The mixer's proposed type might not have a frame rate, in which case
                // we'll use an arbitary default. (Although it's unlikely the video source
                // does not have a frame rate.)
                m_pScheduler->SetFrameRate(s_DefaultFrameRate);
            }

            // Update the required sample count based on the media type (progressive vs. interlaced)
            if (m_unInterlaceMode == MFVideoInterlace_Progressive)
            {
                // XVP will hold on to 1 sample but that's the same sample we will internally hold on to
                hr = SetUINT32(MF_SA_REQUIRED_SAMPLE_COUNT, SAMPLE_QUEUE_HIWATER_THRESHOLD);
            }
            else
            {
                // Assume we will need a maximum of 3 backward reference frames for deinterlacing
                // However, one of the frames is "shared" with SVR
                hr = SetUINT32(MF_SA_REQUIRED_SAMPLE_COUNT, SAMPLE_QUEUE_HIWATER_THRESHOLD + MAX_PAST_FRAMES - 1);
            }

            if (SUCCEEDED(hr))
            {
                hr = m_pPresenter->SetCurrentMediaType(pMediaType);
                if (FAILED(hr))
                {
                    break;
                }
            }
            */

            if (State::State_Started != m_state && State::State_Paused != m_state)
            {
                m_state = State::State_Ready;
            }
            else
            {
                //Flush all current samples in the Queue as this is a format change
                hr = Flush();
            }
        } while (FALSE);

        return hr;
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
        auto p=new CustomVideoStreamSink(STREAM_ID, m_csStreamSinkAndScheduler, this);

        auto hr=p->QueryInterface(IID_IMFStreamSink, &m_pStream);

        if (p) {
            p->Release();
            p = nullptr;
        }

        return hr;
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
