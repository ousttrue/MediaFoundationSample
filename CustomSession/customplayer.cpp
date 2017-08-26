// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "CustomPlayer.h"
#include <assert.h>

#pragma comment(lib, "shlwapi")


//  Create a media source from a URL.
static Microsoft::WRL::ComPtr<IMFMediaSource> CreateMediaSource(PCWSTR sURL)
{
    // Create the source resolver.
    Microsoft::WRL::ComPtr<IMFSourceResolver> pSourceResolver;
    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Use the source resolver to create the media source.

    // Note: For simplicity this sample uses the synchronous method to create 
    // the media source. However, creating a media source can take a noticeable
    // amount of time, especially for a network source. For a more responsive 
    // UI, use the asynchronous BeginCreateObjectFromURL method.

    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
    Microsoft::WRL::ComPtr<IUnknown> pSource;
    hr = pSourceResolver->CreateObjectFromURL(
        sURL,                       // URL of the source.
        MF_RESOLUTION_MEDIASOURCE,  // Create a source object.
        NULL,                       // Optional property store.
        &ObjectType,        // Receives the created object type. 
        &pSource            // Receives a pointer to the media source.
    );
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Get the IMFMediaSource interface from the media source.
    Microsoft::WRL::ComPtr<IMFMediaSource> ppSource;
    
    hr = pSource.As(&ppSource);
    if (FAILED(hr))
    {
        return nullptr;
    }

    return ppSource;
}

static  Microsoft::WRL::ComPtr<IMFTopologyNode> CreateSourceNode(
    const Microsoft::WRL::ComPtr<IMFMediaSource> &pSource,          // Media source.
    const Microsoft::WRL::ComPtr<IMFPresentationDescriptor> &pPD,   // Presentation descriptor.
    const Microsoft::WRL::ComPtr<IMFStreamDescriptor> &pSD          // Stream descriptor.
    )
{
    // Create the node.
    Microsoft::WRL::ComPtr<IMFTopologyNode> pNode;
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Set the attributes.
    hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pSource.Get());
    if (FAILED(hr))
    {
        return nullptr;
    }

    hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD.Get());
    if (FAILED(hr))
    {
        return nullptr;
    }

    hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD.Get());
    if (FAILED(hr))
    {
        return nullptr;
    }

    return pNode;
}

//  Create an activation object for a renderer, based on the stream media type.
static Microsoft::WRL::ComPtr<IMFActivate> CreateMediaSinkActivate(
    const Microsoft::WRL::ComPtr<IMFStreamDescriptor> &pSourceSD,     // Pointer to the stream descriptor.
    HWND hVideoWindow                  // Handle to the video clipping window.
)
{
    // Get the media type handler for the stream.
    Microsoft::WRL::ComPtr<IMFMediaTypeHandler> pHandler;
    HRESULT hr = pSourceSD->GetMediaTypeHandler(&pHandler);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Get the major media type.
    GUID guidMajorType;
    hr = pHandler->GetMajorType(&guidMajorType);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Create an IMFActivate object for the renderer, based on the media type.
    if (MFMediaType_Audio == guidMajorType)
    {
        // Create the audio renderer.
        Microsoft::WRL::ComPtr<IMFActivate> pActivate;
        hr = MFCreateAudioRendererActivate(&pActivate);
        if (FAILED(hr))
        {
            return nullptr;
        }
        return pActivate;
    }
    else if (MFMediaType_Video == guidMajorType)
    {
        // Create the video renderer.
        Microsoft::WRL::ComPtr<IMFActivate> pActivate;
        hr = MFCreateVideoRendererActivate(hVideoWindow, &pActivate);
        if (FAILED(hr))
        {
            return nullptr;
        }
        return pActivate;
    }
    else
    {
        // Unknown stream type. 
        return nullptr;
        // Optionally, you could deselect this stream instead of failing.
    }
}

