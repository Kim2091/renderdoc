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
#include "d3d9_device.h"
#include "d3d9_manager.h"

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DStateBlock9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DStateBlock9 : public IDirect3DStateBlock9
{
public:
  WrappedIDirect3DStateBlock9(IDirect3DStateBlock9 *real, WrappedIDirect3DDevice9 *device,
                              D3DSTATEBLOCKTYPE type = (D3DSTATEBLOCKTYPE)0,
                              ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DStateBlock9();

  IDirect3DStateBlock9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }
  D3DSTATEBLOCKTYPE GetType() { return m_Type; }

  void IntAddRef() { Atomic::Inc32(&m_IntRef); }
  void IntRelease() { Atomic::Dec32(&m_IntRef); }

  //////////////////////////////
  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

  //////////////////////////////
  // IDirect3DStateBlock9
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
  HRESULT STDMETHODCALLTYPE Capture() override;
  HRESULT STDMETHODCALLTYPE Apply() override;

private:
  IDirect3DStateBlock9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3DSTATEBLOCKTYPE m_Type;
};
