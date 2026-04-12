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

#pragma once

#include <set>
#include <stdint.h>
#include "common/threading.h"
#include "common/timing.h"
#include "core/core.h"
#include "replay/replay_driver.h"
#include "d3d9_common.h"
#include "d3d9_manager.h"
#include "d3d9_renderstate.h"

// Forward declarations
class WrappedIDirect3D9;
class D3D9Replay;

struct D3D9InitParams
{
  D3DDEVTYPE DeviceType = D3DDEVTYPE_HAL;
  DWORD BehaviorFlags = 0;
  D3DPRESENT_PARAMETERS PresentationParameters = {};

  // check if a frame capture section version is supported
  static const uint64_t CurrentVersion = 0x1;
  static bool IsSupportedVersion(uint64_t ver) { return ver == CurrentVersion; }
};

DECLARE_REFLECTION_STRUCT(D3D9InitParams);

class WrappedIDirect3DDevice9 : public IDirect3DDevice9, public IFrameCapturer
{
private:
  IDirect3DDevice9 *m_pDevice;    // real device
  WrappedIDirect3D9 *m_pD3D9;     // parent factory (forward-declared)
  int32_t m_RefCount;

  ResourceId m_ResourceID;
  CaptureState m_State;

  D3D9ResourceManager *m_ResourceManager;
  D3D9RenderState m_RenderState;

  D3D9Replay *m_Replay;

  WriteSerialiser m_ScratchSerialiser;
  D3D9ResourceRecord *m_DeviceRecord;

  D3D9InitParams m_InitParams;

  rdcarray<FrameDescription> m_CapturedFrames;
  uint32_t m_FrameCounter;

  Threading::CriticalSection m_D3DLock;

  bool m_StateBlockRecording;

  SDFile *m_StructuredFile;
  SDFile *m_StoredStructuredData;
  std::set<rdcstr> m_StringDB;

  uint64_t m_SectionVersion;

  PerformanceTimer m_CaptureTimer;

  FrameRecord m_FrameRecord;
  rdcarray<ActionDescription *> m_ActionTable;

  // Event/action tracking for replay
  uint32_t m_CurEventID;
  uint32_t m_CurActionID;
  uint64_t m_CurChunkOffset;
  rdcarray<APIEvent> m_CurEvents;
  rdcarray<ActionDescription *> m_ActionStack;
  ActionDescription m_ParentAction;

public:
  WrappedIDirect3DDevice9(IDirect3DDevice9 *real, WrappedIDirect3D9 *d3d9,
                          D3DPRESENT_PARAMETERS *pPresentationParameters);
  WrappedIDirect3DDevice9(IDirect3DDevice9 *real, const D3D9InitParams &params);
  virtual ~WrappedIDirect3DDevice9();

  ////////////////////////////////////////////////////////////////
  // Accessors

  IDirect3DDevice9 *GetReal() { return m_pDevice; }
  D3D9ResourceManager *GetResourceManager() { return m_ResourceManager; }
  D3D9RenderState &GetRenderState() { return m_RenderState; }
  const CaptureState &GetState() { return m_State; }
  ResourceId GetResourceID() { return m_ResourceID; }
  WriteSerialiser &GetScratchSerialiser() { return m_ScratchSerialiser; }
  D3D9ResourceRecord *GetDeviceRecord() { return m_DeviceRecord; }
  Threading::CriticalSection &D3DLock() { return m_D3DLock; }

  SDFile *GetStructuredFile() { return m_StructuredFile; }
  SDFile *DetachStructuredFile()
  {
    SDFile *ret = m_StoredStructuredData;
    m_StoredStructuredData = m_StructuredFile = new SDFile;
    return ret;
  }

  D3D9Replay *GetReplay() { return m_Replay; }

  void IncrementFrameCounter() { m_FrameCounter++; }
  uint32_t GetFrameCounter() const { return m_FrameCounter; }

  // SwapChain Present serialization (called from WrappedIDirect3DSwapChain9)
  template <typename SerialiserType>
  bool Serialise_SwapChainPresent(SerialiserType &ser, const RECT *pSourceRect,
                                  const RECT *pDestRect, HWND hDestWindowOverride,
                                  const RGNDATA *pDirtyRegion);

  ////////////////////////////////////////////////////////////////
  // Replay support

  RDResult ReadLogInitialisation(RDCFile *rdc, bool storeStructuredBuffers);
  void ReplayLog(uint32_t startEventID, uint32_t endEventID, ReplayLogType replayType);

  const ActionDescription *GetAction(uint32_t eventId);

  FrameRecord &GetFrameRecord() { return m_FrameRecord; }

  APIProperties APIProps;