static Microsoft::WRL::ComPtr<IMFTopologyNode> CreateOutputNode(
    const Microsoft::WRL::ComPtr<IMFActivate> &pActivate,     // Media sink activation object.
    DWORD dwId                  // Identifier of the stream sink.
)
{
    // Create the node.
    Microsoft::WRL::ComPtr<IMFTopologyNode> pNode;
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Set the object pointer.
    hr = pNode->SetObject(pActivate.Get());
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Set the stream sink ID attribute.
    hr = pNode->SetUINT32(MF_TOPONODE_STREAMID, dwId);
    if (FAILED(hr))
    {
        return nullptr;
    }

    hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
    if (FAILED(hr))
    {
        return nullptr;
    }

    return pNode;
}


//  Add a topology branch for one stream.
//
//  For each stream, this function does the following:
//
//    1. Creates a source node associated with the stream. 
//    2. Creates an output node for the renderer. 
//    3. Connects the two nodes.
//
//  The media session will add any decoders that are needed.

static HRESULT AddBranchToPartialTopology(
    const Microsoft::WRL::ComPtr<IMFTopology> &pTopology,         // Topology.
    const Microsoft::WRL::ComPtr<IMFMediaSource> &pSource,        // Media source.
    const Microsoft::WRL::ComPtr<IMFPresentationDescriptor> &pPD, // Presentation descriptor.
    DWORD iStream,                  // Stream index.
    HWND hVideoWnd)                 // Window for video playback.
{
    BOOL fSelected = FALSE;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> pSD;
    HRESULT hr = pPD->GetStreamDescriptorByIndex(iStream, &fSelected, &pSD);
    if (FAILED(hr))
    {
        return hr;
    }
    if (!fSelected)
    {
        // else: If not selected, don't add the branch. 
        return E_FAIL;
    }

    auto pSourceNode = CreateSourceNode(pSource, pPD, pSD);
    if (!pSourceNode) 
    {
        return E_FAIL;
    }

    // Add the node to the topology.
    hr = pTopology->AddNode(pSourceNode.Get());
    if (FAILED(hr))
    {
        return hr;
    }

    // Create the media sink activation object.
    auto pSinkActivate = CreateMediaSinkActivate(pSD, hVideoWnd);
    if (!pSinkActivate)
    {
        return E_FAIL;
    }

    auto pOutputNode = CreateOutputNode(pSinkActivate, 0);
    if (!pOutputNode) {
        return E_FAIL;
    }

    // Add the node to the topology.
    hr = pTopology->AddNode(pOutputNode.Get());
    if (FAILED(hr))
    {
        return hr;
    }

    // Connect the source node to the output node.
    hr = pSourceNode->ConnectOutput(0, pOutputNode.Get(), 0);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}


//  Create a playback topology from a media source.
static Microsoft::WRL::ComPtr<IMFTopology> CreatePlaybackTopology(
    const Microsoft::WRL::ComPtr<IMFMediaSource> &pSource,          // Media source.
    const Microsoft::WRL::ComPtr<IMFPresentationDescriptor> &pPD,   // Presentation descriptor.
    HWND hVideoWnd                   // Video window.
)
{
    // Create a new topology.
    Microsoft::WRL::ComPtr<IMFTopology> pTopology;
    HRESULT hr = MFCreateTopology(&pTopology);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // Get the number of streams in the media source.
    DWORD cSourceStreams = 0;
    hr = pPD->GetStreamDescriptorCount(&cSourceStreams);
    if (FAILED(hr))
    {
        return nullptr;
    }

    // For each stream, create the topology nodes and add them to the topology.
    for (DWORD i = 0; i < cSourceStreams; i++)
    {
        hr = AddBranchToPartialTopology(pTopology, pSource, pPD, i, hVideoWnd);
        if (FAILED(hr))
        {
            return nullptr;
        }
    }

    return pTopology;
}

