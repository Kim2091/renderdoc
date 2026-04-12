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

#include "d3d9_d3d9.h"
#include "d3d9_device.h"

WrappedIDirect3D9::WrappedIDirect3D9(IDirect3D9 *real) : m_pReal(real), m_RefCount(1)
{
  RDCLOG("Creating WrappedIDirect3D9 around %p", real);
}

WrappedIDirect3D9::~WrappedIDirect3D9()
{
  RDCLOG("Destroying WrappedIDirect3D9");
  SAFE_RELEASE(m_pReal);
}

////////////////////////////////////////////////////////////////
// IUnknown

ULONG STDMETHODCALLTYPE WrappedIDirect3D9::AddRef()
{
  Atomic::Inc32(&m_RefCount);
  return (ULONG)m_RefCount;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3D9::Release()
{
  Atomic::Dec32(&m_RefCount);
  if(m_RefCount == 0)
  {
    delete this;
    return 0;
  }
  return (ULONG)m_RefCount;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::QueryInterface(REFIID riid, void **ppvObject)
{
  if(riid == IID_IDirect3D9)
  {
    AddRef();
    *ppvObject = static_cast<IDirect3D9 *>(this);
    return S_OK;
  }

  if(riid == IID_IUnknown)
  {
    AddRef();
    *ppvObject = static_cast<IUnknown *>(this);
    return S_OK;
  }

  return m_pReal->QueryInterface(riid, ppvObject);
}

////////////////////////////////////////////////////////////////
// IDirect3D9 — enumeration / caps (passthrough)

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::RegisterSoftwareDevice(void *pInitializeFunction)
{
  return m_pReal->RegisterSoftwareDevice(pInitializeFunction);
}

UINT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterCount()
{
  return m_pReal->GetAdapterCount();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterIdentifier(
    UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
  return m_pReal->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
}

UINT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format)
{
  return m_pReal->GetAdapterModeCount(Adapter, Format);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::EnumAdapterModes(UINT Adapter, D3DFORMAT Format,
                                                               UINT Mode, D3DDISPLAYMODE *pMode)
{
  return m_pReal->EnumAdapterModes(Adapter, Format, Mode, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterDisplayMode(UINT Adapter,
                                                                    D3DDISPLAYMODE *pMode)
{
  return m_pReal->GetAdapterDisplayMode(Adapter, pMode);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType,
                                                              D3DFORMAT AdapterFormat,
                                                              D3DFORMAT BackBufferFormat,
                                                              BOOL bWindowed)
{
  return m_pReal->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType,
                                                                D3DFORMAT AdapterFormat,
                                                                DWORD Usage,
                                                                D3DRESOURCETYPE RType,
                                                                D3DFORMAT CheckFormat)
{
  return m_pReal->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceMultiSampleType(
    UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed,
    D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
  return m_pReal->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed,
                                             MultiSampleType, pQualityLevels);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDepthStencilMatch(UINT Adapter,
                                                                     D3DDEVTYPE DeviceType,
                                                                     D3DFORMAT AdapterFormat,
                                                                     D3DFORMAT RenderTargetFormat,
                                                                     D3DFORMAT DepthStencilFormat)
{
  return m_pReal->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat,
                                         DepthStencilFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CheckDeviceFormatConversion(UINT Adapter,
                                                                          D3DDEVTYPE DeviceType,
                                                                          D3DFORMAT SourceFormat,
                                                                          D3DFORMAT TargetFormat)
{
  return m_pReal->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType,
                                                            D3DCAPS9 *pCaps)
{
  return m_pReal->GetDeviceCaps(Adapter, DeviceType, pCaps);
}

HMONITOR STDMETHODCALLTYPE WrappedIDirect3D9::GetAdapterMonitor(UINT Adapter)
{
  return m_pReal->GetAdapterMonitor(Adapter);
}

////////////////////////////////////////////////////////////////
// IDirect3D9 — device creation (wrapped)

HRESULT STDMETHODCALLTYPE WrappedIDirect3D9::CreateDevice(
    UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS *pPresentationParameters,
    IDirect3DDevice9 **ppReturnedDeviceInterface)
{
  RDCLOG("WrappedIDirect3D9::CreateDevice called (Adapter=%u, DeviceType=%d)", Adapter,
         (int)DeviceType);

  if(ppReturnedDeviceInterface == NULL)
    return m_pReal->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                                 pPresentationParameters, ppReturnedDeviceInterface);

  IDirect3DDevice9 *realDevice = NULL;
  HRESULT ret = m_pReal->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                                      pPresentationParameters, &realDevice);

  if(SUCCEEDED(ret))
  {
    RDCLOG("Wrapping IDirect3DDevice9 %p", realDevice);

    WrappedIDirect3DDevice9 *wrappedDevice =
        new WrappedIDirect3DDevice9(realDevice, this, pPresentationParameters);

    *ppReturnedDeviceInterface = wrappedDevice;
  }
  else
  {
    RDCERR("IDirect3D9::CreateDevice failed: HRESULT %08x", (uint32_t)ret);
    *ppReturnedDeviceInterface = NULL;
  }

  return ret;
}
