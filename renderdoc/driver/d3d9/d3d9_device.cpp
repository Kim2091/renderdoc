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
#include "d3d9_buffers.h"
#include "d3d9_rendertext.h"
#include "d3d9_query.h"
#include "d3d9_replay.h"
#include "d3d9_resources.h"
#include "d3d9_shaders.h"
#include "d3d9_stateblock.h"
#include "os/os_specific.h"
#include "serialise/rdcfile.h"
#include "strings/string_utils.h"

// Static device pointer for D3DPERF routing
WrappedIDirect3DDevice9 *WrappedIDirect3DDevice9::s_D3D9Device = NULL;

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
  m_FrameReader = NULL;
  m_TextRenderer = NULL;

  m_CurEventID = 0;
  m_CurActionID = 0;
  m_CurChunkOffset = 0;

  m_SectionVersion = D3D9InitParams::CurrentVersion;

  m_StructuredFile = m_StoredStructuredData = new SDFile;

  RDCEraseEl(APIProps);

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

    if(pPresentationParameters && pPresentationParameters->hDeviceWindow)
    {
      Keyboard::AddInputWindow(WindowingSystem::Win32,
                               (void *)pPresentationParameters->hDeviceWindow);

      RenderDoc::Inst().AddFrameCapturer(
          DeviceOwnedWindow((void *)this, (void *)pPresentationParameters->hDeviceWindow), this);
    }

    RDCLOG("Created D3D9 device.");

    s_D3D9Device = this;
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
  m_DeviceRecord = NULL;
  m_FrameReader = NULL;
  m_TextRenderer = NULL;

  m_CurEventID = 0;
  m_CurActionID = 0;
  m_CurChunkOffset = 0;

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

  m_Replay = new D3D9Replay(this);

  RDCEraseEl(APIProps);
}

///////////////////////////////////////////////////////////////////////////
// Destructor
///////////////////////////////////////////////////////////////////////////
WrappedIDirect3DDevice9::~WrappedIDirect3DDevice9()
{
  if(s_D3D9Device == this)
    s_D3D9Device = NULL;

  SAFE_DELETE(m_TextRenderer);

  RenderDoc::Inst().RemoveDeviceFrameCapturer((IDirect3DDevice9 *)this);

  if(m_InitParams.PresentationParameters.hDeviceWindow)
  {
    RenderDoc::Inst().RemoveFrameCapturer(
        DeviceOwnedWindow((void *)this, (void *)m_InitParams.PresentationParameters.hDeviceWindow));

    Keyboard::RemoveInputWindow(WindowingSystem::Win32,
                                (void *)m_InitParams.PresentationParameters.hDeviceWindow);
  }

  SAFE_DELETE(m_StoredStructuredData);

  if(m_DeviceRecord)
  {
    RDCASSERT(m_DeviceRecord->GetRefCount() == 1);
    m_DeviceRecord->Delete(GetResourceManager());
  }

  m_ResourceManager->Shutdown();

  SAFE_DELETE(m_ResourceManager);
  SAFE_DELETE(m_Replay);
  SAFE_DELETE(m_FrameReader);

  SAFE_RELEASE(m_pDevice);
}

///////////////////////////////////////////////////////////////////////////
// Event/Action tracking
///////////////////////////////////////////////////////////////////////////

void WrappedIDirect3DDevice9::AddEvent()
{
  if(m_CurEventID == 0)
    return;

  APIEvent apievent;

  apievent.fileOffset = m_CurChunkOffset;
  apievent.eventId = m_CurEventID;

  apievent.chunkIndex = uint32_t(m_StructuredFile->chunks.size() - 1);

  m_CurEvents.push_back(apievent);
}

void WrappedIDirect3DDevice9::AddAction(const ActionDescription &a)
{
  if(m_CurEventID == 0)
    return;

  ActionDescription action = a;

  action.eventId = m_CurEventID;
  action.actionId = m_CurActionID;

  for(int i = 0; i < D3D9_MAX_RENDER_TARGETS; i++)
    action.outputs[i] = m_RenderState.renderTargets[i];

  action.depthOut = m_RenderState.depthStencil;

  // markers don't increment action ID
  ActionFlags MarkerMask = ActionFlags::SetMarker | ActionFlags::PushMarker | ActionFlags::PopMarker;
  if(!(action.flags & MarkerMask))
    m_CurActionID++;

  action.events.swap(m_CurEvents);

  // should have at least the root action here, push this action
  // onto the back's children list.
  if(!m_ActionStack.empty())
    m_ActionStack.back()->children.push_back(action);
  else
    RDCERR("Somehow lost action stack!");
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

  GetResourceManager()->FreeCaptureData();

  // Helper lambda: ensure a real D3D9 surface has a wrapped resource with a resource record.
  // If the surface was obtained implicitly (e.g. via GetBackBuffer) it may have a wrapper
  // but no resource record. We need the record so it gets serialized into the capture.
  auto ensureSurfaceRecord = [this](IDirect3DSurface9 *realSurf, bool isDepth) -> IDirect3DSurface9 * {
    if(!realSurf)
      return NULL;

    WrappedIDirect3DSurface9 *wrapped = NULL;

    if(GetResourceManager()->HasWrapper(realSurf))
    {
      wrapped = (WrappedIDirect3DSurface9 *)GetResourceManager()->GetWrapper(realSurf);
    }
    else
    {
      // Surface was never wrapped (game never called GetBackBuffer). Wrap it now.
      realSurf->AddRef();    // wrapper takes ownership
      wrapped = new WrappedIDirect3DSurface9(realSurf, this);
    }

    ResourceId id = wrapped->GetResourceID();

    // Check if this resource already has a record
    if(!GetResourceManager()->GetResourceRecord(id))
    {
      D3DSURFACE_DESC desc;
      wrapped->GetReal()->GetDesc(&desc);

      D3D9ResourceRecord *record = GetResourceManager()->AddResourceRecord(id);
      record->resType = D3D9ResourceType::Surface;
      record->pool = D3DPOOL_DEFAULT;
      record->usage = desc.Usage;
      record->Length = 0;

      // Serialize the creation chunk so the resource exists on replay
      USE_SCRATCH_SERIALISER();
      IDirect3DSurface9 *wrappedPtr = wrapped;
      if(isDepth)
      {
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateDepthStencilSurface);
        Serialise_CreateDepthStencilSurface(ser, desc.Width, desc.Height, desc.Format,
                                            desc.MultiSampleType, desc.MultiSampleQuality, FALSE,
                                            &wrappedPtr, NULL);
        record->AddChunk(scope.Get());
      }
      else
      {
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateRenderTarget);
        Serialise_CreateRenderTarget(ser, desc.Width, desc.Height, desc.Format,
                                     desc.MultiSampleType, desc.MultiSampleQuality, TRUE,
                                     &wrappedPtr, NULL);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents captures the surface data
      GetResourceManager()->MarkDirtyResource(id);
    }

    return wrapped;
  };

  // Ensure render target and depth stencil surfaces are wrapped and recorded BEFORE
  // PrepareInitialContents, so their initial state gets captured.
  for(DWORD rt = 0; rt < D3D9_MAX_RENDER_TARGETS; rt++)
  {
    IDirect3DSurface9 *realRT = NULL;
    m_pDevice->GetRenderTarget(rt, &realRT);
    if(realRT)
    {
      ensureSurfaceRecord(realRT, false);
      realRT->Release();
    }
  }

  {
    IDirect3DSurface9 *realDS = NULL;
    m_pDevice->GetDepthStencilSurface(&realDS);
    if(realDS)
    {
      ensureSurfaceRecord(realDS, true);
      realDS->Release();
    }
  }

  GetResourceManager()->PrepareInitialContents();

  // Now serialize the SetRenderTarget / SetDepthStencilSurface calls into the frame data
  // so that on replay, m_RenderState.renderTargets[] gets populated.
  for(DWORD rt = 0; rt < D3D9_MAX_RENDER_TARGETS; rt++)
  {
    IDirect3DSurface9 *realRT = NULL;
    m_pDevice->GetRenderTarget(rt, &realRT);
    if(realRT)
    {
      if(GetResourceManager()->HasWrapper(realRT))
      {
        IDirect3DSurface9 *wrappedRT =
            (IDirect3DSurface9 *)GetResourceManager()->GetWrapper(realRT);

        D3D9WrappedInfo *info = GetD3D9WrappedInfo(wrappedRT);
        if(info)
          GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_ReadBeforeWrite);

        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetRenderTarget);
        Serialise_SetRenderTarget(ser, rt, wrappedRT);
        m_DeviceRecord->AddChunk(scope.Get());
      }
      realRT->Release();
    }
  }

  {
    IDirect3DSurface9 *realDS = NULL;
    m_pDevice->GetDepthStencilSurface(&realDS);
    if(realDS)
    {
      if(GetResourceManager()->HasWrapper(realDS))
      {
        IDirect3DSurface9 *wrappedDS =
            (IDirect3DSurface9 *)GetResourceManager()->GetWrapper(realDS);

        D3D9WrappedInfo *info = GetD3D9WrappedInfo(wrappedDS);
        if(info)
          GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_ReadBeforeWrite);

        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetDepthStencilSurface);
        Serialise_SetDepthStencilSurface(ser, wrappedDS);
        m_DeviceRecord->AddChunk(scope.Get());
      }
      realDS->Release();
    }
  }
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

// Initial state helpers are implemented in d3d9_initstate.cpp

///////////////////////////////////////////////////////////////////////////
// D3DPERF statics
///////////////////////////////////////////////////////////////////////////
void WrappedIDirect3DDevice9::SetMarker(uint32_t col, const wchar_t *name)
{
  if(!s_D3D9Device)
    return;

  Annotation annot;
  annot.m_Type = Annotation::ANNOT_SETMARKER;
  annot.m_Col = col;
  annot.m_Name = StringFormat::Wide2UTF8(name ? name : L"");

  {
    SCOPED_LOCK(s_D3D9Device->m_AnnotLock);
    s_D3D9Device->m_AnnotationQueue.push_back(annot);
  }
}

