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

#include "d3d9_swapchain.h"
#include "d3d9_resources.h"

WrappedIDirect3DSwapChain9::WrappedIDirect3DSwapChain9(IDirect3DSwapChain9 *real,
                                                       WrappedIDirect3DDevice9 *device)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  m_ID = ResourceIDGen::GetNewUniqueID();

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DSwapChain9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DSwapChain9::~WrappedIDirect3DSwapChain9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

//////////////////////////////
// IUnknown
//////////////////////////////

ULONG STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::Release()
{
  int32_t ref = Atomic::Dec32(&m_ExtRef);
  RDCASSERT(ref >= 0);

  if(ref == 0 && m_IntRef == 0)
  {
    delete this;
    return 0;
  }

  return (ULONG)ref;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(riid == __uuidof(IDirect3DSwapChain9))
  {
    AddRef();
    *ppvObj = this;
    return S_OK;
  }

  if(riid == __uuidof(IUnknown))
  {
    AddRef();
    *ppvObj = (IUnknown *)this;
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

//////////////////////////////
// IDirect3DSwapChain9
//////////////////////////////

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::Present(CONST RECT *pSourceRect,
                                                               CONST RECT *pDestRect,
                                                               HWND hDestWindowOverride,
                                                               CONST RGNDATA *pDirtyRegion,
                                                               DWORD dwFlags)
{
  // Increment the device's frame counter
  m_pDevice->IncrementFrameCounter();

  if(IsActiveCapturing(m_pDevice->GetState()))
  {
    // Serialize SwapChainPresent as the last event
    {
      WriteSerialiser &ser = m_pDevice->GetScratchSerialiser();
      SCOPED_SERIALISE_CHUNK(D3D9Chunk::SwapChainPresent);
      m_pDevice->Serialise_SwapChainPresent(ser, pSourceRect, pDestRect, hDestWindowOverride,
                                            pDirtyRegion);
      m_pDevice->GetDeviceRecord()->AddChunk(scope.Get());
    }

    // End the frame capture
    RenderDoc::Inst().EndFrameCapture(
        DeviceOwnedWindow((void *)m_pDevice, NULL));
  }
  else
  {
    // Check if a capture was requested
    if(RenderDoc::Inst().ShouldTriggerCapture(m_pDevice->GetFrameCounter()))
    {
      RenderDoc::Inst().StartFrameCapture(
          DeviceOwnedWindow((void *)m_pDevice, NULL));
    }
  }

  return m_pReal->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetFrontBufferData(
    IDirect3DSurface9 *pDestSurface)
{
  // Unwrap the surface if wrapped
  IDirect3DSurface9 *realSurface = pDestSurface;
  if(pDestSurface)
  {
    WrappedIDirect3DSurface9 *wrappedSurf =
        dynamic_cast<WrappedIDirect3DSurface9 *>(pDestSurface);
    if(wrappedSurf)
      realSurface = wrappedSurf->GetReal();
  }
  return m_pReal->GetFrontBufferData(realSurface);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetBackBuffer(
    UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer)
{
  IDirect3DSurface9 *realSurface = NULL;
  HRESULT ret = m_pReal->GetBackBuffer(iBackBuffer, Type, &realSurface);

  if(SUCCEEDED(ret) && realSurface)
  {
    // Check if we already have a wrapper for this surface
    IUnknown *existing = m_pDevice->GetResourceManager()->GetWrapper(realSurface);
    if(existing)
    {
      WrappedIDirect3DSurface9 *wrapped = dynamic_cast<WrappedIDirect3DSurface9 *>(existing);
      if(wrapped)
      {
        wrapped->AddRef();
        realSurface->Release();    // release the ref from GetBackBuffer
        *ppBackBuffer = wrapped;
        return ret;
      }
    }

    // Create a new wrapper for this back buffer surface
    WrappedIDirect3DSurface9 *wrapped =
        new WrappedIDirect3DSurface9(realSurface, m_pDevice, this);
    *ppBackBuffer = wrapped;
  }
  else
  {
    if(ppBackBuffer)
      *ppBackBuffer = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetRasterStatus(
    D3DRASTER_STATUS *pRasterStatus)
{
  return m_pReal->GetRasterStatus(pRasterStatus);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetDisplayMode(D3DDISPLAYMODE *pMode)
{
  return m_pReal->GetDisplayMode(pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice)
  {
    m_pDevice->AddRef();
    *ppDevice = m_pDevice;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSwapChain9::GetPresentParameters(
    D3DPRESENT_PARAMETERS *pPresentationParameters)
{
  return m_pReal->GetPresentParameters(pPresentationParameters);
}
