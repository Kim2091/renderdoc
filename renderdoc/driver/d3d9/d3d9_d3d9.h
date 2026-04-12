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

#include "d3d9_common.h"

class WrappedIDirect3D9 : public IDirect3D9
{
public:
  WrappedIDirect3D9(IDirect3D9 *real);
  virtual ~WrappedIDirect3D9();

  IDirect3D9 *GetReal() { return m_pReal; }

  ////////////////////////////////////////////////////////////////
  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override;

  ////////////////////////////////////////////////////////////////
  // IDirect3D9 — enumeration / caps (passthrough)
  HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *pInitializeFunction) override;
  UINT STDMETHODCALLTYPE GetAdapterCount() override;
  HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags,
                                                   D3DADAPTER_IDENTIFIER9 *pIdentifier) override;
  UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override;
  HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode,
                                              D3DDISPLAYMODE *pMode) override;
  HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType,
                                             D3DFORMAT AdapterFormat,
                                             D3DFORMAT BackBufferFormat, BOOL bWindowed) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType,
                                               D3DFORMAT AdapterFormat, DWORD Usage,
                                               D3DRESOURCETYPE RType,
                                               D3DFORMAT CheckFormat) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType,
                                                        D3DFORMAT SurfaceFormat, BOOL Windowed,
                                                        D3DMULTISAMPLE_TYPE MultiSampleType,
                                                        DWORD *pQualityLevels) override;
  HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType,
                                                    D3DFORMAT AdapterFormat,
                                                    D3DFORMAT RenderTargetFormat,
                                                    D3DFORMAT DepthStencilFormat) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType,
                                                         D3DFORMAT SourceFormat,
                                                         D3DFORMAT TargetFormat) override;
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType,
                                           D3DCAPS9 *pCaps) override;
  HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override;

  ////////////////////////////////////////////////////////////////
  // IDirect3D9 — device creation (wrapped)
  HRESULT STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                                          DWORD BehaviorFlags,
                                          D3DPRESENT_PARAMETERS *pPresentationParameters,
                                          IDirect3DDevice9 **ppReturnedDeviceInterface) override;

private:
  IDirect3D9 *m_pReal;
  int32_t m_RefCount;
};