  ////////////////////////////////////////////////////////////////
  // Event/Action tracking for replay

  void AddEvent();
  void AddAction(const ActionDescription &a);

  ////////////////////////////////////////////////////////////////
  // IFrameCapturer interface

  RDCDriver GetFrameCaptureDriver() override { return RDCDriver::D3D9; }
  void StartFrameCapture(DeviceOwnedWindow devWnd) override;
  bool EndFrameCapture(DeviceOwnedWindow devWnd) override;
  bool DiscardFrameCapture(DeviceOwnedWindow devWnd) override;
  uint32_t SetObjectAnnotation(void *object, const char *key,
                               RENDERDOC_AnnotationType valueType, uint32_t valueVectorWidth,
                               const RENDERDOC_AnnotationValue *value) override;
  uint32_t SetCommandAnnotation(void *queueOrCommandBuffer, const char *key,
                                RENDERDOC_AnnotationType valueType, uint32_t valueVectorWidth,
                                const RENDERDOC_AnnotationValue *value) override;

  ////////////////////////////////////////////////////////////////
  // Frame helpers

  void BeginCaptureFrame();
  void EndCaptureFrame();

  template <typename SerialiserType>
  bool Serialise_CaptureScope(SerialiserType &ser);

  ////////////////////////////////////////////////////////////////
  // Initial state helpers

  bool Prepare_InitialState(IUnknown *res);
  void Create_InitialState(ResourceId id, IUnknown *live, bool hasData);
  void Apply_InitialState(IUnknown *live, D3D9InitialContents &data);

  template <typename SerialiserType>
  bool Serialise_InitialState(SerialiserType &ser, ResourceId id, D3D9ResourceRecord *record,
                              const D3D9InitialContents *initial);

  ////////////////////////////////////////////////////////////////
  // State block helpers

  bool IsStateBlockRecording() const { return m_StateBlockRecording; }
  void SetStateBlockRecording(bool recording) { m_StateBlockRecording = recording; }

  ////////////////////////////////////////////////////////////////
  // D3DPERF static methods

  static void SetMarker(uint32_t col, const wchar_t *name);
  static int BeginEvent(uint32_t col, const wchar_t *name);
  static int EndEvent();

  ////////////////////////////////////////////////////////////////
  // IUnknown

  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

  ////////////////////////////////////////////////////////////////
  // IDirect3DDevice9 methods

