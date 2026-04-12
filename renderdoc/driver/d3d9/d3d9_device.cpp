/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "d3d9_device.h"
#include "core/core.h"
#include "serialise/rdcfile.h"
#include "strings/string_utils.h"

///////////////////////////////////////////////////////////////////////////
// Constructor (capture mode)
///////////////////////////////////////////////////////////////////////////
WrappedIDirect3DDevice9::WrappedIDirect3DDevice9(IDirect3DDevice9 *real, WrappedIDirect3D9 *d3d9,
                                                 D3DPRESENT_PARAMETERS *pPresentationParameters)
    : m_pDevice(real),
      m_pD3D9(d3d9),
      m_RenderState(D3D9RenderState::Empty),
      m_ScratchSerialiser(new StreamWriter(1024), Ownership::Stream)
{
  m_RefCount = 1;
  m_FrameCounter = 0;
  m_StateBlockRecording = false;
  m_Replay = NULL;
  m_DeviceRecord = NULL;

  m_SectionVersion = D3D9InitParams::CurrentVersion;

  m_StructuredFile = m_StoredStructuredData = new SDFile;

  if(RenderDoc::Inst().IsReplayApp())
  {
    m_State = CaptureState::LoadingReplaying;
    ResourceIDGen::SetReplayResourceIDs();
  }
  else
  {
    m_State = CaptureState::BackgroundCapturing;
  }

  m_ResourceManager = new D3D9ResourceManager(m_State, this);

  m_ResourceID = ResourceIDGen::GetNewUniqueID();

  uint32_t flags = WriteSerialiser::ChunkDuration | WriteSerialiser::ChunkTimestamp |
                   WriteSerialiser::ChunkThreadID;

  if(RenderDoc::Inst().GetCaptureOptions().captureCallstacks)
    flags |= WriteSerialiser::ChunkCallstack;

  m_ScratchSerialiser.SetChunkMetadataRecording(flags);
  m_ScratchSerialiser.SetVersion(D3D9InitParams::CurrentVersion);
  m_ScratchSerialiser.SetUserData(GetResourceManager());

  if(pPresentationParameters)
    m_InitParams.PresentationParameters = *pPresentationParameters;

  if(!RenderDoc::Inst().IsReplayApp())
  {
    m_DeviceRecord = GetResourceManager()->AddResourceRecord(m_ResourceID);
    m_DeviceRecord->DataInSerialiser = false;
    m_DeviceRecord->InternalResource = true;
    m_DeviceRecord->Length = 0;
    m_DeviceRecord->NumSubResources = 0;
    m_DeviceRecord->SubResources = NULL;

    RenderDoc::Inst().AddDeviceFrameCapturer((IDirect3DDevice9 *)this, this);

    RDCLOG("Created D3D9 device.");
  }
}

///////////////////////////////////////////////////////////////////////////
// Constructor (replay mode)
///////////////////////////////////////////////////////////////////////////
WrappedIDirect3DDevice9::WrappedIDirect3DDevice9(IDirect3DDevice9 *real,
                                                 const D3D9InitParams &params)
    : m_pDevice(real),
      m_pD3D9(NULL),
      m_RenderState(D3D9RenderState::Empty),
      m_ScratchSerialiser(new StreamWriter(1024), Ownership::Stream)
{
  m_RefCount = 1;
  m_FrameCounter = 0;
  m_StateBlockRecording = false;
  m_Replay = NULL;
  m_DeviceRecord = NULL;

  m_SectionVersion = D3D9InitParams::CurrentVersion;
  m_InitParams = params;

  m_StructuredFile = m_StoredStructuredData = new SDFile;

  m_State = CaptureState::LoadingReplaying;

  ResourceIDGen::SetReplayResourceIDs();

  m_ResourceManager = new D3D9ResourceManager(m_State, this);

  m_ResourceID = ResourceIDGen::GetNewUniqueID();

  uint32_t flags = WriteSerialiser::ChunkDuration | WriteSerialiser::ChunkTimestamp |
                   WriteSerialiser::ChunkThreadID;

  m_ScratchSerialiser.SetChunkMetadataRecording(flags);
  m_ScratchSerialiser.SetVersion(D3D9InitParams::CurrentVersion);
  m_ScratchSerialiser.SetUserData(GetResourceManager());
}

///////////////////////////////////////////////////////////////////////////
// Destructor
///////////////////////////////////////////////////////////////////////////
WrappedIDirect3DDevice9::~WrappedIDirect3DDevice9()
{
  RenderDoc::Inst().RemoveDeviceFrameCapturer((IDirect3DDevice9 *)this);

  SAFE_DELETE(m_StoredStructuredData);

  if(m_DeviceRecord)
  {
    RDCASSERT(m_DeviceRecord->GetRefCount() == 1);
    m_DeviceRecord->Delete(GetResourceManager());
  }

  m_ResourceManager->Shutdown();

  SAFE_DELETE(m_ResourceManager);
  SAFE_DELETE(m_Replay);

  SAFE_RELEASE(m_pDevice);
}

