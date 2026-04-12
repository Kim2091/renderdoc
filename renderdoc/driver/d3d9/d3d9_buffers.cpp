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

#include "d3d9_buffers.h"

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVertexBuffer9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DVertexBuffer9::WrappedIDirect3DVertexBuffer9(IDirect3DVertexBuffer9 *real,
                                                             WrappedIDirect3DDevice9 *device,
                                                             UINT length, DWORD usage,
                                                             ResourceId id)
    : m_pReal(real),
      m_pDevice(device),
      m_ExtRef(1),
      m_IntRef(0),
      m_Usage(usage),
      m_Length(length),
      m_ShadowData(NULL),
      m_LockedOffset(0),
      m_LockedSize(0),
      m_LockedData(NULL)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_WrappedInfo = {D3D9WrappedType::VertexBuffer, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DVertexBuffer9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);

  // Allocate shadow data for WRITEONLY buffers since they cannot be read back
  if(m_Usage & D3DUSAGE_WRITEONLY)
  {
    m_ShadowData = new byte[m_Length];
    memset(m_ShadowData, 0, m_Length);
  }
}

WrappedIDirect3DVertexBuffer9::~WrappedIDirect3DVertexBuffer9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  SAFE_DELETE_ARRAY(m_ShadowData);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::Release()
{
  Atomic::Dec32(&m_ExtRef);
  RDCASSERT(m_ExtRef >= 0);

  int32_t extRef = m_ExtRef;
  int32_t intRef = m_IntRef;

  WrappedIDirect3DDevice9 *dev = m_pDevice;

  if(extRef + intRef == 0)
  {
    delete this;
    dev->Release();
    return 0;
  }

  if(extRef == 0)
    dev->Release();

  return (ULONG)extRef;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == IID_ID3D9WrappedResource)
  {
    *ppvObj = &m_WrappedInfo;
    return S_OK;
  }
  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DVertexBuffer9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DVertexBuffer9))
  {
    *ppvObj = (IDirect3DVertexBuffer9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DResource9))
  {
    *ppvObj = (IDirect3DResource9 *)(IDirect3DVertexBuffer9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::SetPrivateData(REFGUID refguid,
                                                                         CONST void *pData,
                                                                         DWORD SizeOfData,
                                                                         DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::GetPrivateData(REFGUID refguid,
                                                                         void *pData,
                                                                         DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::SetPriority(DWORD PriorityNew)
{
  return m_pReal->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::GetPriority()
{
  return m_pReal->GetPriority();
}

void STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::PreLoad()
{
  m_pReal->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::GetType()
{
  return m_pReal->GetType();
}

// IDirect3DVertexBuffer9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::Lock(UINT OffsetToLock, UINT SizeToLock,
                                                               void **ppbData, DWORD Flags)
{
  HRESULT ret = m_pReal->Lock(OffsetToLock, SizeToLock, ppbData, Flags);

  if(SUCCEEDED(ret))
  {
    m_LockedOffset = OffsetToLock;
    m_LockedSize = SizeToLock == 0 ? m_Length : SizeToLock;
    m_LockedData = (byte *)*ppbData;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::Unlock()
{
  // Shadow copy for WRITEONLY buffers (maintained even when not actively capturing)
  if(m_ShadowData && m_LockedData)
    memcpy(m_ShadowData + m_LockedOffset, m_LockedData, m_LockedSize);

  // Serialize if capturing
  if(IsActiveCapturing(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);

    RDCDEBUG("VertexBuffer Unlock: marked resource dirty (offset=%u, size=%u)", m_LockedOffset,
             m_LockedSize);
  }

  m_LockedData = NULL;
  return m_pReal->Unlock();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVertexBuffer9::GetDesc(D3DVERTEXBUFFER_DESC *pDesc)
{
  return m_pReal->GetDesc(pDesc);
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DIndexBuffer9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DIndexBuffer9::WrappedIDirect3DIndexBuffer9(IDirect3DIndexBuffer9 *real,
                                                           WrappedIDirect3DDevice9 *device,
                                                           UINT length, DWORD usage,
                                                           D3DFORMAT format, ResourceId id)
    : m_pReal(real),
      m_pDevice(device),
      m_ExtRef(1),
      m_IntRef(0),
      m_Usage(usage),
      m_Length(length),
      m_Format(format),
      m_ShadowData(NULL),
      m_LockedOffset(0),
      m_LockedSize(0),
      m_LockedData(NULL)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_WrappedInfo = {D3D9WrappedType::IndexBuffer, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DIndexBuffer9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);

  // Allocate shadow data for WRITEONLY buffers since they cannot be read back
  if(m_Usage & D3DUSAGE_WRITEONLY)
  {
    m_ShadowData = new byte[m_Length];
    memset(m_ShadowData, 0, m_Length);
  }
}

WrappedIDirect3DIndexBuffer9::~WrappedIDirect3DIndexBuffer9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  SAFE_DELETE_ARRAY(m_ShadowData);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::Release()
{
  Atomic::Dec32(&m_ExtRef);
  RDCASSERT(m_ExtRef >= 0);

  int32_t extRef = m_ExtRef;
  int32_t intRef = m_IntRef;

  WrappedIDirect3DDevice9 *dev = m_pDevice;

  if(extRef + intRef == 0)
  {
    delete this;
    dev->Release();
    return 0;
  }

  if(extRef == 0)
    dev->Release();

  return (ULONG)extRef;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == IID_ID3D9WrappedResource)
  {
    *ppvObj = &m_WrappedInfo;
    return S_OK;
  }
  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DIndexBuffer9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DIndexBuffer9))
  {
    *ppvObj = (IDirect3DIndexBuffer9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DResource9))
  {
    *ppvObj = (IDirect3DResource9 *)(IDirect3DIndexBuffer9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::SetPrivateData(REFGUID refguid,
                                                                        CONST void *pData,
                                                                        DWORD SizeOfData,
                                                                        DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::GetPrivateData(REFGUID refguid, void *pData,
                                                                        DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::SetPriority(DWORD PriorityNew)
{
  return m_pReal->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::GetPriority()
{
  return m_pReal->GetPriority();
}

void STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::PreLoad()
{
  m_pReal->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::GetType()
{
  return m_pReal->GetType();
}

// IDirect3DIndexBuffer9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::Lock(UINT OffsetToLock, UINT SizeToLock,
                                                              void **ppbData, DWORD Flags)
{
  HRESULT ret = m_pReal->Lock(OffsetToLock, SizeToLock, ppbData, Flags);

  if(SUCCEEDED(ret))
  {
    m_LockedOffset = OffsetToLock;
    m_LockedSize = SizeToLock == 0 ? m_Length : SizeToLock;
    m_LockedData = (byte *)*ppbData;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::Unlock()
{
  // Shadow copy for WRITEONLY buffers (maintained even when not actively capturing)
  if(m_ShadowData && m_LockedData)
    memcpy(m_ShadowData + m_LockedOffset, m_LockedData, m_LockedSize);

  // Serialize if capturing
  if(IsActiveCapturing(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);

    RDCDEBUG("IndexBuffer Unlock: marked resource dirty (offset=%u, size=%u)", m_LockedOffset,
             m_LockedSize);
  }

  m_LockedData = NULL;
  return m_pReal->Unlock();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DIndexBuffer9::GetDesc(D3DINDEXBUFFER_DESC *pDesc)
{
  return m_pReal->GetDesc(pDesc);
}

///////////////////////////////////////////////////////////////////////////
// Device-side creation serialization
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateVertexBuffer(SerialiserType &ser, UINT Length,
                                                            DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                                            IDirect3DVertexBuffer9 **ppVertexBuffer,
                                                            HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Length).Important();
  SERIALISE_ELEMENT(Usage);
  SERIALISE_ELEMENT(FVF);
  SERIALISE_ELEMENT(Pool);

  ResourceId VertexBuffer;
  if(ser.IsWriting())
  {
    WrappedIDirect3DVertexBuffer9 *wrapped = (WrappedIDirect3DVertexBuffer9 *)*ppVertexBuffer;
    VertexBuffer = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(VertexBuffer).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DVertexBuffer9 *real = NULL;
    D3DPOOL replayPool = Pool;
    if(Pool == D3DPOOL_MANAGED)
      replayPool = D3DPOOL_MANAGED;

    HRESULT hr = m_pDevice->CreateVertexBuffer(Length, Usage, FVF, replayPool, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create vertex buffer on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DVertexBuffer9(real, this, Length, Usage, VertexBuffer);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateIndexBuffer(SerialiserType &ser, UINT Length,
                                                           DWORD Usage, D3DFORMAT Format,
                                                           D3DPOOL Pool,
                                                           IDirect3DIndexBuffer9 **ppIndexBuffer,
                                                           HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Length).Important();
  SERIALISE_ELEMENT(Usage);
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(Pool);

  ResourceId IndexBuffer;
  if(ser.IsWriting())
  {
    WrappedIDirect3DIndexBuffer9 *wrapped = (WrappedIDirect3DIndexBuffer9 *)*ppIndexBuffer;
    IndexBuffer = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(IndexBuffer).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DIndexBuffer9 *real = NULL;
    D3DPOOL replayPool = Pool;
    if(Pool == D3DPOOL_MANAGED)
      replayPool = D3DPOOL_MANAGED;

    HRESULT hr = m_pDevice->CreateIndexBuffer(Length, Usage, Format, replayPool, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create index buffer on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DIndexBuffer9(real, this, Length, Usage, Format, IndexBuffer);
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Template instantiations for serialization
///////////////////////////////////////////////////////////////////////////

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateVertexBuffer, UINT Length,
                                DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateIndexBuffer, UINT Length,
                                DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle);