  // -- Lifecycle / status --
  HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
  UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
  HRESULT STDMETHODCALLTYPE EvictManagedResources() override;
  HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9 **ppD3D9) override;
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *pCaps) override;
  HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE *pMode) override;
  HRESULT STDMETHODCALLTYPE GetCreationParameters(
      D3DDEVICE_CREATION_PARAMETERS *pParameters) override;

  // -- Cursor --
  HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot,
                                                 IDirect3DSurface9 *pCursorBitmap) override;
  void STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) override;
  BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) override;

  // -- Swap chain --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateAdditionalSwapChain,
                                D3DPRESENT_PARAMETERS *pPresentationParameters,
                                IDirect3DSwapChain9 **pSwapChain);

  HRESULT STDMETHODCALLTYPE GetSwapChain(UINT iSwapChain,
                                          IDirect3DSwapChain9 **pSwapChain) override;
  UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override;

  // -- Reset / Present --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, Reset,
                                D3DPRESENT_PARAMETERS *pPresentationParameters);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, Present, CONST RECT *pSourceRect,
                                CONST RECT *pDestRect, HWND hDestWindowOverride,
                                CONST RGNDATA *pDirtyRegion);

  HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT iSwapChain, UINT iBackBuffer,
                                           D3DBACKBUFFER_TYPE Type,
                                           IDirect3DSurface9 **ppBackBuffer) override;
  HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT iSwapChain,
                                             D3DRASTER_STATUS *pRasterStatus) override;

  HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL bEnableDialogs) override;

  void STDMETHODCALLTYPE SetGammaRamp(UINT iSwapChain, DWORD Flags,
                                       CONST D3DGAMMARAMP *pRamp) override;
  void STDMETHODCALLTYPE GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP *pRamp) override;

  // -- Resource creation (serialised) --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateTexture, UINT Width, UINT Height,
                                UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateVolumeTexture, UINT Width,
                                UINT Height, UINT Depth, UINT Levels, DWORD Usage,
                                D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateCubeTexture, UINT EdgeLength,
                                UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DCubeTexture9 **ppCubeTexture, HANDLE *pSharedHandle);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateVertexBuffer, UINT Length,
                                DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateIndexBuffer, UINT Length,
                                DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateRenderTarget, UINT Width,
                                UINT Height, D3DFORMAT Format,
                                D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                BOOL Lockable, IDirect3DSurface9 **ppSurface,
                                HANDLE *pSharedHandle);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateDepthStencilSurface, UINT Width,
                                UINT Height, D3DFORMAT Format,
                                D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                BOOL Discard, IDirect3DSurface9 **ppSurface,
                                HANDLE *pSharedHandle);

  // -- Surface operations (serialised) --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, UpdateSurface,
                                IDirect3DSurface9 *pSourceSurface, CONST RECT *pSourceRect,
                                IDirect3DSurface9 *pDestinationSurface, CONST POINT *pDestPoint);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, UpdateTexture,
                                IDirect3DBaseTexture9 *pSourceTexture,
                                IDirect3DBaseTexture9 *pDestinationTexture);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, GetRenderTargetData,
                                IDirect3DSurface9 *pRenderTarget,
                                IDirect3DSurface9 *pDestSurface);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, GetFrontBufferData, UINT iSwapChain,
                                IDirect3DSurface9 *pDestSurface);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, StretchRect,
                                IDirect3DSurface9 *pSourceSurface, CONST RECT *pSourceRect,
                                IDirect3DSurface9 *pDestSurface, CONST RECT *pDestRect,
                                D3DTEXTUREFILTERTYPE Filter);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, ColorFill,
                                IDirect3DSurface9 *pSurface, CONST RECT *pRect, D3DCOLOR color);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateOffscreenPlainSurface, UINT Width,
                                UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle);

  // -- Render targets / depth (serialised Set, passthrough Get) --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetRenderTarget,
                                DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget);
  HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex,
                                             IDirect3DSurface9 **ppRenderTarget) override;

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetDepthStencilSurface,
                                IDirect3DSurface9 *pNewZStencil);
  HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(
      IDirect3DSurface9 **ppZStencilSurface) override;

  // -- Scene --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, BeginScene);
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, EndScene);

  // -- Clear --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, Clear, DWORD Count,
                                CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z,
                                DWORD Stencil);

  // -- Transforms --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetTransform,
                                D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix);
  HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State,
                                          D3DMATRIX *pMatrix) override;
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, MultiplyTransform,
                                D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix);

  // -- Viewport --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetViewport,
                                CONST D3DVIEWPORT9 *pViewport);
  HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 *pViewport) override;

  // -- Material --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetMaterial,
                                CONST D3DMATERIAL9 *pMaterial);
  HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 *pMaterial) override;

  // -- Lights --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetLight, DWORD Index,
                                CONST D3DLIGHT9 *pLight);
  HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT9 *pLight) override;
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, LightEnable, DWORD Index, BOOL Enable);
  HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL *pEnable) override;

  // -- Clip planes --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetClipPlane, DWORD Index,
                                CONST float *pPlane);
  HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float *pPlane) override;

  // -- Render state --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetRenderState,
                                D3DRENDERSTATETYPE State, DWORD Value);
  HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) override;

  // -- State blocks --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateStateBlock,
                                D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppSB);
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, BeginStateBlock);
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, EndStateBlock,
                                IDirect3DStateBlock9 **ppSB);

  // State block event serialization (called from WrappedIDirect3DStateBlock9)
  template <typename SerialiserType>
  bool Serialise_StateBlockCapture(SerialiserType &ser, IDirect3DStateBlock9 *pSB);
  template <typename SerialiserType>
  bool Serialise_StateBlockApply(SerialiserType &ser, IDirect3DStateBlock9 *pSB);

  // -- Clip status --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetClipStatus,
                                CONST D3DCLIPSTATUS9 *pClipStatus);
  HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 *pClipStatus) override;

  // -- Textures --
  HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture9 **ppTexture) override;
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetTexture, DWORD Stage,
                                IDirect3DBaseTexture9 *pTexture);

  // -- Texture stage state --
  HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type,
                                                  DWORD *pValue) override;
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetTextureStageState, DWORD Stage,
                                D3DTEXTURESTAGESTATETYPE Type, DWORD Value);

  // -- Sampler state --
  HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type,
                                             DWORD *pValue) override;
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetSamplerState, DWORD Sampler,
                                D3DSAMPLERSTATETYPE Type, DWORD Value);

  // -- Validate --
  HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *pNumPasses) override;

  // -- Palette --
  HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber,
                                               CONST PALETTEENTRY *pEntries) override;
  HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber,
                                               PALETTEENTRY *pEntries) override;
  HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override;
  HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *PaletteNumber) override;

  // -- Scissor rect --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetScissorRect, CONST RECT *pRect);
  HRESULT STDMETHODCALLTYPE GetScissorRect(RECT *pRect) override;

  // -- Software vertex processing --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetSoftwareVertexProcessing,
                                BOOL bSoftware);
  BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override;

  // -- N-Patch --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetNPatchMode, float nSegments);
  float STDMETHODCALLTYPE GetNPatchMode() override;

  // -- Draw calls (serialised) --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, DrawPrimitive,
                                D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex,
                                UINT PrimitiveCount);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, DrawIndexedPrimitive,
                                D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex,
                                UINT MinVertexIndex, UINT NumVertices, UINT startIndex,
                                UINT primCount);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, DrawPrimitiveUP,
                                D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, DrawIndexedPrimitiveUP,
                                D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
                                UINT NumVertices, UINT PrimitiveCount, CONST void *pIndexData,
                                D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData,
                                UINT VertexStreamZeroStride);

  // -- Process vertices (passthrough) --
  HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount,
                                             IDirect3DVertexBuffer9 *pDestBuffer,
                                             IDirect3DVertexDeclaration9 *pVertexDecl,
                                             DWORD Flags) override;

  // -- Vertex declaration --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateVertexDeclaration,
                                CONST D3DVERTEXELEMENT9 *pVertexElements,
                                IDirect3DVertexDeclaration9 **ppDecl);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetVertexDeclaration,
                                IDirect3DVertexDeclaration9 *pDecl);
  HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 **ppDecl) override;

  // -- FVF --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetFVF, DWORD FVF);
  HRESULT STDMETHODCALLTYPE GetFVF(DWORD *pFVF) override;

  // -- Vertex shader --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateVertexShader,
                                CONST DWORD *pFunction, IDirect3DVertexShader9 **ppShader);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetVertexShader,
                                IDirect3DVertexShader9 *pShader);
  HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 **ppShader) override;

  // -- Vertex shader constants --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetVertexShaderConstantF,
                                UINT StartRegister, CONST float *pConstantData,
                                UINT Vector4fCount);
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT StartRegister, float *pConstantData,
                                                      UINT Vector4fCount) override;

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetVertexShaderConstantI,
                                UINT StartRegister, CONST int *pConstantData,
                                UINT Vector4iCount);
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT StartRegister, int *pConstantData,
                                                      UINT Vector4iCount) override;

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetVertexShaderConstantB,
                                UINT StartRegister, CONST BOOL *pConstantData, UINT BoolCount);
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT StartRegister, BOOL *pConstantData,
                                                      UINT BoolCount) override;

  // -- Stream sources --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetStreamSource, UINT StreamNumber,
                                IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes,
                                UINT Stride);
  HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber,
                                             IDirect3DVertexBuffer9 **ppStreamData,
                                             UINT *pOffsetInBytes, UINT *pStride) override;

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetStreamSourceFreq,
                                UINT StreamNumber, UINT Setting);
  HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT StreamNumber, UINT *pSetting) override;

  // -- Index buffer --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetIndices,
                                IDirect3DIndexBuffer9 *pIndexData);
  HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 **ppIndexData) override;

  // -- Pixel shader --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreatePixelShader,
                                CONST DWORD *pFunction, IDirect3DPixelShader9 **ppShader);

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetPixelShader,
                                IDirect3DPixelShader9 *pShader);
  HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 **ppShader) override;

  // -- Pixel shader constants --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetPixelShaderConstantF,
                                UINT StartRegister, CONST float *pConstantData,
                                UINT Vector4fCount);
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT StartRegister, float *pConstantData,
                                                     UINT Vector4fCount) override;

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetPixelShaderConstantI,
                                UINT StartRegister, CONST int *pConstantData,
                                UINT Vector4iCount);
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT StartRegister, int *pConstantData,
                                                     UINT Vector4iCount) override;

  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, SetPixelShaderConstantB,
                                UINT StartRegister, CONST BOOL *pConstantData, UINT BoolCount);
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT StartRegister, BOOL *pConstantData,
                                                     UINT BoolCount) override;

  // -- Patches (passthrough) --
  HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, CONST float *pNumSegs,
                                           CONST D3DRECTPATCH_INFO *pRectPatchInfo) override;
  HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, CONST float *pNumSegs,
                                          CONST D3DTRIPATCH_INFO *pTriPatchInfo) override;
  HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override;

  // -- Query --
  IMPLEMENT_FUNCTION_SERIALISED(HRESULT STDMETHODCALLTYPE, CreateQuery, D3DQUERYTYPE Type,
                                IDirect3DQuery9 **ppQuery);

  // Query event serialization (called from WrappedIDirect3DQuery9)
  template <typename SerialiserType>
  bool Serialise_QueryIssue(SerialiserType &ser, IDirect3DQuery9 *pQuery, DWORD dwIssueFlags);
};
