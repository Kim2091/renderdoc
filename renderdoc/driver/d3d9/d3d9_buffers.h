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
// WrappedIDirect3DVertexBuffer9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DVertexBuffer9 : public IDirect3DVertexBuffer9
{
public:
  WrappedIDirect3DVertexBuffer9(IDirect3DVertexBuffer9 *real, WrappedIDirect3DDevice9 *device,
                                UINT length, DWORD usage, ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DVertexBuffer9();

  IDirect3DVertexBuffer9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

  byte *GetShadowData() { return m_ShadowData; }
  UINT GetLength() { return m_Length; }

  void IntAddRef() { Atomic::Inc32(&m_IntRef); }
  void IntRelease() { Atomic::Dec32(&m_IntRef); }

  //////////////////////////////
  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

  //////////////////////////////
  // IDirect3DResource9
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData,
                                            DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData,
                                            DWORD *pSizeOfData) override;
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
  DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
  DWORD STDMETHODCALLTYPE GetPriority() override;
  void STDMETHODCALLTYPE PreLoad() override;
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;

  //////////////////////////////
  // IDirect3DVertexBuffer9
  HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData,
                                  DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE Unlock() override;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override;

private:
  IDirect3DVertexBuffer9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9WrappedInfo m_WrappedInfo;
  DWORD m_Usage;
  UINT m_Length;
  byte *m_ShadowData;

  // Lock tracking
  UINT m_LockedOffset;
  UINT m_LockedSize;
  byte *m_LockedData;
};

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DIndexBuffer9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DIndexBuffer9 : public IDirect3DIndexBuffer9
{
public:
  WrappedIDirect3DIndexBuffer9(IDirect3DIndexBuffer9 *real, WrappedIDirect3DDevice9 *device,
                               UINT length, DWORD usage, D3DFORMAT format,
                               ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DIndexBuffer9();

  IDirect3DIndexBuffer9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

  byte *GetShadowData() { return m_ShadowData; }
  UINT GetLength() { return m_Length; }

  void IntAddRef() { Atomic::Inc32(&m_IntRef); }
  void IntRelease() { Atomic::Dec32(&m_IntRef); }

  //////////////////////////////
  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

  //////////////////////////////
  // IDirect3DResource9
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData,
                                            DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData,
                                            DWORD *pSizeOfData) override;
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
  DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) override;
  DWORD STDMETHODCALLTYPE GetPriority() override;
  void STDMETHODCALLTYPE PreLoad() override;
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override;

  //////////////////////////////
  // IDirect3DIndexBuffer9
  HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData,
                                  DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE Unlock() override;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC *pDesc) override;

private:
  IDirect3DIndexBuffer9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9WrappedInfo m_WrappedInfo;
  DWORD m_Usage;
  UINT m_Length;
  D3DFORMAT m_Format;
  byte *m_ShadowData;

  // Lock tracking
  UINT m_LockedOffset;
  UINT m_LockedSize;
  byte *m_LockedData;
};
