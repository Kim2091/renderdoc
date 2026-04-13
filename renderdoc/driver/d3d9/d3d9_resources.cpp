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

#include "d3d9_resources.h"
#include "d3d9_buffers.h"
#include "d3d9_query.h"
#include "d3d9_shaders.h"
#include "d3d9_stateblock.h"

///////////////////////////////////////////////////////////////////////////
// GetIDForD3D9Resource
///////////////////////////////////////////////////////////////////////////

ResourceId GetIDForD3D9Resource(IUnknown *resource)
{
  if(resource == NULL)
    return ResourceId();

  // Use our custom QI to identify wrapped resources without RTTI
  D3D9WrappedInfo *info = NULL;
  if(SUCCEEDED(resource->QueryInterface(IID_ID3D9WrappedResource, (void **)&info)))
    return info->id;

  RDCERR("GetIDForD3D9Resource called on unrecognised/unwrapped resource");
  return ResourceId();
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DSurface9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DSurface9::WrappedIDirect3DSurface9(IDirect3DSurface9 *real,
                                                   WrappedIDirect3DDevice9 *device, IUnknown *owner,
                                                   ResourceId id)
    : m_pReal(real), m_pDevice(device), m_pOwner(owner), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_Lock = {};
  m_WrappedInfo = {D3D9WrappedType::Surface, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DSurface9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DSurface9::~WrappedIDirect3DSurface9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DSurface9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DSurface9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::QueryInterface(REFIID riid, void **ppvObj)
{
  if(ppvObj == NULL)
    return E_POINTER;

  if(riid == IID_ID3D9WrappedResource)
  {
    // Return info pointer without AddRef — callers use this for identification only
    *ppvObj = &m_WrappedInfo;
    return S_OK;
  }
  if(riid == __uuidof(IUnknown))
  {
    *ppvObj = (IUnknown *)(IDirect3DSurface9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DSurface9))
  {
    *ppvObj = (IDirect3DSurface9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DResource9))
  {
    *ppvObj = (IDirect3DResource9 *)(IDirect3DSurface9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::SetPrivateData(REFGUID refguid,
                                                                    CONST void *pData,
                                                                    DWORD SizeOfData, DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetPrivateData(REFGUID refguid, void *pData,
                                                                    DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DSurface9::SetPriority(DWORD PriorityNew)
{
  return m_pReal->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetPriority()
{
  return m_pReal->GetPriority();
}

void STDMETHODCALLTYPE WrappedIDirect3DSurface9::PreLoad()
{
  m_pReal->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetType()
{
  return m_pReal->GetType();
}

// IDirect3DSurface9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetContainer(REFIID riid, void **ppContainer)
{
  if(ppContainer == NULL)
    return D3DERR_INVALIDCALL;

  // If we have a wrapped owner (parent texture), return it
  if(m_pOwner)
    return m_pOwner->QueryInterface(riid, ppContainer);

  // Otherwise fall back to the device
  return m_pDevice->QueryInterface(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetDesc(D3DSURFACE_DESC *pDesc)
{
  return m_pReal->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::LockRect(D3DLOCKED_RECT *pLockedRect,
                                                              CONST RECT *pRect, DWORD Flags)
{
  HRESULT ret = m_pReal->LockRect(pLockedRect, pRect, Flags);

  if(SUCCEEDED(ret))
  {
    D3DSURFACE_DESC desc;
    m_pReal->GetDesc(&desc);

    m_Lock.active = true;
    m_Lock.level = 0;
    m_Lock.data = (byte *)pLockedRect->pBits;
    m_Lock.pitch = pLockedRect->Pitch;

    if(pRect)
    {
      m_Lock.rect = *pRect;
    }
    else
    {
      m_Lock.rect.left = 0;
      m_Lock.rect.top = 0;
      m_Lock.rect.right = (LONG)desc.Width;
      m_Lock.rect.bottom = (LONG)desc.Height;
    }
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::UnlockRect()
{
  if(m_Lock.active && IsCaptureMode(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);
  }

  m_Lock.active = false;
  return m_pReal->UnlockRect();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::GetDC(HDC *phdc)
{
  return m_pReal->GetDC(phdc);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DSurface9::ReleaseDC(HDC hdc)
{
  return m_pReal->ReleaseDC(hdc);
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVolume9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DVolume9::WrappedIDirect3DVolume9(IDirect3DVolume9 *real,
                                                 WrappedIDirect3DDevice9 *device, IUnknown *owner,
                                                 ResourceId id)
    : m_pReal(real), m_pDevice(device), m_pOwner(owner), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_Lock = {};
  m_WrappedInfo = {D3D9WrappedType::Volume, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DVolume9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DVolume9::~WrappedIDirect3DVolume9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVolume9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVolume9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::QueryInterface(REFIID riid, void **ppvObj)
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
    *ppvObj = (IUnknown *)(IDirect3DVolume9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DVolume9))
  {
    *ppvObj = (IDirect3DVolume9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DVolume9 methods
HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::SetPrivateData(REFGUID refguid,
                                                                   CONST void *pData,
                                                                   DWORD SizeOfData, DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::GetPrivateData(REFGUID refguid, void *pData,
                                                                   DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::GetContainer(REFIID riid, void **ppContainer)
{
  if(ppContainer == NULL)
    return D3DERR_INVALIDCALL;

  if(m_pOwner)
    return m_pOwner->QueryInterface(riid, ppContainer);

  return m_pDevice->QueryInterface(riid, ppContainer);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::GetDesc(D3DVOLUME_DESC *pDesc)
{
  return m_pReal->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::LockBox(D3DLOCKED_BOX *pLockedVolume,
                                                            CONST D3DBOX *pBox, DWORD Flags)
{
  HRESULT ret = m_pReal->LockBox(pLockedVolume, pBox, Flags);

  if(SUCCEEDED(ret))
  {
    D3DVOLUME_DESC desc;
    m_pReal->GetDesc(&desc);

    m_Lock.active = true;
    m_Lock.level = 0;
    m_Lock.data = (byte *)pLockedVolume->pBits;
    m_Lock.rowPitch = pLockedVolume->RowPitch;
    m_Lock.slicePitch = pLockedVolume->SlicePitch;

    if(pBox)
    {
      m_Lock.box = *pBox;
    }
    else
    {
      m_Lock.box.Left = 0;
      m_Lock.box.Top = 0;
      m_Lock.box.Front = 0;
      m_Lock.box.Right = desc.Width;
      m_Lock.box.Bottom = desc.Height;
      m_Lock.box.Back = desc.Depth;
    }
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolume9::UnlockBox()
{
  if(m_Lock.active && IsCaptureMode(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);

    RDCDEBUG("Volume UnlockBox: marked resource dirty");
  }

  m_Lock.active = false;
  return m_pReal->UnlockBox();
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DTexture9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DTexture9::WrappedIDirect3DTexture9(IDirect3DTexture9 *real,
                                                   WrappedIDirect3DDevice9 *device, ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_Lock = {};
  m_WrappedInfo = {D3D9WrappedType::Texture, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DTexture9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DTexture9::~WrappedIDirect3DTexture9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DTexture9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DTexture9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::QueryInterface(REFIID riid, void **ppvObj)
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
    *ppvObj = (IUnknown *)(IDirect3DTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DTexture9))
  {
    *ppvObj = (IDirect3DTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DBaseTexture9))
  {
    *ppvObj = (IDirect3DBaseTexture9 *)(IDirect3DTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DResource9))
  {
    *ppvObj = (IDirect3DResource9 *)(IDirect3DTexture9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetPrivateData(REFGUID refguid,
                                                                    CONST void *pData,
                                                                    DWORD SizeOfData, DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetPrivateData(REFGUID refguid, void *pData,
                                                                    DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetPriority(DWORD PriorityNew)
{
  return m_pReal->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetPriority()
{
  return m_pReal->GetPriority();
}

void STDMETHODCALLTYPE WrappedIDirect3DTexture9::PreLoad()
{
  m_pReal->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetType()
{
  return m_pReal->GetType();
}

// IDirect3DBaseTexture9
DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetLOD(DWORD LODNew)
{
  return m_pReal->SetLOD(LODNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetLOD()
{
  return m_pReal->GetLOD();
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetLevelCount()
{
  return m_pReal->GetLevelCount();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::SetAutoGenFilterType(
    D3DTEXTUREFILTERTYPE FilterType)
{
  return m_pReal->SetAutoGenFilterType(FilterType);
}

D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetAutoGenFilterType()
{
  return m_pReal->GetAutoGenFilterType();
}

void STDMETHODCALLTYPE WrappedIDirect3DTexture9::GenerateMipSubLevels()
{
  m_pReal->GenerateMipSubLevels();
}

// IDirect3DTexture9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetLevelDesc(UINT Level,
                                                                  D3DSURFACE_DESC *pDesc)
{
  return m_pReal->GetLevelDesc(Level, pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::GetSurfaceLevel(
    UINT Level, IDirect3DSurface9 **ppSurfaceLevel)
{
  if(ppSurfaceLevel == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DSurface9 *realSurf = NULL;
  HRESULT ret = m_pReal->GetSurfaceLevel(Level, &realSurf);

  if(SUCCEEDED(ret) && realSurf)
  {
    // Check if we already have a wrapper for this surface
    if(m_pDevice->GetResourceManager()->HasWrapper(realSurf))
    {
      IUnknown *existing = m_pDevice->GetResourceManager()->GetWrapper(realSurf);
      *ppSurfaceLevel = (IDirect3DSurface9 *)existing;
      (*ppSurfaceLevel)->AddRef();
      realSurf->Release();    // we don't need the extra real ref
    }
    else
    {
      WrappedIDirect3DSurface9 *wrapped =
          new WrappedIDirect3DSurface9(realSurf, m_pDevice, (IUnknown *)(IDirect3DTexture9 *)this);
      *ppSurfaceLevel = wrapped;
    }
  }
  else
  {
    if(ppSurfaceLevel)
      *ppSurfaceLevel = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::LockRect(UINT Level,
                                                              D3DLOCKED_RECT *pLockedRect,
                                                              CONST RECT *pRect, DWORD Flags)
{
  HRESULT ret = m_pReal->LockRect(Level, pLockedRect, pRect, Flags);

  if(SUCCEEDED(ret))
  {
    D3DSURFACE_DESC desc;
    m_pReal->GetLevelDesc(Level, &desc);

    m_Lock.active = true;
    m_Lock.level = Level;
    m_Lock.data = (byte *)pLockedRect->pBits;
    m_Lock.pitch = pLockedRect->Pitch;

    if(pRect)
    {
      m_Lock.rect = *pRect;
    }
    else
    {
      m_Lock.rect.left = 0;
      m_Lock.rect.top = 0;
      m_Lock.rect.right = (LONG)desc.Width;
      m_Lock.rect.bottom = (LONG)desc.Height;
    }
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::UnlockRect(UINT Level)
{
  if(m_Lock.active && m_Lock.level == Level && IsCaptureMode(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);
  }

  if(m_Lock.active && m_Lock.level == Level)
    m_Lock.active = false;

  return m_pReal->UnlockRect(Level);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DTexture9::AddDirtyRect(CONST RECT *pDirtyRect)
{
  return m_pReal->AddDirtyRect(pDirtyRect);
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DCubeTexture9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DCubeTexture9::WrappedIDirect3DCubeTexture9(IDirect3DCubeTexture9 *real,
                                                           WrappedIDirect3DDevice9 *device,
                                                           ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_Lock = {};
  m_WrappedInfo = {D3D9WrappedType::CubeTexture, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DCubeTexture9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DCubeTexture9::~WrappedIDirect3DCubeTexture9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::QueryInterface(REFIID riid, void **ppvObj)
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
    *ppvObj = (IUnknown *)(IDirect3DCubeTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DCubeTexture9))
  {
    *ppvObj = (IDirect3DCubeTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DBaseTexture9))
  {
    *ppvObj = (IDirect3DBaseTexture9 *)(IDirect3DCubeTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DResource9))
  {
    *ppvObj = (IDirect3DResource9 *)(IDirect3DCubeTexture9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::SetPrivateData(REFGUID refguid,
                                                                        CONST void *pData,
                                                                        DWORD SizeOfData,
                                                                        DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetPrivateData(REFGUID refguid,
                                                                        void *pData,
                                                                        DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::SetPriority(DWORD PriorityNew)
{
  return m_pReal->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetPriority()
{
  return m_pReal->GetPriority();
}

void STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::PreLoad()
{
  m_pReal->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetType()
{
  return m_pReal->GetType();
}

// IDirect3DBaseTexture9
DWORD STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::SetLOD(DWORD LODNew)
{
  return m_pReal->SetLOD(LODNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetLOD()
{
  return m_pReal->GetLOD();
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetLevelCount()
{
  return m_pReal->GetLevelCount();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::SetAutoGenFilterType(
    D3DTEXTUREFILTERTYPE FilterType)
{
  return m_pReal->SetAutoGenFilterType(FilterType);
}

D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetAutoGenFilterType()
{
  return m_pReal->GetAutoGenFilterType();
}

void STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GenerateMipSubLevels()
{
  m_pReal->GenerateMipSubLevels();
}

// IDirect3DCubeTexture9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetLevelDesc(UINT Level,
                                                                      D3DSURFACE_DESC *pDesc)
{
  return m_pReal->GetLevelDesc(Level, pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::GetCubeMapSurface(
    D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9 **ppCubeMapSurface)
{
  if(ppCubeMapSurface == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DSurface9 *realSurf = NULL;
  HRESULT ret = m_pReal->GetCubeMapSurface(FaceType, Level, &realSurf);

  if(SUCCEEDED(ret) && realSurf)
  {
    if(m_pDevice->GetResourceManager()->HasWrapper(realSurf))
    {
      IUnknown *existing = m_pDevice->GetResourceManager()->GetWrapper(realSurf);
      *ppCubeMapSurface = (IDirect3DSurface9 *)existing;
      (*ppCubeMapSurface)->AddRef();
      realSurf->Release();
    }
    else
    {
      WrappedIDirect3DSurface9 *wrapped = new WrappedIDirect3DSurface9(
          realSurf, m_pDevice, (IUnknown *)(IDirect3DCubeTexture9 *)this);
      *ppCubeMapSurface = wrapped;
    }
  }
  else
  {
    if(ppCubeMapSurface)
      *ppCubeMapSurface = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::LockRect(D3DCUBEMAP_FACES FaceType,
                                                                  UINT Level,
                                                                  D3DLOCKED_RECT *pLockedRect,
                                                                  CONST RECT *pRect, DWORD Flags)
{
  HRESULT ret = m_pReal->LockRect(FaceType, Level, pLockedRect, pRect, Flags);

  if(SUCCEEDED(ret))
  {
    D3DSURFACE_DESC desc;
    m_pReal->GetLevelDesc(Level, &desc);

    m_Lock.active = true;
    m_Lock.face = FaceType;
    m_Lock.level = Level;
    m_Lock.data = (byte *)pLockedRect->pBits;
    m_Lock.pitch = pLockedRect->Pitch;

    if(pRect)
    {
      m_Lock.rect = *pRect;
    }
    else
    {
      m_Lock.rect.left = 0;
      m_Lock.rect.top = 0;
      m_Lock.rect.right = (LONG)desc.Width;
      m_Lock.rect.bottom = (LONG)desc.Height;
    }
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::UnlockRect(D3DCUBEMAP_FACES FaceType,
                                                                    UINT Level)
{
  if(m_Lock.active && m_Lock.face == FaceType && m_Lock.level == Level &&
     IsCaptureMode(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);
  }

  if(m_Lock.active && m_Lock.face == FaceType && m_Lock.level == Level)
    m_Lock.active = false;

  return m_pReal->UnlockRect(FaceType, Level);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DCubeTexture9::AddDirtyRect(D3DCUBEMAP_FACES FaceType,
                                                                      CONST RECT *pDirtyRect)
{
  return m_pReal->AddDirtyRect(FaceType, pDirtyRect);
}

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVolumeTexture9
///////////////////////////////////////////////////////////////////////////

WrappedIDirect3DVolumeTexture9::WrappedIDirect3DVolumeTexture9(IDirect3DVolumeTexture9 *real,
                                                               WrappedIDirect3DDevice9 *device,
                                                               ResourceId id)
    : m_pReal(real), m_pDevice(device), m_ExtRef(1), m_IntRef(0)
{
  if(id == ResourceId())
    id = ResourceIDGen::GetNewUniqueID();
  m_ID = id;
  m_Lock = {};
  m_WrappedInfo = {D3D9WrappedType::VolumeTexture, m_ID, m_pReal};

  m_pDevice->AddRef();

  bool ret = m_pDevice->GetResourceManager()->AddWrapper(this, real);
  if(!ret)
    RDCERR("Error adding wrapper for IDirect3DVolumeTexture9");

  m_pDevice->GetResourceManager()->AddResource(m_ID, this);
}

WrappedIDirect3DVolumeTexture9::~WrappedIDirect3DVolumeTexture9()
{
  m_pDevice->GetResourceManager()->RemoveWrapper(m_pReal);
  m_pDevice->GetResourceManager()->ReleaseResource(m_ID);
  SAFE_RELEASE(m_pReal);
  m_pDevice = NULL;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::AddRef()
{
  Atomic::Inc32(&m_ExtRef);
  return (ULONG)m_ExtRef;
}

ULONG STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::Release()
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

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::QueryInterface(REFIID riid,
                                                                          void **ppvObj)
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
    *ppvObj = (IUnknown *)(IDirect3DVolumeTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DVolumeTexture9))
  {
    *ppvObj = (IDirect3DVolumeTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DBaseTexture9))
  {
    *ppvObj = (IDirect3DBaseTexture9 *)(IDirect3DVolumeTexture9 *)this;
    AddRef();
    return S_OK;
  }
  if(riid == __uuidof(IDirect3DResource9))
  {
    *ppvObj = (IDirect3DResource9 *)(IDirect3DVolumeTexture9 *)this;
    AddRef();
    return S_OK;
  }

  *ppvObj = NULL;
  return E_NOINTERFACE;
}

// IDirect3DResource9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetDevice(IDirect3DDevice9 **ppDevice)
{
  if(ppDevice == NULL)
    return D3DERR_INVALIDCALL;
  *ppDevice = m_pDevice;
  m_pDevice->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::SetPrivateData(REFGUID refguid,
                                                                          CONST void *pData,
                                                                          DWORD SizeOfData,
                                                                          DWORD Flags)
{
  return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetPrivateData(REFGUID refguid,
                                                                          void *pData,
                                                                          DWORD *pSizeOfData)
{
  return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::FreePrivateData(REFGUID refguid)
{
  return m_pReal->FreePrivateData(refguid);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::SetPriority(DWORD PriorityNew)
{
  return m_pReal->SetPriority(PriorityNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetPriority()
{
  return m_pReal->GetPriority();
}

void STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::PreLoad()
{
  m_pReal->PreLoad();
}

D3DRESOURCETYPE STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetType()
{
  return m_pReal->GetType();
}

// IDirect3DBaseTexture9
DWORD STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::SetLOD(DWORD LODNew)
{
  return m_pReal->SetLOD(LODNew);
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetLOD()
{
  return m_pReal->GetLOD();
}

DWORD STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetLevelCount()
{
  return m_pReal->GetLevelCount();
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::SetAutoGenFilterType(
    D3DTEXTUREFILTERTYPE FilterType)
{
  return m_pReal->SetAutoGenFilterType(FilterType);
}

D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetAutoGenFilterType()
{
  return m_pReal->GetAutoGenFilterType();
}

void STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GenerateMipSubLevels()
{
  m_pReal->GenerateMipSubLevels();
}

// IDirect3DVolumeTexture9
HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetLevelDesc(UINT Level,
                                                                        D3DVOLUME_DESC *pDesc)
{
  return m_pReal->GetLevelDesc(Level, pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::GetVolumeLevel(
    UINT Level, IDirect3DVolume9 **ppVolumeLevel)
{
  if(ppVolumeLevel == NULL)
    return D3DERR_INVALIDCALL;

  IDirect3DVolume9 *realVol = NULL;
  HRESULT ret = m_pReal->GetVolumeLevel(Level, &realVol);

  if(SUCCEEDED(ret) && realVol)
  {
    if(m_pDevice->GetResourceManager()->HasWrapper(realVol))
    {
      IUnknown *existing = m_pDevice->GetResourceManager()->GetWrapper(realVol);
      *ppVolumeLevel = (IDirect3DVolume9 *)existing;
      (*ppVolumeLevel)->AddRef();
      realVol->Release();
    }
    else
    {
      WrappedIDirect3DVolume9 *wrapped = new WrappedIDirect3DVolume9(
          realVol, m_pDevice, (IUnknown *)(IDirect3DVolumeTexture9 *)this);
      *ppVolumeLevel = wrapped;
    }
  }
  else
  {
    if(ppVolumeLevel)
      *ppVolumeLevel = NULL;
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::LockBox(UINT Level,
                                                                    D3DLOCKED_BOX *pLockedVolume,
                                                                    CONST D3DBOX *pBox, DWORD Flags)
{
  HRESULT ret = m_pReal->LockBox(Level, pLockedVolume, pBox, Flags);

  if(SUCCEEDED(ret))
  {
    D3DVOLUME_DESC desc;
    m_pReal->GetLevelDesc(Level, &desc);

    m_Lock.active = true;
    m_Lock.level = Level;
    m_Lock.data = (byte *)pLockedVolume->pBits;
    m_Lock.rowPitch = pLockedVolume->RowPitch;
    m_Lock.slicePitch = pLockedVolume->SlicePitch;

    if(pBox)
    {
      m_Lock.box = *pBox;
    }
    else
    {
      m_Lock.box.Left = 0;
      m_Lock.box.Top = 0;
      m_Lock.box.Front = 0;
      m_Lock.box.Right = desc.Width;
      m_Lock.box.Bottom = desc.Height;
      m_Lock.box.Back = desc.Depth;
    }
  }

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::UnlockBox(UINT Level)
{
  if(m_Lock.active && m_Lock.level == Level && IsCaptureMode(m_pDevice->GetState()))
  {
    m_pDevice->GetResourceManager()->MarkDirtyResource(m_ID);

    RDCDEBUG("VolumeTexture UnlockBox level %u: marked dirty", Level);
  }

  if(m_Lock.active && m_Lock.level == Level)
    m_Lock.active = false;

  return m_pReal->UnlockBox(Level);
}

HRESULT STDMETHODCALLTYPE WrappedIDirect3DVolumeTexture9::AddDirtyBox(CONST D3DBOX *pDirtyBox)
{
  return m_pReal->AddDirtyBox(pDirtyBox);
}

///////////////////////////////////////////////////////////////////////////
// Device-side creation serialization
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateTexture(SerialiserType &ser, UINT Width, UINT Height,
                                                       UINT Levels, DWORD Usage, D3DFORMAT Format,
                                                       D3DPOOL Pool,
                                                       IDirect3DTexture9 **ppTexture,
                                                       HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Width).Important();
  SERIALISE_ELEMENT(Height).Important();
  SERIALISE_ELEMENT(Levels);
  SERIALISE_ELEMENT(Usage);
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(Pool);

  ResourceId Texture;
  if(ser.IsWriting())
  {
    WrappedIDirect3DTexture9 *wrapped = (WrappedIDirect3DTexture9 *)*ppTexture;
    Texture = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(Texture).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DTexture9 *real = NULL;
    D3DPOOL replayPool = Pool;
    DWORD replayUsage = Usage;

    // During replay, override D3DPOOL_DEFAULT to D3DPOOL_MANAGED for non-RT/DS textures
    // so they can be locked for CPU readback. D3DPOOL_MANAGED maintains a system memory
    // copy that D3D9 automatically syncs, making textures lockable.
    if(Pool == D3DPOOL_DEFAULT &&
       !(Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
    {
      replayPool = D3DPOOL_MANAGED;
      // D3DPOOL_MANAGED doesn't support D3DUSAGE_DYNAMIC
      replayUsage &= ~D3DUSAGE_DYNAMIC;
    }

    HRESULT hr = m_pDevice->CreateTexture(Width, Height, Levels, replayUsage, Format, replayPool,
                                          &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create texture on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      // Constructor registers with ResourceManager via AddResource
      new WrappedIDirect3DTexture9(real, this, Texture);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateCubeTexture(SerialiserType &ser, UINT EdgeLength,
                                                           UINT Levels, DWORD Usage,
                                                           D3DFORMAT Format, D3DPOOL Pool,
                                                           IDirect3DCubeTexture9 **ppCubeTexture,
                                                           HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(EdgeLength).Important();
  SERIALISE_ELEMENT(Levels);
  SERIALISE_ELEMENT(Usage);
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(Pool);

  ResourceId CubeTexture;
  if(ser.IsWriting())
  {
    WrappedIDirect3DCubeTexture9 *wrapped = (WrappedIDirect3DCubeTexture9 *)*ppCubeTexture;
    CubeTexture = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(CubeTexture).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DCubeTexture9 *real = NULL;
    D3DPOOL replayPool = Pool;
    DWORD replayUsage = Usage;

    // During replay, override D3DPOOL_DEFAULT to D3DPOOL_MANAGED for non-RT/DS textures
    if(Pool == D3DPOOL_DEFAULT &&
       !(Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
    {
      replayPool = D3DPOOL_MANAGED;
      replayUsage &= ~D3DUSAGE_DYNAMIC;
    }

    HRESULT hr =
        m_pDevice->CreateCubeTexture(EdgeLength, Levels, replayUsage, Format, replayPool, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create cube texture on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DCubeTexture9(real, this, CubeTexture);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateVolumeTexture(
    SerialiserType &ser, UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage,
    D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9 **ppVolumeTexture,
    HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Width).Important();
  SERIALISE_ELEMENT(Height).Important();
  SERIALISE_ELEMENT(Depth).Important();
  SERIALISE_ELEMENT(Levels);
  SERIALISE_ELEMENT(Usage);
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(Pool);

  ResourceId VolumeTexture;
  if(ser.IsWriting())
  {
    WrappedIDirect3DVolumeTexture9 *wrapped =
        (WrappedIDirect3DVolumeTexture9 *)*ppVolumeTexture;
    VolumeTexture = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(VolumeTexture).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DVolumeTexture9 *real = NULL;
    D3DPOOL replayPool = Pool;
    DWORD replayUsage = Usage;

    // During replay, override D3DPOOL_DEFAULT to D3DPOOL_MANAGED for non-RT/DS textures
    if(Pool == D3DPOOL_DEFAULT &&
       !(Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)))
    {
      replayPool = D3DPOOL_MANAGED;
      replayUsage &= ~D3DUSAGE_DYNAMIC;
    }

    HRESULT hr = m_pDevice->CreateVolumeTexture(Width, Height, Depth, Levels, replayUsage, Format,
                                                replayPool, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create volume texture on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DVolumeTexture9(real, this, VolumeTexture);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateRenderTarget(
    SerialiserType &ser, UINT Width, UINT Height, D3DFORMAT Format,
    D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable,
    IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Width).Important();
  SERIALISE_ELEMENT(Height).Important();
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(MultiSample);
  SERIALISE_ELEMENT(MultisampleQuality);
  SERIALISE_ELEMENT(Lockable);

  ResourceId Surface;
  if(ser.IsWriting())
  {
    WrappedIDirect3DSurface9 *wrapped = (WrappedIDirect3DSurface9 *)*ppSurface;
    Surface = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(Surface).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DSurface9 *real = NULL;
    HRESULT hr = m_pDevice->CreateRenderTarget(Width, Height, Format, MultiSample,
                                               MultisampleQuality, Lockable, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create render target on replay, HRESULT: %s", ToStr(hr).c_str());
    }
    else
    {
      // Constructor registers with ResourceManager via AddResource
      new WrappedIDirect3DSurface9(real, this, NULL, Surface);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateDepthStencilSurface(
    SerialiserType &ser, UINT Width, UINT Height, D3DFORMAT Format,
    D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard,
    IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Width).Important();
  SERIALISE_ELEMENT(Height).Important();
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(MultiSample);
  SERIALISE_ELEMENT(MultisampleQuality);
  SERIALISE_ELEMENT(Discard);

  ResourceId Surface;
  if(ser.IsWriting())
  {
    WrappedIDirect3DSurface9 *wrapped = (WrappedIDirect3DSurface9 *)*ppSurface;
    Surface = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(Surface).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DSurface9 *real = NULL;
    HRESULT hr = m_pDevice->CreateDepthStencilSurface(Width, Height, Format, MultiSample,
                                                      MultisampleQuality, Discard, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create depth stencil surface on replay, HRESULT: %s",
             ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DSurface9(real, this, NULL, Surface);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_CreateOffscreenPlainSurface(
    SerialiserType &ser, UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle)
{
  SERIALISE_ELEMENT(Width).Important();
  SERIALISE_ELEMENT(Height).Important();
  SERIALISE_ELEMENT(Format).Important();
  SERIALISE_ELEMENT(Pool);

  ResourceId Surface;
  if(ser.IsWriting())
  {
    WrappedIDirect3DSurface9 *wrapped = (WrappedIDirect3DSurface9 *)*ppSurface;
    Surface = wrapped->GetResourceID();
  }
  SERIALISE_ELEMENT(Surface).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    IDirect3DSurface9 *real = NULL;
    HRESULT hr =
        m_pDevice->CreateOffscreenPlainSurface(Width, Height, Format, Pool, &real, NULL);

    if(FAILED(hr))
    {
      RDCERR("Failed to create offscreen plain surface on replay, HRESULT: %s",
             ToStr(hr).c_str());
    }
    else
    {
      new WrappedIDirect3DSurface9(real, this, NULL, Surface);
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Template instantiations for serialization
///////////////////////////////////////////////////////////////////////////

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateTexture, UINT Width,
                                UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
                                D3DPOOL Pool, IDirect3DTexture9 **ppTexture,
                                HANDLE *pSharedHandle);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateCubeTexture,
                                UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format,
                                D3DPOOL Pool, IDirect3DCubeTexture9 **ppCubeTexture,
                                HANDLE *pSharedHandle);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateVolumeTexture, UINT Width,
                                UINT Height, UINT Depth, UINT Levels, DWORD Usage,
                                D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateRenderTarget, UINT Width,
                                UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                DWORD MultisampleQuality, BOOL Lockable,
                                IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateDepthStencilSurface,
                                UINT Width, UINT Height, D3DFORMAT Format,
                                D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                BOOL Discard, IDirect3DSurface9 **ppSurface,
                                HANDLE *pSharedHandle);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, CreateOffscreenPlainSurface,
                                UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle);