int WrappedIDirect3DDevice9::BeginEvent(uint32_t col, const wchar_t *name)
{
  if(!s_D3D9Device)
    return 0;

  Annotation annot;
  annot.m_Type = Annotation::ANNOT_BEGINEVENT;
  annot.m_Col = col;
  annot.m_Name = StringFormat::Wide2UTF8(name ? name : L"");

  {
    SCOPED_LOCK(s_D3D9Device->m_AnnotLock);
    s_D3D9Device->m_AnnotationQueue.push_back(annot);
  }

  return s_D3D9Device->m_MarkerIndentLevel;
}

int WrappedIDirect3DDevice9::EndEvent()
{
  if(!s_D3D9Device)
    return 0;

  Annotation annot;
  annot.m_Type = Annotation::ANNOT_ENDEVENT;
  annot.m_Col = 0;

  {
    SCOPED_LOCK(s_D3D9Device->m_AnnotLock);
    s_D3D9Device->m_AnnotationQueue.push_back(annot);
  }

  return --s_D3D9Device->m_MarkerIndentLevel;
}

///////////////////////////////////////////////////////////////////////////
// Annotation drain and serialization
///////////////////////////////////////////////////////////////////////////
void WrappedIDirect3DDevice9::DrainAnnotationQueue()
{
  if(!IsActiveCapturing(m_State))
    return;

  m_AnnotLock.Lock();

  if(m_AnnotationQueue.empty())
  {
    m_AnnotLock.Unlock();
    return;
  }

  rdcarray<Annotation> annotations;
  annotations.swap(m_AnnotationQueue);

  m_AnnotLock.Unlock();

  for(size_t i = 0; i < annotations.size(); i++)
  {
    const Annotation &a = annotations[i];

    switch(a.m_Type)
    {
      case Annotation::ANNOT_SETMARKER:
      {
        rdcwstr wide = StringFormat::UTF82Wide(a.m_Name);
        USE_SCRATCH_SERIALISER();
        GET_SERIALISER.SetActionChunk();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetMarker);
        Serialise_SetMarker(ser, a.m_Col, wide.c_str());
        m_DeviceRecord->AddChunk(scope.Get());
        break;
      }
      case Annotation::ANNOT_BEGINEVENT:
      {
        rdcwstr wide = StringFormat::UTF82Wide(a.m_Name);
        USE_SCRATCH_SERIALISER();
        GET_SERIALISER.SetActionChunk();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::PushMarker);
        Serialise_PushMarker(ser, a.m_Col, wide.c_str());
        m_DeviceRecord->AddChunk(scope.Get());
        break;
      }
      case Annotation::ANNOT_ENDEVENT:
      {
        USE_SCRATCH_SERIALISER();
        GET_SERIALISER.SetActionChunk();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::PopMarker);
        Serialise_PopMarker(ser);
        m_DeviceRecord->AddChunk(scope.Get());
        break;
      }
    }
  }
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetMarker(SerialiserType &ser, uint32_t Color,
                                                    const wchar_t *MarkerNameW)
{
  SERIALISE_ELEMENT(Color);
  SERIALISE_ELEMENT_LOCAL(MarkerName, StringFormat::Wide2UTF8(MarkerNameW ? MarkerNameW : L""));

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(IsLoading(m_State))
    {
      ActionDescription action;
      action.customName = MarkerName;
      action.flags |= ActionFlags::SetMarker;

      byte alpha = (Color >> 24) & 0xff;
      byte red = (Color >> 16) & 0xff;
      byte green = (Color >> 8) & 0xff;
      byte blue = (Color >> 0) & 0xff;

      action.markerColor.x = float(red) / 255.0f;
      action.markerColor.y = float(green) / 255.0f;
      action.markerColor.z = float(blue) / 255.0f;
      action.markerColor.w = float(alpha) / 255.0f;

      AddEvent();
      AddAction(action);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_PushMarker(SerialiserType &ser, uint32_t Color,
                                                     const wchar_t *MarkerNameW)
{
  SERIALISE_ELEMENT(Color);
  SERIALISE_ELEMENT_LOCAL(MarkerName, StringFormat::Wide2UTF8(MarkerNameW ? MarkerNameW : L""));

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(IsLoading(m_State))
    {
      ActionDescription action;
      action.customName = MarkerName;
      action.flags |= ActionFlags::PushMarker;

      byte alpha = (Color >> 24) & 0xff;
      byte red = (Color >> 16) & 0xff;
      byte green = (Color >> 8) & 0xff;
      byte blue = (Color >> 0) & 0xff;

      action.markerColor.x = float(red) / 255.0f;
      action.markerColor.y = float(green) / 255.0f;
      action.markerColor.z = float(blue) / 255.0f;
      action.markerColor.w = float(alpha) / 255.0f;

      AddEvent();
      AddAction(action);

      // Push onto the action stack so subsequent actions become children
      if(!m_ActionStack.empty())
        m_ActionStack.push_back(&m_ActionStack.back()->children.back());
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_PopMarker(SerialiserType &ser)
{
  if(IsReplayingAndReading())
  {
    if(IsLoading(m_State))
    {
      if(m_ActionStack.size() > 1)
        m_ActionStack.pop_back();
    }
  }

  return true;
}

template bool WrappedIDirect3DDevice9::Serialise_SetMarker(ReadSerialiser &ser, uint32_t Color,
                                                             const wchar_t *MarkerNameW);
template bool WrappedIDirect3DDevice9::Serialise_SetMarker(WriteSerialiser &ser, uint32_t Color,
                                                             const wchar_t *MarkerNameW);
template bool WrappedIDirect3DDevice9::Serialise_PushMarker(ReadSerialiser &ser, uint32_t Color,
                                                              const wchar_t *MarkerNameW);
template bool WrappedIDirect3DDevice9::Serialise_PushMarker(WriteSerialiser &ser, uint32_t Color,
                                                              const wchar_t *MarkerNameW);
template bool WrappedIDirect3DDevice9::Serialise_PopMarker(ReadSerialiser &ser);
template bool WrappedIDirect3DDevice9::Serialise_PopMarker(WriteSerialiser &ser);

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
  IDirect3DSurface9 *realSurf = pCursorBitmap;
  if(pCursorBitmap)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pCursorBitmap);
    if(info)
      realSurf = (IDirect3DSurface9 *)info->realObject;
  }
  return m_pDevice->SetCursorProperties(XHotSpot, YHotSpot, realSurf);
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->Reset(pPresentationParameters));

  if(SUCCEEDED(ret))
  {
    if(pPresentationParameters)
      m_InitParams.PresentationParameters = *pPresentationParameters;

    if(IsActiveCapturing(m_State))
    {
      USE_SCRATCH_SERIALISER();
      SCOPED_SERIALISE_CHUNK(D3D9Chunk::Reset);
      Serialise_Reset(ser, pPresentationParameters);
      m_DeviceRecord->AddChunk(scope.Get());
    }
  }

  return ret;
}