static Microsoft::WRL::ComPtr<IMFPresentationDescriptor> GetPresentationDescriptor(
    const Microsoft::WRL::ComPtr<IMFMediaEvent> &pEvent
)
{
    PROPVARIANT var;
    HRESULT hr = pEvent->GetValue(&var);
    if (FAILED(hr))
    {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> pPD;
    if (var.vt == VT_UNKNOWN)
    {
        hr = MF_E_INVALIDTYPE;
    }
    else {
        hr = var.punkVal->QueryInterface(IID_PPV_ARGS(&pPD));
    }
    PropVariantClear(&var);

    // Get the presentation descriptor from the event.
    if (FAILED(hr))
    {
        return nullptr;
    }
    return pPD;
}

//  Static class method to create the CPlayer object.

HRESULT CPlayer::CreateInstance(
        HWND hVideo,                  // Video window.
        HWND hEvent,                  // Window to receive notifications.
        CPlayer **ppPlayer)           // Receives a pointer to the CPlayer object.
{
    if (ppPlayer == NULL)
    {
        return E_POINTER;
    }

    CPlayer *pPlayer = new (std::nothrow) CPlayer(hVideo, hEvent);
    if (pPlayer == NULL)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pPlayer->Initialize();
    if (SUCCEEDED(hr))
    {
        *ppPlayer = pPlayer;
    }
    else
    {
        pPlayer->Release();
    }
    return hr;
}

HRESULT CPlayer::Initialize()
{
    // Start up Media Foundation platform.
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr))
    {
        m_hCloseEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (m_hCloseEvent == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    return hr;
}

CPlayer::CPlayer(HWND hVideo, HWND hEvent) : 
    m_pSession(NULL),
    m_pSource(NULL),
    m_pVideoDisplay(NULL),
    m_hwndVideo(hVideo),
    m_hwndEvent(hEvent),
    m_state(Closed),
    m_hCloseEvent(NULL),
    m_nRefCount(1)
{
}

CPlayer::~CPlayer()
{
    assert(m_pSession == NULL);  
    // If FALSE, the app did not call Shutdown().

    // When CPlayer calls IMediaEventGenerator::BeginGetEvent on the
    // media session, it causes the media session to hold a reference 
    // count on the CPlayer. 

    // This creates a circular reference count between CPlayer and the 
    // media session. Calling Shutdown breaks the circular reference 
    // count.

    // If CreateInstance fails, the application will not call 
    // Shutdown. To handle that case, call Shutdown in the destructor. 

    Shutdown();
}

// IUnknown methods

HRESULT CPlayer::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = 
    {
        QITABENT(CPlayer, IMFAsyncCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG CPlayer::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

ULONG CPlayer::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

//  Open a URL for playback.
HRESULT CPlayer::OpenURL(const WCHAR *sURL)
{
    // 1. Create a new media session.
    // 2. Create the media source.
    // 3. Create the topology.
    // 4. Queue the topology [asynchronous]
    // 5. Start playback [asynchronous - does not happen in this method.]

    m_state = Closed;

    // Create the media session.
    HRESULT hr = CreateSession();
    if (FAILED(hr))
    {
        return hr;
    }

    // Create the media source.
    m_pSource = CreateMediaSource(sURL);
    if (!m_pSource)
    {
        return E_FAIL;
    }

    // Create the presentation descriptor for the media source.
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> pSourcePD;
    hr = m_pSource->CreatePresentationDescriptor(&pSourcePD);
    if (FAILED(hr))
    {
        return hr;
    }

    // Create a partial topology.
    auto  pTopology = CreatePlaybackTopology(m_pSource.Get(), pSourcePD, m_hwndVideo);
    if (!pTopology)
    {
        return E_FAIL;
    }

    // Set the topology on the media session.
    hr = m_pSession->SetTopology(0, pTopology.Get());
    if (FAILED(hr))
    {
        return hr;
    }

    m_state = OpenPending;

    // If SetTopology succeeds, the media session will queue an 
    // MESessionTopologySet event.

    return S_OK;
}

//  Pause playback.
HRESULT CPlayer::Pause()    
{
    if (m_state != Started)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL || m_pSource == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pSession->Pause();
    if (SUCCEEDED(hr))
    {
        m_state = Paused;
    }

    return hr;
}

// Stop playback.
HRESULT CPlayer::Stop()
{
    if (m_state != Started && m_state != Paused)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pSession->Stop();
    if (SUCCEEDED(hr))
    {
        m_state = Stopped;
    }
    return hr;
}

//  Repaint the video window. Call this method on WM_PAINT.

HRESULT CPlayer::Repaint()
{
    if (m_pVideoDisplay)
    {
        return m_pVideoDisplay->RepaintVideo();
    }
    else
    {
        return S_OK;
    }
}

//  Resize the video rectangle.
//
//  Call this method if the size of the video window changes.

HRESULT CPlayer::ResizeVideo(WORD width, WORD height)
{
    if (m_pVideoDisplay)
    {
        // Set the destination rectangle.
        // Leave the default source rectangle (0,0,1,1).

        RECT rcDest = { 0, 0, width, height };

        return m_pVideoDisplay->SetVideoPosition(NULL, &rcDest);
    }
    else
    {
        return S_OK;
    }
}

//  Callback for the asynchronous BeginGetEvent method.

HRESULT CPlayer::Invoke(IMFAsyncResult *pResult)
{
    // Get the event from the event queue.
    Microsoft::WRL::ComPtr<IMFMediaEvent> pEvent;
    HRESULT hr = m_pSession->EndGetEvent(pResult, &pEvent);
    if (FAILED(hr))
    {
        return hr;
    }

    // Get the event type. 
    MediaEventType meType = MEUnknown;  // Event type
    hr = pEvent->GetType(&meType);
    if (FAILED(hr))
    {
        return hr;
    }

    if (meType == MESessionClosed)
    {
        // The session was closed. 
        // The application is waiting on the m_hCloseEvent event handle. 
        SetEvent(m_hCloseEvent);
    }
    else
    {
        // For all other events, get the next event in the queue.
        hr = m_pSession->BeginGetEvent(this, NULL);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    // Check the application state. 

    // If a call to IMFMediaSession::Close is pending, it means the 
    // application is waiting on the m_hCloseEvent event and
    // the application's message loop is blocked. 

    // Otherwise, post a private window message to the application. 

    if (m_state != Closing)
    {
        // Leave a reference count on the event.
        pEvent.Get()->AddRef();

        PostMessage(m_hwndEvent, WM_APP_PLAYER_EVENT, 
                (WPARAM)pEvent.Get(), (LPARAM)meType);
    }

    return S_OK;
}

HRESULT CPlayer::HandleEvent(UINT_PTR pEventPtr)
{
    Microsoft::WRL::ComPtr<IMFMediaEvent> pEvent = (IMFMediaEvent*)pEventPtr;
    if (pEvent == NULL)
    {
        return E_POINTER;
    }

    // Get the event type.
    MediaEventType meType = MEUnknown;
    HRESULT hr = pEvent->GetType(&meType);
    if (FAILED(hr))
    {
        return hr;
    }

    // Get the event status. If the operation that triggered the event 
    // did not succeed, the status is a failure code.
    HRESULT hrStatus = S_OK;
    hr = pEvent->GetStatus(&hrStatus);

    // Check if the async operation succeeded.
    if (SUCCEEDED(hr) && FAILED(hrStatus)) 
    {
        hr = hrStatus;
    }
    if (FAILED(hr))
    {
        return hr;
    }

    switch(meType)
    {
        case MESessionTopologyStatus:
            hr = OnTopologyStatus(pEvent);
            break;

        case MEEndOfPresentation:
            hr = OnPresentationEnded(pEvent);
            break;

        case MENewPresentation:
            hr = OnNewPresentation(pEvent);
            break;

        default:
            hr = OnSessionEvent(pEvent, meType);
            break;
    }

    return hr;
}

//  Release all resources held by this object.
HRESULT CPlayer::Shutdown()
{
    // Close the session
    HRESULT hr = CloseSession();

    // Shutdown the Media Foundation platform
    MFShutdown();

    if (m_hCloseEvent)
    {
        CloseHandle(m_hCloseEvent);
        m_hCloseEvent = NULL;
    }

    return hr;
}

/// Protected methods

HRESULT CPlayer::OnTopologyStatus(const Microsoft::WRL::ComPtr<IMFMediaEvent> &pEvent)
{
    UINT32 status; 

    HRESULT hr = pEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);
    if (SUCCEEDED(hr) && (status == MF_TOPOSTATUS_READY))
    {
        // Get the IMFVideoDisplayControl interface from EVR. This call is
        // expected to fail if the media file does not have a video stream.

        (void)MFGetService(m_pSession.Get(), MR_VIDEO_RENDER_SERVICE, 
                IID_PPV_ARGS(&m_pVideoDisplay));

        hr = StartPlayback();
    }
    return hr;
}


//  Handler for MEEndOfPresentation event.
HRESULT CPlayer::OnPresentationEnded(const Microsoft::WRL::ComPtr<IMFMediaEvent> &pEvent)
{
    // The session puts itself into the stopped state automatically.
    m_state = Stopped;
    return S_OK;
}

//  Handler for MENewPresentation event.
//
//  This event is sent if the media source has a new presentation, which 
//  requires a new topology. 

HRESULT CPlayer::OnNewPresentation(const Microsoft::WRL::ComPtr<IMFMediaEvent> &pEvent)
{
    auto pPD = GetPresentationDescriptor(pEvent);
    if (!pPD) {
        return E_FAIL;
    }

    // Create a partial topology.
    auto pTopology = CreatePlaybackTopology(m_pSource.Get(), pPD,  m_hwndVideo);
    if (!pTopology)
    {
        return E_FAIL;
    }

    // Set the topology on the media session.
    auto hr = m_pSession->SetTopology(0, pTopology.Get());
    if (FAILED(hr))
    {
        return hr;
    }

    m_state = OpenPending;
    return S_OK;
}

//  Create a new instance of the media session.
HRESULT CPlayer::CreateSession()
{
    // Close the old session, if any.
    HRESULT hr = CloseSession();
    if (FAILED(hr))
    {
        goto done;
    }

    assert(m_state == Closed);

    // Create the media session.
    hr = MFCreateMediaSession(NULL, &m_pSession);
    if (FAILED(hr))
    {
        goto done;
    }

    // Start pulling events from the media session
    hr = m_pSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
    if (FAILED(hr))
    {
        goto done;
    }

    m_state = Ready;

done:
    return hr;
}

//  Close the media session. 
HRESULT CPlayer::CloseSession()
{
    //  The IMFMediaSession::Close method is asynchronous, but the 
    //  CPlayer::CloseSession method waits on the MESessionClosed event.
    //  
    //  MESessionClosed is guaranteed to be the last event that the 
    //  media session fires.

    HRESULT hr = S_OK;

    m_pVideoDisplay.Reset();

    // First close the media session.
    if (m_pSession)
    {
        DWORD dwWaitResult = 0;

        m_state = Closing;

        hr = m_pSession->Close();
        // Wait for the close operation to complete
        if (SUCCEEDED(hr))
        {
            dwWaitResult = WaitForSingleObject(m_hCloseEvent, 5000);
            if (dwWaitResult == WAIT_TIMEOUT)
            {
                assert(FALSE);
            }
            // Now there will be no more events from this session.
        }
    }

    // Complete shutdown operations.
    if (SUCCEEDED(hr))
    {
        // Shut down the media source. (Synchronous operation, no events.)
        if (m_pSource)
        {
            (void)m_pSource->Shutdown();
        }
        // Shut down the media session. (Synchronous operation, no events.)
        if (m_pSession)
        {
            (void)m_pSession->Shutdown();
        }
    }

    m_pSession.Reset();
    m_state = Closed;
    return hr;
}

//  Start playback from the current position. 
HRESULT CPlayer::StartPlayback()
{
    assert(m_pSession != NULL);

    PROPVARIANT varStart;
    PropVariantInit(&varStart);

    HRESULT hr = m_pSession->Start(&GUID_NULL, &varStart);
    if (SUCCEEDED(hr))
    {
        // Note: Start is an asynchronous operation. However, we
        // can treat our state as being already started. If Start
        // fails later, we'll get an MESessionStarted event with
        // an error code, and we will update our state then.
        m_state = Started;
    }
    PropVariantClear(&varStart);
    return hr;
}

//  Start playback from paused or stopped.
HRESULT CPlayer::Play()
{
    if (m_state != Paused && m_state != Stopped)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL || m_pSource == NULL)
    {
        return E_UNEXPECTED;
    }
    return StartPlayback();
}