///////////////////////////////////////////////////////////////////////////
// IUnknown
///////////////////////////////////////////////////////////////////////////
ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9::AddRef()
{
  Atomic::Inc32(&m_RefCount);
  return (ULONG)m_RefCount;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DDevice9::Release()
{
  Atomic::Dec32(&m_RefCount);
  RDCASSERT(m_RefCount >= 0);
  if(m_RefCount == 0)
  {
    delete this;
    return 0;
  }
  return (ULONG)m_RefCount;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DDevice9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DDevice9))
  {
    *ppvObj = (IDirect3DDevice9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

///////////////////////////////////////////////////////////////////////////
// IFrameCapturer
///////////////////////////////////////////////////////////////////////////
void WrappedIDirect3DDevice9::StartFrameCapture(DeviceOwnedWindow devWnd)
{
  SCOPED_LOCK(m_D3DLock);

  if(!IsBackgroundCapturing(m_State))
    return;

  RDCLOG("Starting D3D9 capture");

  m_CaptureTimer.Restart();

  m_State = CaptureState::ActiveCapturing;

  FrameDescription frame;
  frame.frameNumber = ~0U;
  frame.captureTime = Timing::GetUnixTimestamp();
  m_CapturedFrames.push_back(frame);

  GetResourceManager()->ClearReferencedResources();

  GetResourceManager()->MarkResourceFrameReferenced(m_ResourceID, eFrameRef_PartialWrite);

  GetResourceManager()->FreeCaptureData();

  GetResourceManager()->PrepareInitialContents();
}

bool WrappedIDirect3DDevice9::EndFrameCapture(DeviceOwnedWindow devWnd)
{
  SCOPED_LOCK(m_D3DLock);

  if(!IsActiveCapturing(m_State))
    return true;

  RDCLOG("Finished D3D9 capture, Frame %u", m_CapturedFrames.back().frameNumber);

  EndCaptureFrame();

  RDCFile *rdc =
      RenderDoc::Inst().CreateRDC(RDCDriver::D3D9, m_CapturedFrames.back().frameNumber, {});

  StreamWriter *captureWriter = NULL;

  if(rdc)
  {
    SectionProperties props;
    props.flags = SectionFlags::LZ4Compressed;
    props.version = m_SectionVersion;
    props.type = SectionType::FrameCapture;

    captureWriter = rdc->WriteSection(props);
  }
  else
  {
    captureWriter = new StreamWriter(StreamWriter::InvalidStream);
  }

  uint64_t captureSectionSize = 0;

  {
    WriteSerialiser ser(captureWriter, Ownership::Stream);

    ser.SetChunkMetadataRecording(m_ScratchSerialiser.GetChunkMetadataRecording());
    ser.SetUserData(GetResourceManager());

    {
      SCOPED_SERIALISE_CHUNK(SystemChunk::DriverInit, sizeof(D3D9InitParams) + 16);
      SERIALISE_ELEMENT(m_InitParams);
    }

    RDCDEBUG("Inserting Resource Serialisers");

    GetResourceManager()->InsertReferencedChunks(ser);

    GetResourceManager()->InsertInitialContentsChunks(ser);

    RDCDEBUG("Creating Capture Scope");

    GetResourceManager()->Serialise_InitialContentsNeeded(ser);

    {
      SCOPED_SERIALISE_CHUNK(SystemChunk::CaptureScope, 16);
      Serialise_CaptureScope(ser);
    }

    if(m_DeviceRecord)
    {
      RDCDEBUG("Getting Resource Record");

      std::map<int64_t, Chunk *> recordlist;
      m_DeviceRecord->Insert(recordlist);

      RDCDEBUG("Flushing %u records to file serialiser", (uint32_t)recordlist.size());

      float num = float(recordlist.size());
      float idx = 0.0f;

      for(auto it = recordlist.begin(); it != recordlist.end(); ++it)
      {
        RenderDoc::Inst().SetProgress(CaptureProgress::SerialiseFrameContents, idx / num);
        idx += 1.0f;
        it->second->Write(ser);
      }

      RDCDEBUG("Done");
    }

    captureSectionSize = captureWriter->GetOffset();
  }

  RDCLOG("Captured D3D9 frame with %f MB capture section in %f seconds",
         double(captureSectionSize) / (1024.0 * 1024.0),
         m_CaptureTimer.GetMilliseconds() / 1000.0);

  RenderDoc::Inst().FinishCaptureWriting(rdc, m_CapturedFrames.back().frameNumber);

  m_State = CaptureState::BackgroundCapturing;

  GetResourceManager()->FreeCaptureData();

  GetResourceManager()->MarkUnwrittenResources();

  GetResourceManager()->ClearReferencedResources();

  GetResourceManager()->FreeInitialContents();

  return true;
}

bool WrappedIDirect3DDevice9::DiscardFrameCapture(DeviceOwnedWindow devWnd)
{
  SCOPED_LOCK(m_D3DLock);

  if(!IsActiveCapturing(m_State))
    return true;

  RDCLOG("Discarding D3D9 frame capture.");

  RenderDoc::Inst().FinishCaptureWriting(NULL, m_CapturedFrames.back().frameNumber);

  m_CapturedFrames.pop_back();

  GetResourceManager()->ClearReferencedResources();

  GetResourceManager()->FreeInitialContents();

  GetResourceManager()->FreeCaptureData();

  m_State = CaptureState::BackgroundCapturing;

  GetResourceManager()->MarkUnwrittenResources();

  return true;
}

uint32_t WrappedIDirect3DDevice9::SetObjectAnnotation(void *object, const char *key,
                                                       RENDERDOC_AnnotationType valueType,
                                                       uint32_t valueVectorWidth,
                                                       const RENDERDOC_AnnotationValue *value)
{
  // TODO: implement annotation support
  return 0;
}

uint32_t WrappedIDirect3DDevice9::SetCommandAnnotation(void *queueOrCommandBuffer, const char *key,
                                                        RENDERDOC_AnnotationType valueType,
                                                        uint32_t valueVectorWidth,
                                                        const RENDERDOC_AnnotationValue *value)
{
  // TODO: implement annotation support
  return 0;
}

///////////////////////////////////////////////////////////////////////////
// Frame helpers
///////////////////////////////////////////////////////////////////////////
void WrappedIDirect3DDevice9::BeginCaptureFrame()
{
  // TODO: serialise initial frame references
}

void WrappedIDirect3DDevice9::EndCaptureFrame()
{
  // TODO: serialise end-of-frame marker
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CaptureScope(SerialiserType &ser)
{
  SERIALISE_ELEMENT_LOCAL(frameNumber, m_CapturedFrames.back().frameNumber);

  if(IsReplayingAndReading())
  {
    // TODO: replay-side capture scope handling
  }

  return true;
}

template bool WrappedIDirect3DDevice9::Serialise_CaptureScope(ReadSerialiser &ser);
template bool WrappedIDirect3DDevice9::Serialise_CaptureScope(WriteSerialiser &ser);

///////////////////////////////////////////////////////////////////////////
// Initial state helpers
///////////////////////////////////////////////////////////////////////////
bool WrappedIDirect3DDevice9::Prepare_InitialState(IUnknown *res)
{
  // TODO: implement initial state preparation per resource type
  return true;
}

void WrappedIDirect3DDevice9::Create_InitialState(ResourceId id, IUnknown *live, bool hasData)
{
  // TODO: implement initial state creation during replay
}

void WrappedIDirect3DDevice9::Apply_InitialState(IUnknown *live, D3D9InitialContents &data)
{
  // TODO: implement initial state application during replay
}

///////////////////////////////////////////////////////////////////////////
// D3DPERF statics
///////////////////////////////////////////////////////////////////////////
void WrappedIDirect3DDevice9::SetMarker(uint32_t col, const wchar_t *name)
{
  // TODO: connect to event/annotation tracking
}

int WrappedIDirect3DDevice9::BeginEvent(uint32_t col, const wchar_t *name)
{
  // TODO: connect to event/annotation tracking
  return 0;
}

int WrappedIDirect3DDevice9::EndEvent()
{
  // TODO: connect to event/annotation tracking
  return 0;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Lifecycle / status
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::TestCooperativeLevel()
{
  return m_pDevice->TestCooperativeLevel();
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetAvailableTextureMem()
{
  return m_pDevice->GetAvailableTextureMem();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EvictManagedResources()
{
  return m_pDevice->EvictManagedResources();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDirect3D(IDirect3D9 **ppD3D9)
{
  if(ppD3D9 == NULL)
    return D3DERR_INVALIDCALL;

  // Return our wrapped D3D9 object, not the real one
  *ppD3D9 = (IDirect3D9 *)m_pD3D9;
  if(m_pD3D9)
    ((IUnknown *)m_pD3D9)->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDeviceCaps(D3DCAPS9 *pCaps)
{
  return m_pDevice->GetDeviceCaps(pCaps);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDisplayMode(UINT iSwapChain,
                                                                   D3DDISPLAYMODE *pMode)
{
  return m_pDevice->GetDisplayMode(iSwapChain, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetCreationParameters(
    D3DDEVICE_CREATION_PARAMETERS *pParameters)
{
  return m_pDevice->GetCreationParameters(pParameters);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Cursor (passthrough)
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetCursorProperties(
    UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 *pCursorBitmap)
{
  return m_pDevice->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetCursorPosition(int X, int Y, DWORD Flags)
{
  m_pDevice->SetCursorPosition(X, Y, Flags);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9::ShowCursor(BOOL bShow)
{
  return m_pDevice->ShowCursor(bShow);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Swap chain
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateAdditionalSwapChain(
    D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DSwapChain9 **pSwapChain)
{
  // TODO: wrap swap chain
  return m_pDevice->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetSwapChain(UINT iSwapChain,
                                                                 IDirect3DSwapChain9 **pSwapChain)
{
  return m_pDevice->GetSwapChain(iSwapChain, pSwapChain);
}

UINT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetNumberOfSwapChains()
{
  return m_pDevice->GetNumberOfSwapChains();
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Reset / Present
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Reset(
    D3DPRESENT_PARAMETERS *pPresentationParameters)
{
  // TODO: serialise Reset, re-wrap resources
  return m_pDevice->Reset(pPresentationParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Present(CONST RECT *pSourceRect,
                                                            CONST RECT *pDestRect,
                                                            HWND hDestWindowOverride,
                                                            CONST RGNDATA *pDirtyRegion)
{
  // TODO: serialise Present, increment frame counter, handle capture triggers
  m_FrameCounter++;
  return m_pDevice->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetBackBuffer(
    UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer)
{
  return m_pDevice->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRasterStatus(
    UINT iSwapChain, D3DRASTER_STATUS *pRasterStatus)
{
  return m_pDevice->GetRasterStatus(iSwapChain, pRasterStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetDialogBoxMode(BOOL bEnableDialogs)
{
  return m_pDevice->SetDialogBoxMode(bEnableDialogs);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetGammaRamp(UINT iSwapChain, DWORD Flags,
                                                              CONST D3DGAMMARAMP *pRamp)
{
  m_pDevice->SetGammaRamp(iSwapChain, Flags, pRamp);
}

void STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP *pRamp)
{
  m_pDevice->GetGammaRamp(iSwapChain, pRamp);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Resource creation (stubs, to be serialised)
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateTexture(
    UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle)
{
  // TODO: wrap created texture
  return m_pDevice->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture,
                                  pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVolumeTexture(
    UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle)
{
  // TODO: wrap created volume texture
  return m_pDevice->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool,
                                        ppVolumeTexture, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateCubeTexture(
    UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DCubeTexture9 **ppCubeTexture, HANDLE *pSharedHandle)
{
  // TODO: wrap created cube texture
  return m_pDevice->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture,
                                      pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexBuffer(
    UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
    IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle)
{
  // TODO: wrap created vertex buffer
  return m_pDevice->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateIndexBuffer(
    UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle)
{
  // TODO: wrap created index buffer
  return m_pDevice->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateRenderTarget(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
    DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  // TODO: wrap created render target
  return m_pDevice->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality,
                                       Lockable, ppSurface, pSharedHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateDepthStencilSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
    DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  // TODO: wrap created depth stencil surface
  return m_pDevice->CreateDepthStencilSurface(Width, Height, Format, MultiSample,
                                              MultisampleQuality, Discard, ppSurface,
                                              pSharedHandle);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Surface operations
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::UpdateSurface(
    IDirect3DSurface9 *pSourceSurface, CONST RECT *pSourceRect,
    IDirect3DSurface9 *pDestinationSurface, CONST POINT *pDestPoint)
{
  // TODO: unwrap surfaces, serialise
  return m_pDevice->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::UpdateTexture(
    IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture)
{
  // TODO: unwrap textures, serialise
  return m_pDevice->UpdateTexture(pSourceTexture, pDestinationTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderTargetData(
    IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface)
{
  return m_pDevice->GetRenderTargetData(pRenderTarget, pDestSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetFrontBufferData(
    UINT iSwapChain, IDirect3DSurface9 *pDestSurface)
{
  return m_pDevice->GetFrontBufferData(iSwapChain, pDestSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::StretchRect(
    IDirect3DSurface9 *pSourceSurface, CONST RECT *pSourceRect, IDirect3DSurface9 *pDestSurface,
    CONST RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
  // TODO: unwrap surfaces, serialise
  return m_pDevice->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ColorFill(IDirect3DSurface9 *pSurface,
                                                              CONST RECT *pRect, D3DCOLOR color)
{
  // TODO: unwrap surface, serialise
  return m_pDevice->ColorFill(pSurface, pRect, color);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateOffscreenPlainSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9 **ppSurface,
    HANDLE *pSharedHandle)
{
  // TODO: wrap created surface
  return m_pDevice->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface,
                                                pSharedHandle);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Render targets / depth
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetRenderTarget(
    DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget)
{
  // TODO: update shadow state, unwrap surface, serialise
  return m_pDevice->SetRenderTarget(RenderTargetIndex, pRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderTarget(
    DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget)
{
  // TODO: return wrapped surface
  return m_pDevice->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetDepthStencilSurface(
    IDirect3DSurface9 *pNewZStencil)
{
  // TODO: update shadow state, unwrap surface, serialise
  return m_pDevice->SetDepthStencilSurface(pNewZStencil);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDepthStencilSurface(
    IDirect3DSurface9 **ppZStencilSurface)
{
  // TODO: return wrapped surface
  return m_pDevice->GetDepthStencilSurface(ppZStencilSurface);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Scene
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::BeginScene()
{
  // TODO: serialise
  return m_pDevice->BeginScene();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EndScene()
{
  // TODO: serialise
  return m_pDevice->EndScene();
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Clear
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Clear(DWORD Count, CONST D3DRECT *pRects,
                                                          DWORD Flags, D3DCOLOR Color, float Z,
                                                          DWORD Stencil)
{
  // TODO: serialise
  return m_pDevice->Clear(Count, pRects, Flags, Color, Z, Stencil);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Transforms
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State,
                                                                 CONST D3DMATRIX *pMatrix)
{
  if(pMatrix && (UINT)State < D3D9_MAX_TRANSFORMS)
    m_RenderState.transforms[(UINT)State] = *pMatrix;

  // TODO: serialise
  return m_pDevice->SetTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE State,
                                                                 D3DMATRIX *pMatrix)
{
  if(pMatrix && (UINT)State < D3D9_MAX_TRANSFORMS)
  {
    *pMatrix = m_RenderState.transforms[(UINT)State];
    return D3D_OK;
  }
  return m_pDevice->GetTransform(State, pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE State,
                                                                      CONST D3DMATRIX *pMatrix)
{
  // TODO: update shadow state (multiply), serialise
  return m_pDevice->MultiplyTransform(State, pMatrix);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Viewport
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9 *pViewport)
{
  if(pViewport)
    m_RenderState.viewport = *pViewport;

  // TODO: serialise
  return m_pDevice->SetViewport(pViewport);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetViewport(D3DVIEWPORT9 *pViewport)
{
  if(pViewport)
  {
    *pViewport = m_RenderState.viewport;
    return D3D_OK;
  }
  return D3DERR_INVALIDCALL;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Material
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetMaterial(CONST D3DMATERIAL9 *pMaterial)
{
  if(pMaterial)
    m_RenderState.material = *pMaterial;

  // TODO: serialise
  return m_pDevice->SetMaterial(pMaterial);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetMaterial(D3DMATERIAL9 *pMaterial)
{
  if(pMaterial)
  {
    *pMaterial = m_RenderState.material;
    return D3D_OK;
  }
  return D3DERR_INVALIDCALL;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Lights
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetLight(DWORD Index,
                                                             CONST D3DLIGHT9 *pLight)
{
  if(pLight)
  {
    if(Index >= m_RenderState.lights.size())
      m_RenderState.lights.resize(Index + 1);
    m_RenderState.lights[Index].light = *pLight;
  }

  // TODO: serialise
  return m_pDevice->SetLight(Index, pLight);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetLight(DWORD Index, D3DLIGHT9 *pLight)
{
  if(pLight && Index < m_RenderState.lights.size())
  {
    *pLight = m_RenderState.lights[Index].light;
    return D3D_OK;
  }
  return m_pDevice->GetLight(Index, pLight);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::LightEnable(DWORD Index, BOOL Enable)
{
  if(Index >= m_RenderState.lights.size())
    m_RenderState.lights.resize(Index + 1);
  m_RenderState.lights[Index].enabled = (Enable != FALSE);

  // TODO: serialise
  return m_pDevice->LightEnable(Index, Enable);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetLightEnable(DWORD Index, BOOL *pEnable)
{
  if(pEnable && Index < m_RenderState.lights.size())
  {
    *pEnable = m_RenderState.lights[Index].enabled ? TRUE : FALSE;
    return D3D_OK;
  }
  return m_pDevice->GetLightEnable(Index, pEnable);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Clip planes
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetClipPlane(DWORD Index,
                                                                 CONST float *pPlane)
{
  if(pPlane && Index < D3D9_MAX_CLIP_PLANES)
    memcpy(m_RenderState.clipPlanes[Index], pPlane, sizeof(float) * 4);

  // TODO: serialise
  return m_pDevice->SetClipPlane(Index, pPlane);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetClipPlane(DWORD Index, float *pPlane)
{
  if(pPlane && Index < D3D9_MAX_CLIP_PLANES)
  {
    memcpy(pPlane, m_RenderState.clipPlanes[Index], sizeof(float) * 4);
    return D3D_OK;
  }
  return m_pDevice->GetClipPlane(Index, pPlane);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Render state
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE State,
                                                                   DWORD Value)
{
  if((UINT)State < D3D9_MAX_RENDER_STATES)
    m_RenderState.renderStates[(UINT)State] = Value;

  // TODO: serialise
  return m_pDevice->SetRenderState(State, Value);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderState(D3DRENDERSTATETYPE State,
                                                                   DWORD *pValue)
{
  if(pValue && (UINT)State < D3D9_MAX_RENDER_STATES)
  {
    *pValue = m_RenderState.renderStates[(UINT)State];
    return D3D_OK;
  }
  return m_pDevice->GetRenderState(State, pValue);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — State blocks
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateStateBlock(D3DSTATEBLOCKTYPE Type,
                                                                     IDirect3DStateBlock9 **ppSB)
{
  // TODO: wrap state block
  return m_pDevice->CreateStateBlock(Type, ppSB);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::BeginStateBlock()
{
  m_StateBlockRecording = true;
  // TODO: serialise
  return m_pDevice->BeginStateBlock();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EndStateBlock(IDirect3DStateBlock9 **ppSB)
{
  m_StateBlockRecording = false;
  // TODO: wrap state block, serialise
  return m_pDevice->EndStateBlock(ppSB);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Clip status
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetClipStatus(
    CONST D3DCLIPSTATUS9 *pClipStatus)
{
  // TODO: serialise
  return m_pDevice->SetClipStatus(pClipStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetClipStatus(D3DCLIPSTATUS9 *pClipStatus)
{
  return m_pDevice->GetClipStatus(pClipStatus);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Textures
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetTexture(DWORD Stage,
                                                               IDirect3DBaseTexture9 **ppTexture)
{
  // TODO: return wrapped texture
  return m_pDevice->GetTexture(Stage, ppTexture);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTexture(DWORD Stage,
                                                               IDirect3DBaseTexture9 *pTexture)
{
  // TODO: update shadow state, unwrap texture, serialise
  return m_pDevice->SetTexture(Stage, pTexture);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Texture stage state
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetTextureStageState(
    DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)
{
  if(pValue && Stage < D3D9_MAX_TEXTURE_STAGES && (UINT)Type < D3D9_MAX_TSS_STATES)
  {
    *pValue = m_RenderState.textureStageStates[Stage][(UINT)Type];
    return D3D_OK;
  }
  return m_pDevice->GetTextureStageState(Stage, Type, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTextureStageState(
    DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
  if(Stage < D3D9_MAX_TEXTURE_STAGES && (UINT)Type < D3D9_MAX_TSS_STATES)
    m_RenderState.textureStageStates[Stage][(UINT)Type] = Value;

  // TODO: serialise
  return m_pDevice->SetTextureStageState(Stage, Type, Value);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Sampler state
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetSamplerState(DWORD Sampler,
                                                                    D3DSAMPLERSTATETYPE Type,
                                                                    DWORD *pValue)
{
  if(pValue && Sampler < D3D9_TOTAL_SAMPLERS && (UINT)Type < D3D9_MAX_SAMPLER_STATES)
  {
    *pValue = m_RenderState.samplerStates[Sampler][(UINT)Type];
    return D3D_OK;
  }
  return m_pDevice->GetSamplerState(Sampler, Type, pValue);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetSamplerState(DWORD Sampler,
                                                                    D3DSAMPLERSTATETYPE Type,
                                                                    DWORD Value)
{
  if(Sampler < D3D9_TOTAL_SAMPLERS && (UINT)Type < D3D9_MAX_SAMPLER_STATES)
    m_RenderState.samplerStates[Sampler][(UINT)Type] = Value;

  // TODO: serialise
  return m_pDevice->SetSamplerState(Sampler, Type, Value);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Validate
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ValidateDevice(DWORD *pNumPasses)
{
  return m_pDevice->ValidateDevice(pNumPasses);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Palette (passthrough)
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPaletteEntries(
    UINT PaletteNumber, CONST PALETTEENTRY *pEntries)
{
  return m_pDevice->SetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPaletteEntries(UINT PaletteNumber,
                                                                      PALETTEENTRY *pEntries)
{
  return m_pDevice->GetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetCurrentTexturePalette(UINT PaletteNumber)
{
  return m_pDevice->SetCurrentTexturePalette(PaletteNumber);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetCurrentTexturePalette(UINT *PaletteNumber)
{
  return m_pDevice->GetCurrentTexturePalette(PaletteNumber);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Scissor rect
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetScissorRect(CONST RECT *pRect)
{
  if(pRect)
    m_RenderState.scissor = *pRect;

  // TODO: serialise
  return m_pDevice->SetScissorRect(pRect);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetScissorRect(RECT *pRect)
{
  if(pRect)
  {
    *pRect = m_RenderState.scissor;
    return D3D_OK;
  }
  return D3DERR_INVALIDCALL;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Software vertex processing
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetSoftwareVertexProcessing(BOOL bSoftware)
{
  m_RenderState.softwareVertexProcessing = bSoftware;

  // TODO: serialise
  return m_pDevice->SetSoftwareVertexProcessing(bSoftware);
}

BOOL STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetSoftwareVertexProcessing()
{
  return m_RenderState.softwareVertexProcessing;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — N-Patch mode
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetNPatchMode(float nSegments)
{
  m_RenderState.nPatchMode = nSegments;

  // TODO: serialise
  return m_pDevice->SetNPatchMode(nSegments);
}

float STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetNPatchMode()
{
  return m_RenderState.nPatchMode;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Draw calls
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,
                                                                  UINT StartVertex,
                                                                  UINT PrimitiveCount)
{
  // TODO: serialise
  return m_pDevice->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawIndexedPrimitive(
    D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices,
    UINT startIndex, UINT primCount)
{
  // TODO: serialise
  return m_pDevice->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex,
                                         NumVertices, startIndex, primCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawPrimitiveUP(
    D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData,
    UINT VertexStreamZeroStride)
{
  // TODO: serialise
  return m_pDevice->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData,
                                    VertexStreamZeroStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawIndexedPrimitiveUP(
    D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount,
    CONST void *pIndexData, D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData,
    UINT VertexStreamZeroStride)
{
  // TODO: serialise
  return m_pDevice->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices,
                                           PrimitiveCount, pIndexData, IndexDataFormat,
                                           pVertexStreamZeroData, VertexStreamZeroStride);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Process vertices (passthrough)
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ProcessVertices(
    UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9 *pDestBuffer,
    IDirect3DVertexDeclaration9 *pVertexDecl, DWORD Flags)
{
  return m_pDevice->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDecl,
                                    Flags);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Vertex declaration
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexDeclaration(
    CONST D3DVERTEXELEMENT9 *pVertexElements, IDirect3DVertexDeclaration9 **ppDecl)
{
  // TODO: wrap vertex declaration
  return m_pDevice->CreateVertexDeclaration(pVertexElements, ppDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexDeclaration(
    IDirect3DVertexDeclaration9 *pDecl)
{
  // TODO: update shadow state, unwrap, serialise
  return m_pDevice->SetVertexDeclaration(pDecl);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexDeclaration(
    IDirect3DVertexDeclaration9 **ppDecl)
{
  // TODO: return wrapped declaration
  return m_pDevice->GetVertexDeclaration(ppDecl);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — FVF
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetFVF(DWORD FVF)
{
  m_RenderState.FVF = FVF;

  // TODO: serialise
  return m_pDevice->SetFVF(FVF);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetFVF(DWORD *pFVF)
{
  if(pFVF)
  {
    *pFVF = m_RenderState.FVF;
    return D3D_OK;
  }
  return D3DERR_INVALIDCALL;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Vertex shader
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexShader(
    CONST DWORD *pFunction, IDirect3DVertexShader9 **ppShader)
{
  // TODO: wrap shader
  return m_pDevice->CreateVertexShader(pFunction, ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShader(
    IDirect3DVertexShader9 *pShader)
{
  // TODO: update shadow state, unwrap, serialise
  return m_pDevice->SetVertexShader(pShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShader(
    IDirect3DVertexShader9 **ppShader)
{
  // TODO: return wrapped shader
  return m_pDevice->GetVertexShader(ppShader);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Vertex shader constants
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantF(
    UINT StartRegister, CONST float *pConstantData, UINT Vector4fCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_F; i++)
      memcpy(m_RenderState.vsConstantsF[StartRegister + i], &pConstantData[i * 4],
             sizeof(float) * 4);
  }

  // TODO: serialise
  return m_pDevice->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShaderConstantF(
    UINT StartRegister, float *pConstantData, UINT Vector4fCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_F; i++)
      memcpy(&pConstantData[i * 4], m_RenderState.vsConstantsF[StartRegister + i],
             sizeof(float) * 4);
    return D3D_OK;
  }
  return m_pDevice->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantI(
    UINT StartRegister, CONST int *pConstantData, UINT Vector4iCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_I; i++)
      memcpy(m_RenderState.vsConstantsI[StartRegister + i], &pConstantData[i * 4], sizeof(int) * 4);
  }

  // TODO: serialise
  return m_pDevice->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShaderConstantI(
    UINT StartRegister, int *pConstantData, UINT Vector4iCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_I; i++)
      memcpy(&pConstantData[i * 4], m_RenderState.vsConstantsI[StartRegister + i], sizeof(int) * 4);
    return D3D_OK;
  }
  return m_pDevice->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantB(
    UINT StartRegister, CONST BOOL *pConstantData, UINT BoolCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_B; i++)
      m_RenderState.vsConstantsB[StartRegister + i] = pConstantData[i];
  }

  // TODO: serialise
  return m_pDevice->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShaderConstantB(
    UINT StartRegister, BOOL *pConstantData, UINT BoolCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_B; i++)
      pConstantData[i] = m_RenderState.vsConstantsB[StartRegister + i];
    return D3D_OK;
  }
  return m_pDevice->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Stream sources
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetStreamSource(
    UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes, UINT Stride)
{
  if(StreamNumber < D3D9_MAX_STREAMS)
  {
    m_RenderState.streamSources[StreamNumber].offsetInBytes = OffsetInBytes;
    m_RenderState.streamSources[StreamNumber].stride = Stride;
    // TODO: update buffer ResourceId in shadow state
  }

  // TODO: unwrap buffer, serialise
  return m_pDevice->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetStreamSource(
    UINT StreamNumber, IDirect3DVertexBuffer9 **ppStreamData, UINT *pOffsetInBytes, UINT *pStride)
{
  // TODO: return wrapped buffer
  return m_pDevice->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber,
                                                                        UINT Setting)
{
  if(StreamNumber < D3D9_MAX_STREAMS)
    m_RenderState.streamSources[StreamNumber].freq = Setting;

  // TODO: serialise
  return m_pDevice->SetStreamSourceFreq(StreamNumber, Setting);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetStreamSourceFreq(UINT StreamNumber,
                                                                        UINT *pSetting)
{
  if(pSetting && StreamNumber < D3D9_MAX_STREAMS)
  {
    *pSetting = m_RenderState.streamSources[StreamNumber].freq;
    return D3D_OK;
  }
  return m_pDevice->GetStreamSourceFreq(StreamNumber, pSetting);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Index buffer
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9 *pIndexData)
{
  // TODO: update shadow state, unwrap, serialise
  return m_pDevice->SetIndices(pIndexData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9 **ppIndexData)
{
  // TODO: return wrapped index buffer
  return m_pDevice->GetIndices(ppIndexData);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Pixel shader
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreatePixelShader(
    CONST DWORD *pFunction, IDirect3DPixelShader9 **ppShader)
{
  // TODO: wrap shader
  return m_pDevice->CreatePixelShader(pFunction, ppShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9 *pShader)
{
  // TODO: update shadow state, unwrap, serialise
  return m_pDevice->SetPixelShader(pShader);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShader(
    IDirect3DPixelShader9 **ppShader)
{
  // TODO: return wrapped shader
  return m_pDevice->GetPixelShader(ppShader);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Pixel shader constants
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantF(
    UINT StartRegister, CONST float *pConstantData, UINT Vector4fCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_F; i++)
      memcpy(m_RenderState.psConstantsF[StartRegister + i], &pConstantData[i * 4],
             sizeof(float) * 4);
  }

  // TODO: serialise
  return m_pDevice->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShaderConstantF(
    UINT StartRegister, float *pConstantData, UINT Vector4fCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_F; i++)
      memcpy(&pConstantData[i * 4], m_RenderState.psConstantsF[StartRegister + i],
             sizeof(float) * 4);
    return D3D_OK;
  }
  return m_pDevice->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantI(
    UINT StartRegister, CONST int *pConstantData, UINT Vector4iCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_I; i++)
      memcpy(m_RenderState.psConstantsI[StartRegister + i], &pConstantData[i * 4], sizeof(int) * 4);
  }

  // TODO: serialise
  return m_pDevice->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShaderConstantI(
    UINT StartRegister, int *pConstantData, UINT Vector4iCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_I; i++)
      memcpy(&pConstantData[i * 4], m_RenderState.psConstantsI[StartRegister + i], sizeof(int) * 4);
    return D3D_OK;
  }
  return m_pDevice->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantB(
    UINT StartRegister, CONST BOOL *pConstantData, UINT BoolCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_B; i++)
      m_RenderState.psConstantsB[StartRegister + i] = pConstantData[i];
  }

  // TODO: serialise
  return m_pDevice->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShaderConstantB(
    UINT StartRegister, BOOL *pConstantData, UINT BoolCount)
{
  if(pConstantData)
  {
    for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_B; i++)
      pConstantData[i] = m_RenderState.psConstantsB[StartRegister + i];
    return D3D_OK;
  }
  return m_pDevice->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Patches (passthrough)
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawRectPatch(
    UINT Handle, CONST float *pNumSegs, CONST D3DRECTPATCH_INFO *pRectPatchInfo)
{
  return m_pDevice->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawTriPatch(
    UINT Handle, CONST float *pNumSegs, CONST D3DTRIPATCH_INFO *pTriPatchInfo)
{
  return m_pDevice->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DeletePatch(UINT Handle)
{
  return m_pDevice->DeletePatch(Handle);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Query
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type,
                                                                IDirect3DQuery9 **ppQuery)
{
  // TODO: wrap query
  return m_pDevice->CreateQuery(Type, ppQuery);
}
