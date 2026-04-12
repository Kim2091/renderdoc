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

// Forward declarations
class WrappedIDirect3DSurface9;
class WrappedIDirect3DVolume9;

// Helper to extract a ResourceId from any wrapped D3D9 resource.
// Returns ResourceId() if the pointer is NULL or unrecognised.
ResourceId GetIDForD3D9Resource(IUnknown *resource);

///////////////////////////////////////////////////////////////////////////
// Lock tracking structures
///////////////////////////////////////////////////////////////////////////

struct D3D9LockedRect
{
  UINT level = 0;
  RECT rect = {};
  byte *data = NULL;
  int pitch = 0;
  bool active = false;
};

struct D3D9LockedBox
{
  UINT level = 0;
  D3DBOX box = {};
  byte *data = NULL;
  int rowPitch = 0;
  int slicePitch = 0;
  bool active = false;
};

struct D3D9CubeLockedRect
{
  D3DCUBEMAP_FACES face = D3DCUBEMAP_FACE_POSITIVE_X;
  UINT level = 0;
  RECT rect = {};
  byte *data = NULL;
  int pitch = 0;
  bool active = false;
};

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DSurface9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DSurface9 : public IDirect3DSurface9
{
public:
  WrappedIDirect3DSurface9(IDirect3DSurface9 *real, WrappedIDirect3DDevice9 *device,
                           IUnknown *owner = NULL, ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DSurface9();

  IDirect3DSurface9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

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
  // IDirect3DSurface9
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) override;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) override;
  HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect,
                                      DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect() override;
  HRESULT STDMETHODCALLTYPE GetDC(HDC *phdc) override;
  HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) override;

private:
  IDirect3DSurface9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  IUnknown *m_pOwner;    // parent texture/device that owns this surface (for GetContainer)
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9LockedRect m_Lock;
};

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVolume9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DVolume9 : public IDirect3DVolume9
{
public:
  WrappedIDirect3DVolume9(IDirect3DVolume9 *real, WrappedIDirect3DDevice9 *device,
                          IUnknown *owner = NULL, ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DVolume9();

  IDirect3DVolume9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

  void IntAddRef() { Atomic::Inc32(&m_IntRef); }
  void IntRelease() { Atomic::Dec32(&m_IntRef); }

  //////////////////////////////
  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;

  //////////////////////////////
  // IDirect3DVolume9 (not an IDirect3DResource9)
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **ppDevice) override;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, CONST void *pData, DWORD SizeOfData,
                                            DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void *pData,
                                            DWORD *pSizeOfData) override;
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) override;
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) override;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC *pDesc) override;
  HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX *pLockedVolume, CONST D3DBOX *pBox,
                                     DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockBox() override;

private:
  IDirect3DVolume9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  IUnknown *m_pOwner;    // parent volume texture
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9LockedBox m_Lock;
};

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DTexture9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DTexture9 : public IDirect3DTexture9
{
public:
  WrappedIDirect3DTexture9(IDirect3DTexture9 *real, WrappedIDirect3DDevice9 *device,
                           ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DTexture9();

  IDirect3DTexture9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

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
  // IDirect3DBaseTexture9
  DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override;
  DWORD STDMETHODCALLTYPE GetLOD() override;
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override;
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override;
  void STDMETHODCALLTYPE GenerateMipSubLevels() override;

  //////////////////////////////
  // IDirect3DTexture9
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
  HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level,
                                             IDirect3DSurface9 **ppSurfaceLevel) override;
  HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect,
                                      DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;
  HRESULT STDMETHODCALLTYPE AddDirtyRect(CONST RECT *pDirtyRect) override;

private:
  IDirect3DTexture9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9LockedRect m_Lock;
};

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DCubeTexture9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DCubeTexture9 : public IDirect3DCubeTexture9
{
public:
  WrappedIDirect3DCubeTexture9(IDirect3DCubeTexture9 *real, WrappedIDirect3DDevice9 *device,
                               ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DCubeTexture9();

  IDirect3DCubeTexture9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

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
  // IDirect3DBaseTexture9
  DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override;
  DWORD STDMETHODCALLTYPE GetLOD() override;
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override;
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override;
  void STDMETHODCALLTYPE GenerateMipSubLevels() override;

  //////////////////////////////
  // IDirect3DCubeTexture9
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
  HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level,
                                               IDirect3DSurface9 **ppCubeMapSurface) override;
  HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level,
                                      D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect,
                                      DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) override;
  HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES FaceType,
                                          CONST RECT *pDirtyRect) override;

private:
  IDirect3DCubeTexture9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9CubeLockedRect m_Lock;
};

///////////////////////////////////////////////////////////////////////////
// WrappedIDirect3DVolumeTexture9
///////////////////////////////////////////////////////////////////////////

class WrappedIDirect3DVolumeTexture9 : public IDirect3DVolumeTexture9
{
public:
  WrappedIDirect3DVolumeTexture9(IDirect3DVolumeTexture9 *real, WrappedIDirect3DDevice9 *device,
                                 ResourceId id = ResourceId());
  virtual ~WrappedIDirect3DVolumeTexture9();

  IDirect3DVolumeTexture9 *GetReal() { return m_pReal; }
  ResourceId GetResourceID() { return m_ID; }

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
  // IDirect3DBaseTexture9
  DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) override;
  DWORD STDMETHODCALLTYPE GetLOD() override;
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) override;
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override;
  void STDMETHODCALLTYPE GenerateMipSubLevels() override;

  //////////////////////////////
  // IDirect3DVolumeTexture9
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) override;
  HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT Level,
                                            IDirect3DVolume9 **ppVolumeLevel) override;
  HRESULT STDMETHODCALLTYPE LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, CONST D3DBOX *pBox,
                                     DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockBox(UINT Level) override;
  HRESULT STDMETHODCALLTYPE AddDirtyBox(CONST D3DBOX *pDirtyBox) override;

private:
  IDirect3DVolumeTexture9 *m_pReal;
  ResourceId m_ID;
  WrappedIDirect3DDevice9 *m_pDevice;
  int32_t m_ExtRef;
  int32_t m_IntRef;

  D3D9LockedBox m_Lock;
};