void WrappedIDirect3DDevice9::RenderOverlayText()
{
  if(!IsBackgroundCapturing(m_State))
    return;

  uint32_t overlay = RenderDoc::Inst().GetOverlayBits();

  if(!(overlay & eRENDERDOC_Overlay_Enabled))
    return;

  // Lazy-init the text renderer on first use
  if(!m_TextRenderer)
    m_TextRenderer = new D3D9TextRenderer(m_pDevice);

  DeviceOwnedWindow devWnd((void *)this,
                           (void *)m_InitParams.PresentationParameters.hDeviceWindow);
  rdcstr overlayText = RenderDoc::Inst().GetOverlayText(RDCDriver::D3D9, devWnd, m_FrameCounter, 0);

  m_TextRenderer->RenderText(0.0f, 0.0f, overlayText);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Present(CONST RECT *pSourceRect,
                                                            CONST RECT *pDestRect,
                                                            HWND hDestWindowOverride,
                                                            CONST RGNDATA *pDirtyRegion)
{
  if(IsBackgroundCapturing(m_State))
    RenderDoc::Inst().Tick();

  m_FrameCounter++;

  RenderDoc::Inst().AddActiveDriver(RDCDriver::D3D9, true);

  if(IsActiveCapturing(m_State))
  {
    DrainAnnotationQueue();

    // Serialize Present as the last event
    {
      USE_SCRATCH_SERIALISER();
      SCOPED_SERIALISE_CHUNK(D3D9Chunk::Present);
      Serialise_Present(ser, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
      m_DeviceRecord->AddChunk(scope.Get());
    }

    // End the frame capture
    RenderDoc::Inst().EndFrameCapture(DeviceOwnedWindow((void *)this, NULL));
  }

  if(IsBackgroundCapturing(m_State) && RenderDoc::Inst().ShouldTriggerCapture(m_FrameCounter))
  {
    RenderDoc::Inst().StartFrameCapture(DeviceOwnedWindow((void *)this, NULL));

    m_CapturedFrames.back().frameNumber = m_FrameCounter;
  }

  RenderOverlayText();

  return m_pDevice->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetBackBuffer(
    UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer)
{
  if(ppBackBuffer == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DSurface9 *realSurface = NULL;
  HRESULT ret = m_pDevice->GetBackBuffer(iSwapChain, iBackBuffer, Type, &realSurface);

  if(SUCCEEDED(ret) && realSurface)
  {
    if(GetResourceManager()->HasWrapper(realSurface))
    {
      IUnknown *existing = GetResourceManager()->GetWrapper(realSurface);
      *ppBackBuffer = (IDirect3DSurface9 *)existing;
      (*ppBackBuffer)->AddRef();
      realSurface->Release();
    }
    else
    {
      WrappedIDirect3DSurface9 *wrapped = new WrappedIDirect3DSurface9(realSurface, this);
      *ppBackBuffer = wrapped;
    }
  }
  else
  {
    *ppBackBuffer = NULL;
  }

  return ret;
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
  IDirect3DTexture9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(
      ret = m_pDevice->CreateTexture(Width, Height, Levels, Usage, Format, Pool, &real,
                                     pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DTexture9 *wrappedTex = new WrappedIDirect3DTexture9(real, this);
    IDirect3DTexture9 *wrappedPtr = wrappedTex;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedTex->GetResourceID());
      record->resType = D3D9ResourceType::Texture;
      record->pool = Pool;
      record->usage = Usage;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateTexture);
        Serialise_CreateTexture(ser, Width, Height, Levels, Usage, Format, Pool, &wrappedPtr,
                                pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture texture data when F12 is pressed
      GetResourceManager()->MarkDirtyResource(wrappedTex->GetResourceID());
    }

    *ppTexture = wrappedTex;
  }
  else
  {
    if(ppTexture)
      *ppTexture = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVolumeTexture(
    UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle)
{
  IDirect3DVolumeTexture9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateVolumeTexture(Width, Height, Depth, Levels, Usage,
                                                            Format, Pool, &real, pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DVolumeTexture9 *wrappedVol = new WrappedIDirect3DVolumeTexture9(real, this);
    IDirect3DVolumeTexture9 *wrappedPtr = wrappedVol;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedVol->GetResourceID());
      record->resType = D3D9ResourceType::VolumeTexture;
      record->pool = Pool;
      record->usage = Usage;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateVolumeTexture);
        Serialise_CreateVolumeTexture(ser, Width, Height, Depth, Levels, Usage, Format, Pool,
                                      &wrappedPtr, pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture volume texture data
      GetResourceManager()->MarkDirtyResource(wrappedVol->GetResourceID());
    }

    *ppVolumeTexture = wrappedVol;
  }
  else
  {
    if(ppVolumeTexture)
      *ppVolumeTexture = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateCubeTexture(
    UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DCubeTexture9 **ppCubeTexture, HANDLE *pSharedHandle)
{
  IDirect3DCubeTexture9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool,
                                                          &real, pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DCubeTexture9 *wrappedCube = new WrappedIDirect3DCubeTexture9(real, this);
    IDirect3DCubeTexture9 *wrappedPtr = wrappedCube;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedCube->GetResourceID());
      record->resType = D3D9ResourceType::CubeTexture;
      record->pool = Pool;
      record->usage = Usage;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateCubeTexture);
        Serialise_CreateCubeTexture(ser, EdgeLength, Levels, Usage, Format, Pool, &wrappedPtr,
                                    pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture cube texture data
      GetResourceManager()->MarkDirtyResource(wrappedCube->GetResourceID());
    }

    *ppCubeTexture = wrappedCube;
  }
  else
  {
    if(ppCubeTexture)
      *ppCubeTexture = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexBuffer(
    UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
    IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle)
{
  IDirect3DVertexBuffer9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(
      ret = m_pDevice->CreateVertexBuffer(Length, Usage, FVF, Pool, &real, pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DVertexBuffer9 *wrappedVB =
        new WrappedIDirect3DVertexBuffer9(real, this, Length, Usage);
    IDirect3DVertexBuffer9 *wrappedPtr = wrappedVB;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedVB->GetResourceID());
      record->resType = D3D9ResourceType::VertexBuffer;
      record->pool = Pool;
      record->usage = Usage;
      record->Length = Length;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateVertexBuffer);
        Serialise_CreateVertexBuffer(ser, Length, Usage, FVF, Pool, &wrappedPtr, pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture buffer data
      GetResourceManager()->MarkDirtyResource(wrappedVB->GetResourceID());
    }

    *ppVertexBuffer = wrappedVB;
  }
  else
  {
    if(ppVertexBuffer)
      *ppVertexBuffer = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateIndexBuffer(
    UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle)
{
  IDirect3DIndexBuffer9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(
      ret = m_pDevice->CreateIndexBuffer(Length, Usage, Format, Pool, &real, pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DIndexBuffer9 *wrappedIB =
        new WrappedIDirect3DIndexBuffer9(real, this, Length, Usage, Format);
    IDirect3DIndexBuffer9 *wrappedPtr = wrappedIB;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedIB->GetResourceID());
      record->resType = D3D9ResourceType::IndexBuffer;
      record->pool = Pool;
      record->usage = Usage;
      record->Length = Length;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateIndexBuffer);
        Serialise_CreateIndexBuffer(ser, Length, Usage, Format, Pool, &wrappedPtr, pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture buffer data
      GetResourceManager()->MarkDirtyResource(wrappedIB->GetResourceID());
    }

    *ppIndexBuffer = wrappedIB;
  }
  else
  {
    if(ppIndexBuffer)
      *ppIndexBuffer = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateRenderTarget(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
    DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  IDirect3DSurface9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateRenderTarget(Width, Height, Format, MultiSample,
                                                           MultisampleQuality, Lockable, &real,
                                                           pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DSurface9 *wrappedSurf = new WrappedIDirect3DSurface9(real, this);
    IDirect3DSurface9 *wrappedPtr = wrappedSurf;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedSurf->GetResourceID());
      record->resType = D3D9ResourceType::Surface;
      record->pool = D3DPOOL_DEFAULT;
      record->usage = D3DUSAGE_RENDERTARGET;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateRenderTarget);
        Serialise_CreateRenderTarget(ser, Width, Height, Format, MultiSample, MultisampleQuality,
                                     Lockable, &wrappedPtr, pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture render target data
      GetResourceManager()->MarkDirtyResource(wrappedSurf->GetResourceID());
    }

    *ppSurface = wrappedSurf;
  }
  else
  {
    if(ppSurface)
      *ppSurface = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateDepthStencilSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
    DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  IDirect3DSurface9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateDepthStencilSurface(
                          Width, Height, Format, MultiSample, MultisampleQuality, Discard, &real,
                          pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DSurface9 *wrappedSurf = new WrappedIDirect3DSurface9(real, this);
    IDirect3DSurface9 *wrappedPtr = wrappedSurf;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedSurf->GetResourceID());
      record->resType = D3D9ResourceType::Surface;
      record->pool = D3DPOOL_DEFAULT;
      record->usage = D3DUSAGE_DEPTHSTENCIL;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateDepthStencilSurface);
        Serialise_CreateDepthStencilSurface(ser, Width, Height, Format, MultiSample,
                                            MultisampleQuality, Discard, &wrappedPtr,
                                            pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture depth stencil data
      GetResourceManager()->MarkDirtyResource(wrappedSurf->GetResourceID());
    }

    *ppSurface = wrappedSurf;
  }
  else
  {
    if(ppSurface)
      *ppSurface = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Surface operations
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::UpdateSurface(
    IDirect3DSurface9 *pSourceSurface, CONST RECT *pSourceRect,
    IDirect3DSurface9 *pDestinationSurface, CONST POINT *pDestPoint)
{
  IDirect3DSurface9 *realSrc = pSourceSurface;
  IDirect3DSurface9 *realDst = pDestinationSurface;
  if(pSourceSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pSourceSurface);
    if(info)
      realSrc = (IDirect3DSurface9 *)info->realObject;
  }
  if(pDestinationSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDestinationSurface);
    if(info)
      realDst = (IDirect3DSurface9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->UpdateSurface(realSrc, pSourceRect, realDst, pDestPoint));

  if(SUCCEEDED(ret) && IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::UpdateSurface);
    Serialise_UpdateSurface(ser, pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
    m_DeviceRecord->AddChunk(scope.Get());

    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pSourceSurface),
                                                      eFrameRef_Read);
    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pDestinationSurface),
                                                      eFrameRef_PartialWrite);
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::UpdateTexture(
    IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture)
{
  IDirect3DBaseTexture9 *realSrc = pSourceTexture;
  IDirect3DBaseTexture9 *realDst = pDestinationTexture;
  if(pSourceTexture)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pSourceTexture);
    if(info)
      realSrc = (IDirect3DBaseTexture9 *)info->realObject;
  }
  if(pDestinationTexture)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDestinationTexture);
    if(info)
      realDst = (IDirect3DBaseTexture9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->UpdateTexture(realSrc, realDst));

  if(SUCCEEDED(ret) && IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::UpdateTexture);
    Serialise_UpdateTexture(ser, pSourceTexture, pDestinationTexture);
    m_DeviceRecord->AddChunk(scope.Get());

    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pSourceTexture),
                                                      eFrameRef_Read);
    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pDestinationTexture),
                                                      eFrameRef_PartialWrite);
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderTargetData(
    IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface)
{
  IDirect3DSurface9 *realRT = pRenderTarget;
  IDirect3DSurface9 *realDst = pDestSurface;
  if(pRenderTarget)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pRenderTarget);
    if(info)
      realRT = (IDirect3DSurface9 *)info->realObject;
  }
  if(pDestSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDestSurface);
    if(info)
      realDst = (IDirect3DSurface9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->GetRenderTargetData(realRT, realDst));

  if(SUCCEEDED(ret) && IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::GetRenderTargetData);
    Serialise_GetRenderTargetData(ser, pRenderTarget, pDestSurface);
    m_DeviceRecord->AddChunk(scope.Get());

    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pRenderTarget),
                                                      eFrameRef_Read);
    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pDestSurface),
                                                      eFrameRef_PartialWrite);
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetFrontBufferData(
    UINT iSwapChain, IDirect3DSurface9 *pDestSurface)
{
  IDirect3DSurface9 *realDst = pDestSurface;
  if(pDestSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDestSurface);
    if(info)
      realDst = (IDirect3DSurface9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->GetFrontBufferData(iSwapChain, realDst));

  if(SUCCEEDED(ret) && IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::GetFrontBufferData);
    Serialise_GetFrontBufferData(ser, iSwapChain, pDestSurface);
    m_DeviceRecord->AddChunk(scope.Get());

    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pDestSurface),
                                                      eFrameRef_PartialWrite);
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::StretchRect(
    IDirect3DSurface9 *pSourceSurface, CONST RECT *pSourceRect, IDirect3DSurface9 *pDestSurface,
    CONST RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
  IDirect3DSurface9 *realSrc = pSourceSurface;
  IDirect3DSurface9 *realDst = pDestSurface;
  if(pSourceSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pSourceSurface);
    if(info)
      realSrc = (IDirect3DSurface9 *)info->realObject;
  }
  if(pDestSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDestSurface);
    if(info)
      realDst = (IDirect3DSurface9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(
      ret = m_pDevice->StretchRect(realSrc, pSourceRect, realDst, pDestRect, Filter));

  if(SUCCEEDED(ret) && IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::StretchRect);
    Serialise_StretchRect(ser, pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
    m_DeviceRecord->AddChunk(scope.Get());

    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pSourceSurface),
                                                      eFrameRef_Read);
    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pDestSurface),
                                                      eFrameRef_PartialWrite);
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ColorFill(IDirect3DSurface9 *pSurface,
                                                              CONST RECT *pRect, D3DCOLOR color)
{
  IDirect3DSurface9 *realSurf = pSurface;
  if(pSurface)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pSurface);
    if(info)
      realSurf = (IDirect3DSurface9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->ColorFill(realSurf, pRect, color));

  if(SUCCEEDED(ret) && IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::ColorFill);
    Serialise_ColorFill(ser, pSurface, pRect, color);
    m_DeviceRecord->AddChunk(scope.Get());

    GetResourceManager()->MarkResourceFrameReferenced(GetIDForD3D9Resource(pSurface),
                                                      eFrameRef_PartialWrite);
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateOffscreenPlainSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9 **ppSurface,
    HANDLE *pSharedHandle)
{
  IDirect3DSurface9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateOffscreenPlainSurface(Width, Height, Format, Pool,
                                                                    &real, pSharedHandle));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DSurface9 *wrappedSurf = new WrappedIDirect3DSurface9(real, this);
    IDirect3DSurface9 *wrappedPtr = wrappedSurf;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedSurf->GetResourceID());
      record->resType = D3D9ResourceType::Surface;
      record->pool = Pool;
      record->usage = 0;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateOffscreenPlainSurface);
        Serialise_CreateOffscreenPlainSurface(ser, Width, Height, Format, Pool, &wrappedPtr,
                                              pSharedHandle);
        record->AddChunk(scope.Get());
      }

      // Mark dirty so PrepareInitialContents will capture surface data
      GetResourceManager()->MarkDirtyResource(wrappedSurf->GetResourceID());
    }

    *ppSurface = wrappedSurf;
  }
  else
  {
    if(ppSurface)
      *ppSurface = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Render targets / depth
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetRenderTarget(
    DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget)
{
  IDirect3DSurface9 *realSurf = NULL;
  if(pRenderTarget)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pRenderTarget);
    if(info)
      realSurf = (IDirect3DSurface9 *)info->realObject;
    else
      realSurf = pRenderTarget;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetRenderTarget(RenderTargetIndex, realSurf));

  if(IsActiveCapturing(m_State))
  {
    if(pRenderTarget)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pRenderTarget);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_ReadBeforeWrite);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetRenderTarget);
    Serialise_SetRenderTarget(ser, RenderTargetIndex, pRenderTarget);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetRenderTarget(
    DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget)
{
  if(ppRenderTarget == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DSurface9 *real = NULL;
  HRESULT ret = m_pDevice->GetRenderTarget(RenderTargetIndex, &real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppRenderTarget = (IDirect3DSurface9 *)wrapper;
      (*ppRenderTarget)->AddRef();
      real->Release();
    }
    else
    {
      WrappedIDirect3DSurface9 *wrapped = new WrappedIDirect3DSurface9(real, this);
      *ppRenderTarget = wrapped;
    }
  }
  else
  {
    *ppRenderTarget = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetDepthStencilSurface(
    IDirect3DSurface9 *pNewZStencil)
{
  IDirect3DSurface9 *realSurf = NULL;
  if(pNewZStencil)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pNewZStencil);
    if(info)
      realSurf = (IDirect3DSurface9 *)info->realObject;
    else
      realSurf = pNewZStencil;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetDepthStencilSurface(realSurf));

  if(IsActiveCapturing(m_State))
  {
    if(pNewZStencil)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pNewZStencil);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_ReadBeforeWrite);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetDepthStencilSurface);
    Serialise_SetDepthStencilSurface(ser, pNewZStencil);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetDepthStencilSurface(
    IDirect3DSurface9 **ppZStencilSurface)
{
  if(ppZStencilSurface == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DSurface9 *real = NULL;
  HRESULT ret = m_pDevice->GetDepthStencilSurface(&real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppZStencilSurface = (IDirect3DSurface9 *)wrapper;
      (*ppZStencilSurface)->AddRef();
      real->Release();
    }
    else
    {
      WrappedIDirect3DSurface9 *wrapped = new WrappedIDirect3DSurface9(real, this);
      *ppZStencilSurface = wrapped;
    }
  }
  else
  {
    *ppZStencilSurface = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Scene
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::BeginScene()
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->BeginScene());

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::BeginScene);
    Serialise_BeginScene(ser);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EndScene()
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->EndScene());

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::EndScene);
    Serialise_EndScene(ser);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Clear
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::Clear(DWORD Count, CONST D3DRECT *pRects,
                                                          DWORD Flags, D3DCOLOR Color, float Z,
                                                          DWORD Stencil)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->Clear(Count, pRects, Flags, Color, Z, Stencil));

  if(IsActiveCapturing(m_State))
  {
    DrainAnnotationQueue();

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::Clear);
    Serialise_Clear(ser, Count, pRects, Flags, Color, Z, Stencil);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Transforms
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State,
                                                                 CONST D3DMATRIX *pMatrix)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetTransform(State, pMatrix));

  if(pMatrix && (UINT)State < D3D9_MAX_TRANSFORMS)
    m_RenderState.transforms[(UINT)State] = *pMatrix;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetTransform);
    Serialise_SetTransform(ser, State, pMatrix);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetViewport(pViewport));

  if(pViewport)
    m_RenderState.viewport = *pViewport;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetViewport);
    Serialise_SetViewport(ser, pViewport);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetMaterial(pMaterial));

  if(pMaterial)
    m_RenderState.material = *pMaterial;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetMaterial);
    Serialise_SetMaterial(ser, pMaterial);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetLight(Index, pLight));

  if(pLight)
  {
    if(Index >= m_RenderState.lights.size())
      m_RenderState.lights.resize(Index + 1);
    m_RenderState.lights[Index].light = *pLight;
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetLight);
    Serialise_SetLight(ser, Index, pLight);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->LightEnable(Index, Enable));

  if(Index >= m_RenderState.lights.size())
    m_RenderState.lights.resize(Index + 1);
  m_RenderState.lights[Index].enabled = (Enable != FALSE);

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::LightEnable);
    Serialise_LightEnable(ser, Index, Enable);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetClipPlane(Index, pPlane));

  if(pPlane && Index < D3D9_MAX_CLIP_PLANES)
    memcpy(m_RenderState.clipPlanes[Index], pPlane, sizeof(float) * 4);

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetClipPlane);
    Serialise_SetClipPlane(ser, Index, pPlane);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetRenderState(State, Value));

  if((UINT)State < D3D9_MAX_RENDER_STATES)
    m_RenderState.renderStates[(UINT)State] = Value;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetRenderState);
    Serialise_SetRenderState(ser, State, Value);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  IDirect3DStateBlock9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateStateBlock(Type, &real));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DStateBlock9 *wrappedSB = new WrappedIDirect3DStateBlock9(real, this, Type);
    IDirect3DStateBlock9 *wrappedPtr = wrappedSB;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedSB->GetResourceID());
      record->resType = D3D9ResourceType::StateBlock;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateStateBlock);
        Serialise_CreateStateBlock(ser, Type, &wrappedPtr);
        record->AddChunk(scope.Get());
      }
    }

    *ppSB = wrappedSB;
  }
  else
  {
    if(ppSB)
      *ppSB = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::BeginStateBlock()
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->BeginStateBlock());

  if(SUCCEEDED(ret))
  {
    m_StateBlockRecording = true;

    if(IsActiveCapturing(m_State))
    {
      USE_SCRATCH_SERIALISER();
      SCOPED_SERIALISE_CHUNK(D3D9Chunk::BeginStateBlock);
      Serialise_BeginStateBlock(ser);
      m_DeviceRecord->AddChunk(scope.Get());
    }
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::EndStateBlock(IDirect3DStateBlock9 **ppSB)
{
  IDirect3DStateBlock9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->EndStateBlock(&real));

  m_StateBlockRecording = false;

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DStateBlock9 *wrappedSB =
        new WrappedIDirect3DStateBlock9(real, this, (D3DSTATEBLOCKTYPE)0);
    IDirect3DStateBlock9 *wrappedPtr = wrappedSB;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedSB->GetResourceID());
      record->resType = D3D9ResourceType::StateBlock;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::EndStateBlock);
        Serialise_EndStateBlock(ser, &wrappedPtr);
        record->AddChunk(scope.Get());
      }
    }

    *ppSB = wrappedSB;
  }
  else
  {
    if(ppSB)
      *ppSB = NULL;
  }

  return ret;
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
  if(ppTexture == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DBaseTexture9 *real = NULL;
  HRESULT ret = m_pDevice->GetTexture(Stage, &real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppTexture = (IDirect3DBaseTexture9 *)wrapper;
      (*ppTexture)->AddRef();
      real->Release();
    }
    else
    {
      *ppTexture = real;
    }
  }
  else
  {
    *ppTexture = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetTexture(DWORD Stage,
                                                               IDirect3DBaseTexture9 *pTexture)
{
  IDirect3DBaseTexture9 *realTex = NULL;
  if(pTexture)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pTexture);
    if(info)
      realTex = (IDirect3DBaseTexture9 *)info->realObject;
    else
      realTex = pTexture;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetTexture(Stage, realTex));

  if(IsActiveCapturing(m_State))
  {
    if(pTexture)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pTexture);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_Read);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetTexture);
    Serialise_SetTexture(ser, Stage, pTexture);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetTextureStageState(Stage, Type, Value));

  if(Stage < D3D9_MAX_TEXTURE_STAGES && (UINT)Type < D3D9_MAX_TSS_STATES)
    m_RenderState.textureStageStates[Stage][(UINT)Type] = Value;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetTextureStageState);
    Serialise_SetTextureStageState(ser, Stage, Type, Value);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetSamplerState(Sampler, Type, Value));

  if(Sampler < D3D9_TOTAL_SAMPLERS && (UINT)Type < D3D9_MAX_SAMPLER_STATES)
    m_RenderState.samplerStates[Sampler][(UINT)Type] = Value;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetSamplerState);
    Serialise_SetSamplerState(ser, Sampler, Type, Value);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetScissorRect(pRect));

  if(pRect)
    m_RenderState.scissor = *pRect;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetScissorRect);
    Serialise_SetScissorRect(ser, pRect);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetSoftwareVertexProcessing(bSoftware));

  m_RenderState.softwareVertexProcessing = bSoftware;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetSoftwareVertexProcessing);
    Serialise_SetSoftwareVertexProcessing(ser, bSoftware);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetNPatchMode(nSegments));

  m_RenderState.nPatchMode = nSegments;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetNPatchMode);
    Serialise_SetNPatchMode(ser, nSegments);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount));

  if(IsActiveCapturing(m_State))
  {
    DrainAnnotationQueue();

    USE_SCRATCH_SERIALISER();
    GET_SERIALISER.SetActionChunk();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::DrawPrimitive);
    Serialise_DrawPrimitive(ser, PrimitiveType, StartVertex, PrimitiveCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }
  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawIndexedPrimitive(
    D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices,
    UINT startIndex, UINT primCount)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex,
                                                             MinVertexIndex, NumVertices,
                                                             startIndex, primCount));

  if(IsActiveCapturing(m_State))
  {
    DrainAnnotationQueue();

    USE_SCRATCH_SERIALISER();
    GET_SERIALISER.SetActionChunk();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::DrawIndexedPrimitive);
    Serialise_DrawIndexedPrimitive(ser, PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices,
                                   startIndex, primCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }
  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawPrimitiveUP(
    D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData,
    UINT VertexStreamZeroStride)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->DrawPrimitiveUP(PrimitiveType, PrimitiveCount,
                                                        pVertexStreamZeroData,
                                                        VertexStreamZeroStride));

  if(IsActiveCapturing(m_State))
  {
    DrainAnnotationQueue();

    USE_SCRATCH_SERIALISER();
    GET_SERIALISER.SetActionChunk();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::DrawPrimitiveUP);
    Serialise_DrawPrimitiveUP(ser, PrimitiveType, PrimitiveCount, pVertexStreamZeroData,
                              VertexStreamZeroStride);
    m_DeviceRecord->AddChunk(scope.Get());
  }
  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::DrawIndexedPrimitiveUP(
    D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount,
    CONST void *pIndexData, D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData,
    UINT VertexStreamZeroStride)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->DrawIndexedPrimitiveUP(
                          PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData,
                          IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride));

  if(IsActiveCapturing(m_State))
  {
    DrainAnnotationQueue();

    USE_SCRATCH_SERIALISER();
    GET_SERIALISER.SetActionChunk();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::DrawIndexedPrimitiveUP);
    Serialise_DrawIndexedPrimitiveUP(ser, PrimitiveType, MinVertexIndex, NumVertices,
                                     PrimitiveCount, pIndexData, IndexDataFormat,
                                     pVertexStreamZeroData, VertexStreamZeroStride);
    m_DeviceRecord->AddChunk(scope.Get());
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Process vertices (passthrough)
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::ProcessVertices(
    UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9 *pDestBuffer,
    IDirect3DVertexDeclaration9 *pVertexDecl, DWORD Flags)
{
  IDirect3DVertexBuffer9 *realVB = pDestBuffer;
  if(pDestBuffer)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDestBuffer);
    if(info)
      realVB = (IDirect3DVertexBuffer9 *)info->realObject;
  }

  IDirect3DVertexDeclaration9 *realDecl = pVertexDecl;
  if(pVertexDecl)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pVertexDecl);
    if(info)
      realDecl = (IDirect3DVertexDeclaration9 *)info->realObject;
  }

  return m_pDevice->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, realVB, realDecl,
                                    Flags);
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Vertex declaration
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreateVertexDeclaration(
    CONST D3DVERTEXELEMENT9 *pVertexElements, IDirect3DVertexDeclaration9 **ppDecl)
{
  IDirect3DVertexDeclaration9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateVertexDeclaration(pVertexElements, &real));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DVertexDeclaration9 *wrappedDecl =
        new WrappedIDirect3DVertexDeclaration9(real, this);
    IDirect3DVertexDeclaration9 *wrappedPtr = wrappedDecl;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedDecl->GetResourceID());
      record->resType = D3D9ResourceType::VertexDeclaration;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateVertexDeclaration);
        Serialise_CreateVertexDeclaration(ser, pVertexElements, &wrappedPtr);
        record->AddChunk(scope.Get());
      }
    }

    *ppDecl = wrappedDecl;
  }
  else
  {
    if(ppDecl)
      *ppDecl = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexDeclaration(
    IDirect3DVertexDeclaration9 *pDecl)
{
  IDirect3DVertexDeclaration9 *realDecl = NULL;
  if(pDecl)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDecl);
    if(info)
      realDecl = (IDirect3DVertexDeclaration9 *)info->realObject;
    else
      realDecl = pDecl;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetVertexDeclaration(realDecl));

  if(IsActiveCapturing(m_State))
  {
    if(pDecl)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pDecl);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_Read);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetVertexDeclaration);
    Serialise_SetVertexDeclaration(ser, pDecl);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexDeclaration(
    IDirect3DVertexDeclaration9 **ppDecl)
{
  if(ppDecl == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DVertexDeclaration9 *real = NULL;
  HRESULT ret = m_pDevice->GetVertexDeclaration(&real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppDecl = (IDirect3DVertexDeclaration9 *)wrapper;
      (*ppDecl)->AddRef();
    }
    else
    {
      *ppDecl = real;
    }
    real->Release();
  }
  else
  {
    *ppDecl = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — FVF
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetFVF(DWORD FVF)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetFVF(FVF));

  m_RenderState.FVF = FVF;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetFVF);
    Serialise_SetFVF(ser, FVF);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  IDirect3DVertexShader9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateVertexShader(pFunction, &real));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DVertexShader9 *wrappedVS = new WrappedIDirect3DVertexShader9(real, this);
    IDirect3DVertexShader9 *wrappedPtr = wrappedVS;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedVS->GetResourceID());
      record->resType = D3D9ResourceType::VertexShader;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateVertexShader);
        Serialise_CreateVertexShader(ser, pFunction, &wrappedPtr);
        record->AddChunk(scope.Get());
      }
    }

    *ppShader = wrappedVS;
  }
  else
  {
    if(ppShader)
      *ppShader = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShader(
    IDirect3DVertexShader9 *pShader)
{
  IDirect3DVertexShader9 *realShader = NULL;
  if(pShader)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pShader);
    if(info)
      realShader = (IDirect3DVertexShader9 *)info->realObject;
    else
      realShader = pShader;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetVertexShader(realShader));

  if(IsActiveCapturing(m_State))
  {
    if(pShader)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pShader);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_Read);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetVertexShader);
    Serialise_SetVertexShader(ser, pShader);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetVertexShader(
    IDirect3DVertexShader9 **ppShader)
{
  if(ppShader == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DVertexShader9 *real = NULL;
  HRESULT ret = m_pDevice->GetVertexShader(&real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppShader = (IDirect3DVertexShader9 *)wrapper;
      (*ppShader)->AddRef();
    }
    else
    {
      *ppShader = real;
    }
    real->Release();
  }
  else
  {
    *ppShader = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Vertex shader constants
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetVertexShaderConstantF(
    UINT StartRegister, CONST float *pConstantData, UINT Vector4fCount)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetVertexShaderConstantF(StartRegister, pConstantData,
                                                                 Vector4fCount));

  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_F; i++)
      memcpy(m_RenderState.vsConstantsF[StartRegister + i], &pConstantData[i * 4],
             sizeof(float) * 4);
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetVertexShaderConstantF);
    Serialise_SetVertexShaderConstantF(ser, StartRegister, pConstantData, Vector4fCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetVertexShaderConstantI(StartRegister, pConstantData,
                                                                 Vector4iCount));

  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_I; i++)
      memcpy(m_RenderState.vsConstantsI[StartRegister + i], &pConstantData[i * 4], sizeof(int) * 4);
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetVertexShaderConstantI);
    Serialise_SetVertexShaderConstantI(ser, StartRegister, pConstantData, Vector4iCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetVertexShaderConstantB(StartRegister, pConstantData,
                                                                 BoolCount));

  if(pConstantData)
  {
    for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_B; i++)
      m_RenderState.vsConstantsB[StartRegister + i] = pConstantData[i];
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetVertexShaderConstantB);
    Serialise_SetVertexShaderConstantB(ser, StartRegister, pConstantData, BoolCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  IDirect3DVertexBuffer9 *realVB = pStreamData;
  if(pStreamData)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pStreamData);
    if(info)
      realVB = (IDirect3DVertexBuffer9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(
      ret = m_pDevice->SetStreamSource(StreamNumber, realVB, OffsetInBytes, Stride));

  if(StreamNumber < D3D9_MAX_STREAMS)
  {
    m_RenderState.streamSources[StreamNumber].offsetInBytes = OffsetInBytes;
    m_RenderState.streamSources[StreamNumber].stride = Stride;
  }

  if(IsActiveCapturing(m_State))
  {
    if(pStreamData)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pStreamData);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_Read);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetStreamSource);
    Serialise_SetStreamSource(ser, StreamNumber, pStreamData, OffsetInBytes, Stride);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetStreamSource(
    UINT StreamNumber, IDirect3DVertexBuffer9 **ppStreamData, UINT *pOffsetInBytes, UINT *pStride)
{
  if(ppStreamData == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DVertexBuffer9 *real = NULL;
  HRESULT ret = m_pDevice->GetStreamSource(StreamNumber, &real, pOffsetInBytes, pStride);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppStreamData = (IDirect3DVertexBuffer9 *)wrapper;
      (*ppStreamData)->AddRef();
      real->Release();
    }
    else
    {
      *ppStreamData = real;
    }
  }
  else
  {
    *ppStreamData = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber,
                                                                        UINT Setting)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetStreamSourceFreq(StreamNumber, Setting));

  if(StreamNumber < D3D9_MAX_STREAMS)
    m_RenderState.streamSources[StreamNumber].freq = Setting;

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetStreamSourceFreq);
    Serialise_SetStreamSourceFreq(ser, StreamNumber, Setting);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  IDirect3DIndexBuffer9 *realIB = pIndexData;
  if(pIndexData)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pIndexData);
    if(info)
      realIB = (IDirect3DIndexBuffer9 *)info->realObject;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetIndices(realIB));

  if(IsActiveCapturing(m_State))
  {
    if(pIndexData)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pIndexData);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_Read);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetIndices);
    Serialise_SetIndices(ser, pIndexData);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9 **ppIndexData)
{
  if(ppIndexData == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DIndexBuffer9 *real = NULL;
  HRESULT ret = m_pDevice->GetIndices(&real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppIndexData = (IDirect3DIndexBuffer9 *)wrapper;
      (*ppIndexData)->AddRef();
      real->Release();
    }
    else
    {
      *ppIndexData = real;
    }
  }
  else
  {
    *ppIndexData = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Pixel shader
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::CreatePixelShader(
    CONST DWORD *pFunction, IDirect3DPixelShader9 **ppShader)
{
  IDirect3DPixelShader9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreatePixelShader(pFunction, &real));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DPixelShader9 *wrappedPS = new WrappedIDirect3DPixelShader9(real, this);
    IDirect3DPixelShader9 *wrappedPtr = wrappedPS;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedPS->GetResourceID());
      record->resType = D3D9ResourceType::PixelShader;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreatePixelShader);
        Serialise_CreatePixelShader(ser, pFunction, &wrappedPtr);
        record->AddChunk(scope.Get());
      }
    }

    *ppShader = wrappedPS;
  }
  else
  {
    if(ppShader)
      *ppShader = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9 *pShader)
{
  IDirect3DPixelShader9 *realShader = NULL;
  if(pShader)
  {
    D3D9WrappedInfo *info = GetD3D9WrappedInfo(pShader);
    if(info)
      realShader = (IDirect3DPixelShader9 *)info->realObject;
    else
      realShader = pShader;
  }

  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetPixelShader(realShader));

  if(IsActiveCapturing(m_State))
  {
    if(pShader)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(pShader);
      if(info)
        GetResourceManager()->MarkResourceFrameReferenced(info->id, eFrameRef_Read);
    }

    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetPixelShader);
    Serialise_SetPixelShader(ser, pShader);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::GetPixelShader(
    IDirect3DPixelShader9 **ppShader)
{
  if(ppShader == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DPixelShader9 *real = NULL;
  HRESULT ret = m_pDevice->GetPixelShader(&real);

  if(SUCCEEDED(ret) && real)
  {
    if(GetResourceManager()->HasWrapper(real))
    {
      IUnknown *wrapper = GetResourceManager()->GetWrapper(real);
      *ppShader = (IDirect3DPixelShader9 *)wrapper;
      (*ppShader)->AddRef();
    }
    else
    {
      *ppShader = real;
    }
    real->Release();
  }
  else
  {
    *ppShader = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// IDirect3DDevice9 — Pixel shader constants
///////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE WrappedIDirect3DDevice9::SetPixelShaderConstantF(
    UINT StartRegister, CONST float *pConstantData, UINT Vector4fCount)
{
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetPixelShaderConstantF(StartRegister, pConstantData,
                                                                Vector4fCount));

  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_F; i++)
      memcpy(m_RenderState.psConstantsF[StartRegister + i], &pConstantData[i * 4],
             sizeof(float) * 4);
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetPixelShaderConstantF);
    Serialise_SetPixelShaderConstantF(ser, StartRegister, pConstantData, Vector4fCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetPixelShaderConstantI(StartRegister, pConstantData,
                                                                Vector4iCount));

  if(pConstantData)
  {
    for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_I; i++)
      memcpy(m_RenderState.psConstantsI[StartRegister + i], &pConstantData[i * 4], sizeof(int) * 4);
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetPixelShaderConstantI);
    Serialise_SetPixelShaderConstantI(ser, StartRegister, pConstantData, Vector4iCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->SetPixelShaderConstantB(StartRegister, pConstantData,
                                                                BoolCount));

  if(pConstantData)
  {
    for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_B; i++)
      m_RenderState.psConstantsB[StartRegister + i] = pConstantData[i];
  }

  if(IsActiveCapturing(m_State))
  {
    USE_SCRATCH_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D9Chunk::SetPixelShaderConstantB);
    Serialise_SetPixelShaderConstantB(ser, StartRegister, pConstantData, BoolCount);
    m_DeviceRecord->AddChunk(scope.Get());
  }

  return ret;
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
  // If ppQuery is NULL, the application is just testing if the query type is supported
  if(ppQuery == NULL)
    return m_pDevice->CreateQuery(Type, NULL);

  IDirect3DQuery9 *real = NULL;
  HRESULT ret;
  SERIALISE_TIME_CALL(ret = m_pDevice->CreateQuery(Type, &real));

  if(SUCCEEDED(ret))
  {
    WrappedIDirect3DQuery9 *wrappedQuery = new WrappedIDirect3DQuery9(real, this, Type);
    IDirect3DQuery9 *wrappedPtr = wrappedQuery;

    if(IsCaptureMode(m_State))
    {
      D3D9ResourceRecord *record =
          GetResourceManager()->AddResourceRecord(wrappedQuery->GetResourceID());
      record->resType = D3D9ResourceType::Query;
      record->Length = 0;

      {
        USE_SCRATCH_SERIALISER();
        SCOPED_SERIALISE_CHUNK(D3D9Chunk::CreateQuery);
        Serialise_CreateQuery(ser, Type, &wrappedPtr);
        record->AddChunk(scope.Get());
      }
    }

    *ppQuery = wrappedQuery;
  }
  else
  {
    *ppQuery = NULL;
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// Replay support
///////////////////////////////////////////////////////////////////////////

static rdcstr D3D9ChunkName(uint32_t idx)
{
  if((SystemChunk)idx < SystemChunk::FirstDriverChunk)
    return ToStr((SystemChunk)idx);

  return ToStr((D3D9Chunk)idx);
}

bool WrappedIDirect3DDevice9::ProcessChunk(ReadSerialiser &ser, D3D9Chunk chunk)
{
  switch(chunk)
  {
    case D3D9Chunk::DeviceInitialisation:
    {
      SERIALISE_ELEMENT_LOCAL(initParams, D3D9InitParams());
      SERIALISE_CHECK_READ_ERRORS();
      return true;
    }

    // Resource creation
    case D3D9Chunk::CreateTexture:
      return Serialise_CreateTexture(ser, 0, 0, 0, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, NULL, NULL);
    case D3D9Chunk::CreateVolumeTexture:
      return Serialise_CreateVolumeTexture(ser, 0, 0, 0, 0, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT,
                                           NULL, NULL);
    case D3D9Chunk::CreateCubeTexture:
      return Serialise_CreateCubeTexture(ser, 0, 0, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, NULL, NULL);
    case D3D9Chunk::CreateVertexBuffer:
      return Serialise_CreateVertexBuffer(ser, 0, 0, 0, D3DPOOL_DEFAULT, NULL, NULL);
    case D3D9Chunk::CreateIndexBuffer:
      return Serialise_CreateIndexBuffer(ser, 0, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, NULL, NULL);
    case D3D9Chunk::CreateRenderTarget:
      return Serialise_CreateRenderTarget(ser, 0, 0, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, 0, FALSE,
                                          NULL, NULL);
    case D3D9Chunk::CreateDepthStencilSurface:
      return Serialise_CreateDepthStencilSurface(ser, 0, 0, D3DFMT_UNKNOWN, D3DMULTISAMPLE_NONE, 0,
                                                 FALSE, NULL, NULL);
    case D3D9Chunk::CreateOffscreenPlainSurface:
      return Serialise_CreateOffscreenPlainSurface(ser, 0, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, NULL,
                                                   NULL);
    case D3D9Chunk::CreateVertexShader:
      return Serialise_CreateVertexShader(ser, NULL, NULL);
    case D3D9Chunk::CreatePixelShader:
      return Serialise_CreatePixelShader(ser, NULL, NULL);
    case D3D9Chunk::CreateVertexDeclaration:
      return Serialise_CreateVertexDeclaration(ser, NULL, NULL);
    case D3D9Chunk::CreateStateBlock:
      return Serialise_CreateStateBlock(ser, D3DSBT_ALL, NULL);
    case D3D9Chunk::CreateQuery:
      return Serialise_CreateQuery(ser, D3DQUERYTYPE_EVENT, NULL);

    // Annotations / markers
    case D3D9Chunk::SetMarker: return Serialise_SetMarker(ser, 0, NULL);
    case D3D9Chunk::PushMarker: return Serialise_PushMarker(ser, 0, NULL);
    case D3D9Chunk::PopMarker: return Serialise_PopMarker(ser);

    // Draw calls
    case D3D9Chunk::DrawPrimitive:
      return Serialise_DrawPrimitive(ser, D3DPT_TRIANGLELIST, 0, 0);
    case D3D9Chunk::DrawIndexedPrimitive:
      return Serialise_DrawIndexedPrimitive(ser, D3DPT_TRIANGLELIST, 0, 0, 0, 0, 0);
    case D3D9Chunk::DrawPrimitiveUP:
      return Serialise_DrawPrimitiveUP(ser, D3DPT_TRIANGLELIST, 0, NULL, 0);
    case D3D9Chunk::DrawIndexedPrimitiveUP:
      return Serialise_DrawIndexedPrimitiveUP(ser, D3DPT_TRIANGLELIST, 0, 0, 0, NULL,
                                              D3DFMT_INDEX16, NULL, 0);

    // Frame / scene
    case D3D9Chunk::Present: return Serialise_Present(ser, NULL, NULL, NULL, NULL);
    case D3D9Chunk::SwapChainPresent:
      return Serialise_SwapChainPresent(ser, NULL, NULL, NULL, NULL);
    case D3D9Chunk::BeginScene: return Serialise_BeginScene(ser);
    case D3D9Chunk::EndScene: return Serialise_EndScene(ser);
    case D3D9Chunk::Clear: return Serialise_Clear(ser, 0, NULL, 0, 0, 0.0f, 0);
    case D3D9Chunk::Reset: return Serialise_Reset(ser, NULL);

    // Render state
    case D3D9Chunk::SetRenderState:
      return Serialise_SetRenderState(ser, D3DRS_ZENABLE, 0);
    case D3D9Chunk::SetSamplerState:
      return Serialise_SetSamplerState(ser, 0, D3DSAMP_ADDRESSU, 0);
    case D3D9Chunk::SetTextureStageState:
      return Serialise_SetTextureStageState(ser, 0, D3DTSS_COLOROP, 0);
    case D3D9Chunk::SetTransform:
      return Serialise_SetTransform(ser, D3DTS_WORLD, NULL);
    case D3D9Chunk::SetViewport: return Serialise_SetViewport(ser, NULL);
    case D3D9Chunk::SetScissorRect: return Serialise_SetScissorRect(ser, NULL);
    case D3D9Chunk::SetClipPlane: return Serialise_SetClipPlane(ser, 0, NULL);
    case D3D9Chunk::SetMaterial: return Serialise_SetMaterial(ser, NULL);
    case D3D9Chunk::SetLight: return Serialise_SetLight(ser, 0, NULL);
    case D3D9Chunk::LightEnable: return Serialise_LightEnable(ser, 0, FALSE);
    case D3D9Chunk::SetNPatchMode: return Serialise_SetNPatchMode(ser, 0.0f);

    // Shader state
    case D3D9Chunk::SetVertexShader: return Serialise_SetVertexShader(ser, NULL);
    case D3D9Chunk::SetPixelShader: return Serialise_SetPixelShader(ser, NULL);
    case D3D9Chunk::SetVertexDeclaration: return Serialise_SetVertexDeclaration(ser, NULL);
    case D3D9Chunk::SetFVF: return Serialise_SetFVF(ser, 0);
    case D3D9Chunk::SetVertexShaderConstantF:
      return Serialise_SetVertexShaderConstantF(ser, 0, NULL, 0);
    case D3D9Chunk::SetVertexShaderConstantI:
      return Serialise_SetVertexShaderConstantI(ser, 0, NULL, 0);
    case D3D9Chunk::SetVertexShaderConstantB:
      return Serialise_SetVertexShaderConstantB(ser, 0, NULL, 0);
    case D3D9Chunk::SetPixelShaderConstantF:
      return Serialise_SetPixelShaderConstantF(ser, 0, NULL, 0);
    case D3D9Chunk::SetPixelShaderConstantI:
      return Serialise_SetPixelShaderConstantI(ser, 0, NULL, 0);
    case D3D9Chunk::SetPixelShaderConstantB:
      return Serialise_SetPixelShaderConstantB(ser, 0, NULL, 0);

    // Resource binding
    case D3D9Chunk::SetTexture: return Serialise_SetTexture(ser, 0, NULL);
    case D3D9Chunk::SetStreamSource: return Serialise_SetStreamSource(ser, 0, NULL, 0, 0);
    case D3D9Chunk::SetStreamSourceFreq: return Serialise_SetStreamSourceFreq(ser, 0, 0);
    case D3D9Chunk::SetIndices: return Serialise_SetIndices(ser, NULL);
    case D3D9Chunk::SetRenderTarget: return Serialise_SetRenderTarget(ser, 0, NULL);
    case D3D9Chunk::SetDepthStencilSurface: return Serialise_SetDepthStencilSurface(ser, NULL);

    // Resource data
    case D3D9Chunk::UpdateSurface: return Serialise_UpdateSurface(ser, NULL, NULL, NULL, NULL);
    case D3D9Chunk::UpdateTexture: return Serialise_UpdateTexture(ser, NULL, NULL);
    case D3D9Chunk::StretchRect:
      return Serialise_StretchRect(ser, NULL, NULL, NULL, NULL, D3DTEXF_NONE);
    case D3D9Chunk::ColorFill: return Serialise_ColorFill(ser, NULL, NULL, 0);
    case D3D9Chunk::GetRenderTargetData:
      return Serialise_GetRenderTargetData(ser, NULL, NULL);
    case D3D9Chunk::GetFrontBufferData: return Serialise_GetFrontBufferData(ser, 0, NULL);

    // State blocks
    case D3D9Chunk::BeginStateBlock: return Serialise_BeginStateBlock(ser);
    case D3D9Chunk::EndStateBlock: return Serialise_EndStateBlock(ser, NULL);
    case D3D9Chunk::StateBlockCapture: return Serialise_StateBlockCapture(ser, NULL);
    case D3D9Chunk::StateBlockApply: return Serialise_StateBlockApply(ser, NULL);

    // Queries
    case D3D9Chunk::QueryIssue: return Serialise_QueryIssue(ser, NULL, 0);

    // Misc
    case D3D9Chunk::SetSoftwareVertexProcessing:
      return Serialise_SetSoftwareVertexProcessing(ser, FALSE);

    // Unhandled chunks — skip gracefully
    case D3D9Chunk::SetResourceName:
    case D3D9Chunk::CreateDevice:
    case D3D9Chunk::CreateAdditionalSwapChain:
    case D3D9Chunk::LockRect:
    case D3D9Chunk::UnlockRect:
    case D3D9Chunk::LockBox:
    case D3D9Chunk::UnlockBox:
    case D3D9Chunk::LockVertexBuffer:
    case D3D9Chunk::UnlockVertexBuffer:
    case D3D9Chunk::LockIndexBuffer:
    case D3D9Chunk::UnlockIndexBuffer:
    case D3D9Chunk::QueryGetData:
    case D3D9Chunk::SetDialogBoxMode:
    case D3D9Chunk::ValidateDevice:
    case D3D9Chunk::SetCursorProperties:
    case D3D9Chunk::SetCursorPosition:
    case D3D9Chunk::ShowCursor:
    case D3D9Chunk::DrawRectPatch:
    case D3D9Chunk::DrawTriPatch:
      ser.SkipCurrentChunk();
      return true;

    default: break;
  }

  // handle system chunks
  SystemChunk system = (SystemChunk)chunk;
  if(system == SystemChunk::DriverInit)
  {
    SERIALISE_ELEMENT(m_InitParams);
    SERIALISE_CHECK_READ_ERRORS();
    return true;
  }
  else if(system == SystemChunk::InitialContentsList)
  {
    GetResourceManager()->CreateInitialContents(ser);
    return true;
  }
  else if(system == SystemChunk::InitialContents)
  {
    return GetResourceManager()->Serialise_InitialState(ser, ResourceId(), NULL, NULL);
  }
  else if(system == SystemChunk::CaptureScope)
  {
    return Serialise_CaptureScope(ser);
  }
  else if(system < SystemChunk::FirstDriverChunk)
  {
    RDCERR("Unexpected system chunk in capture data: %u", system);
    ser.SkipCurrentChunk();
    return true;
  }

  RDCERR("Unhandled D3D9 chunk type: %u", (uint32_t)chunk);
  ser.SkipCurrentChunk();
  return true;
}

RDResult WrappedIDirect3DDevice9::ReadLogInitialisation(RDCFile *rdc, bool storeStructuredBuffers)
{
  int sectionIdx = rdc->SectionIndex(SectionType::FrameCapture);
  if(sectionIdx < 0)
    RETURN_ERROR_RESULT(ResultCode::FileCorrupted, "File does not contain captured API data");

  StreamReader *reader = rdc->ReadSection(sectionIdx);

  if(reader->IsErrored())
  {
    RDResult result = reader->GetError();
    delete reader;
    return result;
  }

  ReadSerialiser ser(reader, Ownership::Stream);

  ser.SetStringDatabase(&m_StringDB);
  ser.SetUserData(GetResourceManager());

  ser.ConfigureStructuredExport(&D3D9ChunkName, storeStructuredBuffers, 0, 1.0);

  m_StructuredFile = &ser.GetStructuredFile();

  m_StoredStructuredData->version = m_StructuredFile->version = m_SectionVersion;

  ser.SetVersion(m_SectionVersion);

  m_FrameRecord.frameInfo.fileOffset = 0;
  m_FrameRecord.frameInfo.frameNumber = 0;

  // read through the log, populating the structured file and action list
  m_CurEventID = 0;
  m_CurActionID = 0;

  {
    m_ParentAction.children.clear();
    m_ActionStack.clear();
    m_ActionStack.push_back(&m_ParentAction);
  }

  uint64_t frameDataSize = 0;

  for(;;)
  {
    uint64_t offsetStart = reader->GetOffset();

    D3D9Chunk chunktype = ser.ReadChunk<D3D9Chunk>();

    if(reader->IsErrored())
      return RDResult(ResultCode::APIDataCorrupted, ser.GetError().message);

    bool success = ProcessChunk(ser, chunktype);

    ser.EndChunk();

    if(reader->IsErrored())
      return RDResult(ResultCode::APIDataCorrupted, ser.GetError().message);

    if(!success)
      return RDResult(ResultCode::APIDataCorrupted, "Failed to process chunk during replay");

    RenderDoc::Inst().SetProgress(LoadProgress::FileInitialRead,
                                  float(reader->GetOffset()) / float(reader->GetSize()));

    if((SystemChunk)chunktype == SystemChunk::CaptureScope)
    {
      m_FrameRecord.frameInfo.fileOffset = offsetStart;

      // read the remaining data into memory for frame replay
      frameDataSize = reader->GetSize() - reader->GetOffset();

      SAFE_DELETE(m_FrameReader);
      m_FrameReader = new StreamReader(reader, frameDataSize);

      GetResourceManager()->ApplyInitialContents();

      // Replay-side fallback: if render target 0 is not set (because the capture was made
      // before the capture-side fix that serializes initial RT/DS bindings, or the
      // backbuffer surface was never explicitly created in the capture), create a synthetic
      // render target surface and set it as the default output.
      if(m_RenderState.renderTargets[0] == ResourceId())
      {
        // First try to find an existing surface resource that looks like the backbuffer
        UINT bbWidth = m_InitParams.PresentationParameters.BackBufferWidth;
        UINT bbHeight = m_InitParams.PresentationParameters.BackBufferHeight;
        D3DFORMAT bbFormat = m_InitParams.PresentationParameters.BackBufferFormat;

        ResourceId bestRT;
        ResourceId bestDS;

        const auto &resMap = GetResourceManager()->GetResourceMap();
        for(auto it = resMap.begin(); it != resMap.end(); ++it)
        {
          IUnknown *res = it->second;
          if(!res)
            continue;

          D3D9WrappedInfo *info = GetD3D9WrappedInfo(res);
          if(!info || info->type != D3D9WrappedType::Surface)
            continue;

          WrappedIDirect3DSurface9 *surf = (WrappedIDirect3DSurface9 *)res;
          D3DSURFACE_DESC desc;
          if(SUCCEEDED(surf->GetReal()->GetDesc(&desc)))
          {
            bool sizeMatch = (bbWidth > 0 && bbHeight > 0 && desc.Width == bbWidth &&
                              desc.Height == bbHeight);

            if(sizeMatch && (desc.Usage & D3DUSAGE_RENDERTARGET))
              bestRT = it->first;
            else if(sizeMatch && (desc.Usage & D3DUSAGE_DEPTHSTENCIL))
              bestDS = it->first;
          }
        }

        // If no existing surface found, create a synthetic backbuffer render target
        if(bestRT == ResourceId() && bbWidth > 0 && bbHeight > 0)
        {
          if(bbFormat == D3DFMT_UNKNOWN)
            bbFormat = D3DFMT_A8R8G8B8;

          IDirect3DSurface9 *realRT = NULL;
          HRESULT hr = m_pDevice->CreateRenderTarget(bbWidth, bbHeight, bbFormat,
                                                     D3DMULTISAMPLE_NONE, 0, TRUE, &realRT, NULL);
          if(SUCCEEDED(hr) && realRT)
          {
            WrappedIDirect3DSurface9 *wrapped = new WrappedIDirect3DSurface9(realRT, this);
            bestRT = wrapped->GetResourceID();
            RDCLOG("Replay fallback: created synthetic backbuffer RT %s (%ux%u)",
                   ToStr(bestRT).c_str(), bbWidth, bbHeight);
          }
        }

        if(bestRT != ResourceId())
        {
          m_RenderState.renderTargets[0] = bestRT;
          RDCLOG("Replay fallback: set render target 0 to %s", ToStr(bestRT).c_str());
        }
        if(bestDS != ResourceId())
        {
          m_RenderState.depthStencil = bestDS;
          RDCLOG("Replay fallback: set depth stencil to %s", ToStr(bestDS).c_str());
        }
      }

      // first-pass read of frame contents to build action list
      {
        m_FrameReader->SetOffset(0);

        ReadSerialiser frameSer(m_FrameReader, Ownership::Nothing);
        frameSer.SetStringDatabase(&m_StringDB);
        frameSer.SetUserData(GetResourceManager());
        frameSer.SetVersion(m_SectionVersion);

        // configure structured export on the frame serialiser so that chunks are recorded
        // into the structured file, which AddEvent() needs for chunkIndex
        frameSer.ConfigureStructuredExport(&D3D9ChunkName, storeStructuredBuffers, 0, 1.0);

        // move the structured file (containing init chunks) into the frame serialiser
        // so that frame chunks are appended after init chunks
        frameSer.GetStructuredFile().Swap(*m_StructuredFile);

        m_StructuredFile = &frameSer.GetStructuredFile();

        m_State = CaptureState::LoadingReplaying;

        m_CurEventID = 1;

        for(;;)
        {
          if(m_FrameReader->AtEnd())
            break;

          m_CurChunkOffset = m_FrameReader->GetOffset();

          D3D9Chunk innerChunk = frameSer.ReadChunk<D3D9Chunk>();

          if(frameSer.GetReader()->IsErrored())
            return RDResult(ResultCode::APIDataCorrupted, frameSer.GetError().message);

          if(innerChunk == D3D9Chunk::Max || frameSer.GetReader()->AtEnd())
          {
            frameSer.EndChunk();
            break;
          }

          success = ProcessChunk(frameSer, innerChunk);

          frameSer.EndChunk();

          if(frameSer.GetReader()->IsErrored())
            return RDResult(ResultCode::APIDataCorrupted, frameSer.GetError().message);

          if(!success)
            return RDResult(ResultCode::APIDataCorrupted,
                            "Failed to process chunk during frame replay");

          m_CurEventID++;
        }

        // swap the structured data back out of the frame serialiser
        m_StructuredFile->Swap(*m_StoredStructuredData);

        m_StructuredFile = m_StoredStructuredData;
      }
    }

    if((SystemChunk)chunktype == SystemChunk::CaptureScope || reader->IsErrored() || reader->AtEnd())
      break;
  }

  // swap the loaded action list into the frame record
  m_FrameRecord.actionList.swap(m_ParentAction.children);

  SetupActionPointers(m_ActionTable, m_FrameRecord.actionList);

  GetReplay()->WriteFrameRecord() = m_FrameRecord;

  m_FrameRecord.frameInfo.uncompressedFileSize =
      rdc->GetSectionProperties(sectionIdx).uncompressedSize;
  m_FrameRecord.frameInfo.compressedFileSize =
      rdc->GetSectionProperties(sectionIdx).compressedSize;
  m_FrameRecord.frameInfo.persistentSize = frameDataSize;

  return ResultCode::Succeeded;
}

void WrappedIDirect3DDevice9::ReplayLog(uint32_t startEventID, uint32_t endEventID,
                                        ReplayLogType replayType)
{
  bool partial = true;

  if(startEventID == 0 && (replayType == eReplay_WithoutDraw || replayType == eReplay_Full))
  {
    startEventID = 1;
    partial = false;
  }

  if(!partial)
    GetResourceManager()->ApplyInitialContents();

  m_State = CaptureState::ActiveReplaying;

  if(replayType == eReplay_Full)
  {
    // replay is: startEventID to endEventID inclusive
  }
  else if(replayType == eReplay_WithoutDraw)
  {
    // replay is: startEventID to endEventID-1 inclusive
    if(endEventID > 0)
      endEventID = RDCMAX(1U, endEventID) - 1;
  }
  else if(replayType == eReplay_OnlyDraw)
  {
    // replay only the single endEventID
    startEventID = endEventID;
  }

  if(!m_FrameReader)
  {
    RDCERR("Can't replay without frame reader");
    return;
  }

  m_FrameReader->SetOffset(0);

  ReadSerialiser ser(m_FrameReader, Ownership::Nothing);
  ser.SetStringDatabase(&m_StringDB);
  ser.SetUserData(GetResourceManager());
  ser.SetVersion(m_SectionVersion);

  m_CurEventID = 1;

  for(;;)
  {
    if(m_CurEventID > endEventID || m_FrameReader->AtEnd())
      break;

    m_CurChunkOffset = ser.GetReader()->GetOffset();

    D3D9Chunk chunktype = ser.ReadChunk<D3D9Chunk>();

    if(ser.GetReader()->IsErrored() || chunktype == D3D9Chunk::Max || ser.GetReader()->AtEnd())
    {
      ser.EndChunk();
      break;
    }

    if(m_CurEventID >= startEventID && m_CurEventID <= endEventID)
    {
      ProcessChunk(ser, chunktype);
    }
    else
    {
      ser.SkipCurrentChunk();
    }

    ser.EndChunk();

    m_CurEventID++;
  }

  m_State = CaptureState::LoadingReplaying;
}

const ActionDescription *WrappedIDirect3DDevice9::GetAction(uint32_t eventId)
{
  if(eventId < m_ActionTable.size())
    return m_ActionTable[eventId];

  return NULL;
}
