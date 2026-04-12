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
#include "d3d9_buffers.h"
#include "d3d9_resources.h"

///////////////////////////////////////////////////////////////////////////
// Helper: determine D3D9ResourceType from a wrapped IUnknown
///////////////////////////////////////////////////////////////////////////

static D3D9ResourceType IdentifyResourceType(IUnknown *res)
{
  if(dynamic_cast<WrappedIDirect3DVertexBuffer9 *>(res))
    return D3D9ResourceType::VertexBuffer;
  if(dynamic_cast<WrappedIDirect3DIndexBuffer9 *>(res))
    return D3D9ResourceType::IndexBuffer;
  if(dynamic_cast<WrappedIDirect3DTexture9 *>(res))
    return D3D9ResourceType::Texture;
  if(dynamic_cast<WrappedIDirect3DCubeTexture9 *>(res))
    return D3D9ResourceType::CubeTexture;
  if(dynamic_cast<WrappedIDirect3DVolumeTexture9 *>(res))
    return D3D9ResourceType::VolumeTexture;
  if(dynamic_cast<WrappedIDirect3DSurface9 *>(res))
    return D3D9ResourceType::Surface;

  return D3D9ResourceType::Unknown;
}

///////////////////////////////////////////////////////////////////////////
// Helper: compute row pitch for a surface level
///////////////////////////////////////////////////////////////////////////

static uint32_t ComputeRowPitch(D3DFORMAT fmt, UINT width)
{
  if(IsD3D9FormatCompressed(fmt))
  {
    UINT blockW = (width + 3) / 4;
    return blockW * GetD3D9FormatByteSize(fmt);
  }
  return width * GetD3D9FormatByteSize(fmt);
}

///////////////////////////////////////////////////////////////////////////
// Prepare_InitialState
///////////////////////////////////////////////////////////////////////////

bool WrappedIDirect3DDevice9::Prepare_InitialState(IUnknown *res)
{
  D3D9ResourceType type = IdentifyResourceType(res);
  ResourceId id = GetIDForD3D9Resource(res);

  if(id == ResourceId())
  {
    RDCERR("Prepare_InitialState called on unrecognised resource");
    return false;
  }

  RDCDEBUG("Prepare_InitialState for %s", ToStr(id).c_str());

  ///////////////////////////////////////////////////////////////////////////
  // Vertex Buffers
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::VertexBuffer)
  {
    WrappedIDirect3DVertexBuffer9 *wrapper =
        dynamic_cast<WrappedIDirect3DVertexBuffer9 *>(res);
    if(!wrapper)
      return false;

    UINT length = wrapper->GetLength();
    if(length == 0)
      return true;

    D3D9InitialContents initial(D3D9ResourceType::VertexBuffer, NULL);
    initial.shadowData = new byte[length];
    initial.shadowDataLen = length;

    // If the wrapper has a shadow copy (write-only buffer), use it directly
    if(wrapper->GetShadowData())
    {
      memcpy(initial.shadowData, wrapper->GetShadowData(), length);
    }
    else
    {
      // Lock the real buffer and read back
      void *data = NULL;
      HRESULT hr = wrapper->GetReal()->Lock(0, 0, &data, D3DLOCK_READONLY);
      if(SUCCEEDED(hr) && data)
      {
        memcpy(initial.shadowData, data, length);
        wrapper->GetReal()->Unlock();
      }
      else
      {
        RDCERR("Failed to lock vertex buffer for initial state capture: %08x", hr);
        memset(initial.shadowData, 0, length);
        if(SUCCEEDED(hr))
          wrapper->GetReal()->Unlock();
      }
    }

    GetResourceManager()->SetInitialContents(id, std::move(initial));
    return true;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Index Buffers
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::IndexBuffer)
  {
    WrappedIDirect3DIndexBuffer9 *wrapper =
        dynamic_cast<WrappedIDirect3DIndexBuffer9 *>(res);
    if(!wrapper)
      return false;

    UINT length = wrapper->GetLength();
    if(length == 0)
      return true;

    D3D9InitialContents initial(D3D9ResourceType::IndexBuffer, NULL);
    initial.shadowData = new byte[length];
    initial.shadowDataLen = length;

    if(wrapper->GetShadowData())
    {
      memcpy(initial.shadowData, wrapper->GetShadowData(), length);
    }
    else
    {
      void *data = NULL;
      HRESULT hr = wrapper->GetReal()->Lock(0, 0, &data, D3DLOCK_READONLY);
      if(SUCCEEDED(hr) && data)
      {
        memcpy(initial.shadowData, data, length);
        wrapper->GetReal()->Unlock();
      }
      else
      {
        RDCERR("Failed to lock index buffer for initial state capture: %08x", hr);
        memset(initial.shadowData, 0, length);
        if(SUCCEEDED(hr))
          wrapper->GetReal()->Unlock();
      }
    }

    GetResourceManager()->SetInitialContents(id, std::move(initial));
    return true;
  }

  ///////////////////////////////////////////////////////////////////////////
  // 2D Textures
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::Texture)
  {
    WrappedIDirect3DTexture9 *wrapper = dynamic_cast<WrappedIDirect3DTexture9 *>(res);
    if(!wrapper)
      return false;

    IDirect3DTexture9 *real = wrapper->GetReal();
    DWORD levelCount = real->GetLevelCount();

    D3DSURFACE_DESC desc;
    real->GetLevelDesc(0, &desc);

    // Compute total size across all mip levels
    uint64_t totalSize = 0;
    for(DWORD mip = 0; mip < levelCount; mip++)
    {
      D3DSURFACE_DESC mipDesc;
      real->GetLevelDesc(mip, &mipDesc);
      totalSize += GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
    }

    D3D9InitialContents initial(D3D9ResourceType::Texture, NULL);
    initial.shadowData = new byte[totalSize];
    initial.shadowDataLen = totalSize;

    byte *dst = initial.shadowData;

    if(desc.Pool == D3DPOOL_MANAGED || desc.Pool == D3DPOOL_SYSTEMMEM)
    {
      // Can lock directly
      for(DWORD mip = 0; mip < levelCount; mip++)
      {
        D3DSURFACE_DESC mipDesc;
        real->GetLevelDesc(mip, &mipDesc);

        uint32_t mipSize = GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
        uint32_t expectedPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
        uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format)
                            ? (mipDesc.Height + 3) / 4
                            : mipDesc.Height;

        D3DLOCKED_RECT locked;
        HRESULT hr = real->LockRect(mip, &locked, NULL, D3DLOCK_READONLY);
        if(SUCCEEDED(hr))
        {
          // Copy row by row in case pitch differs
          byte *src = (byte *)locked.pBits;
          for(uint32_t row = 0; row < rows; row++)
          {
            memcpy(dst, src, expectedPitch);
            dst += expectedPitch;
            src += locked.Pitch;
          }
          real->UnlockRect(mip);
        }
        else
        {
          RDCERR("Failed to lock texture mip %u for initial state: %08x", mip, hr);
          memset(dst, 0, mipSize);
          dst += mipSize;
        }
      }
    }
    else if(desc.Pool == D3DPOOL_DEFAULT)
    {
      if(desc.Usage & D3DUSAGE_RENDERTARGET)
      {
        // Use GetRenderTargetData on level 0 only (higher mips are not supported by the API)
        // For mip 0:
        D3DSURFACE_DESC mipDesc;
        real->GetLevelDesc(0, &mipDesc);

        IDirect3DSurface9 *rtSurf = NULL;
        IDirect3DSurface9 *sysSurf = NULL;

        HRESULT hr = real->GetSurfaceLevel(0, &rtSurf);
        if(SUCCEEDED(hr))
        {
          hr = m_pDevice->CreateOffscreenPlainSurface(mipDesc.Width, mipDesc.Height, mipDesc.Format,
                                                      D3DPOOL_SYSTEMMEM, &sysSurf, NULL);
          if(SUCCEEDED(hr))
          {
            hr = m_pDevice->GetRenderTargetData(rtSurf, sysSurf);
            if(SUCCEEDED(hr))
            {
              uint32_t expectedPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
              uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format)
                                  ? (mipDesc.Height + 3) / 4
                                  : mipDesc.Height;

              D3DLOCKED_RECT locked;
              hr = sysSurf->LockRect(&locked, NULL, D3DLOCK_READONLY);
              if(SUCCEEDED(hr))
              {
                byte *src = (byte *)locked.pBits;
                for(uint32_t row = 0; row < rows; row++)
                {
                  memcpy(dst, src, expectedPitch);
                  dst += expectedPitch;
                  src += locked.Pitch;
                }
                sysSurf->UnlockRect();
              }
              else
              {
                uint32_t mipSize =
                    GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
                memset(dst, 0, mipSize);
                dst += mipSize;
              }
            }
            else
            {
              RDCWARN("GetRenderTargetData failed for texture initial state: %08x", hr);
              uint32_t mipSize =
                  GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
              memset(dst, 0, mipSize);
              dst += mipSize;
            }
            SAFE_RELEASE(sysSurf);
          }
          SAFE_RELEASE(rtSurf);
        }

        // Zero remaining mip levels (can't read them from render targets)
        for(DWORD mip = 1; mip < levelCount; mip++)
        {
          D3DSURFACE_DESC mipLevelDesc;
          real->GetLevelDesc(mip, &mipLevelDesc);
          uint32_t mipSize =
              GetD3D9SurfaceByteSize(mipLevelDesc.Format, mipLevelDesc.Width, mipLevelDesc.Height);
          memset(dst, 0, mipSize);
          dst += mipSize;
        }
      }
      else
      {
        // DEFAULT pool non-RT: not easily readable, store zeroed data
        RDCWARN("Cannot read initial state from DEFAULT pool non-RT texture");
        memset(initial.shadowData, 0, (size_t)totalSize);
      }
    }
    else
    {
      // Unknown pool
      memset(initial.shadowData, 0, (size_t)totalSize);
    }

    GetResourceManager()->SetInitialContents(id, std::move(initial));
    return true;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Cube Textures
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::CubeTexture)
  {
    WrappedIDirect3DCubeTexture9 *wrapper =
        dynamic_cast<WrappedIDirect3DCubeTexture9 *>(res);
    if(!wrapper)
      return false;

    IDirect3DCubeTexture9 *real = wrapper->GetReal();
    DWORD levelCount = real->GetLevelCount();

    D3DSURFACE_DESC desc;
    real->GetLevelDesc(0, &desc);

    // Compute total size: 6 faces * all mip levels
    uint64_t totalSize = 0;
    for(DWORD mip = 0; mip < levelCount; mip++)
    {
      D3DSURFACE_DESC mipDesc;
      real->GetLevelDesc(mip, &mipDesc);
      totalSize += 6 * GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
    }

    D3D9InitialContents initial(D3D9ResourceType::CubeTexture, NULL);
    initial.shadowData = new byte[totalSize];
    initial.shadowDataLen = totalSize;

    byte *dst = initial.shadowData;

    if(desc.Pool == D3DPOOL_MANAGED || desc.Pool == D3DPOOL_SYSTEMMEM)
    {
      for(int face = 0; face < 6; face++)
      {
        D3DCUBEMAP_FACES d3dFace = (D3DCUBEMAP_FACES)face;

        for(DWORD mip = 0; mip < levelCount; mip++)
        {
          D3DSURFACE_DESC mipDesc;
          real->GetLevelDesc(mip, &mipDesc);

          uint32_t mipSize =
              GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
          uint32_t expectedPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
          uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format)
                              ? (mipDesc.Height + 3) / 4
                              : mipDesc.Height;

          D3DLOCKED_RECT locked;
          HRESULT hr = real->LockRect(d3dFace, mip, &locked, NULL, D3DLOCK_READONLY);
          if(SUCCEEDED(hr))
          {
            byte *src = (byte *)locked.pBits;
            for(uint32_t row = 0; row < rows; row++)
            {
              memcpy(dst, src, expectedPitch);
              dst += expectedPitch;
              src += locked.Pitch;
            }
            real->UnlockRect(d3dFace, mip);
          }
          else
          {
            RDCERR("Failed to lock cube texture face %d mip %u: %08x", face, mip, hr);
            memset(dst, 0, mipSize);
            dst += mipSize;
          }
        }
      }
    }
    else
    {
      // DEFAULT pool cube textures: store zeroed data
      RDCWARN("Cannot read initial state from DEFAULT pool cube texture");
      memset(initial.shadowData, 0, (size_t)totalSize);
    }

    GetResourceManager()->SetInitialContents(id, std::move(initial));
    return true;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Volume Textures
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::VolumeTexture)
  {
    WrappedIDirect3DVolumeTexture9 *wrapper =
        dynamic_cast<WrappedIDirect3DVolumeTexture9 *>(res);
    if(!wrapper)
      return false;

    IDirect3DVolumeTexture9 *real = wrapper->GetReal();
    DWORD levelCount = real->GetLevelCount();

    D3DVOLUME_DESC desc;
    real->GetLevelDesc(0, &desc);

    // Compute total size across all mip levels
    uint64_t totalSize = 0;
    for(DWORD mip = 0; mip < levelCount; mip++)
    {
      D3DVOLUME_DESC mipDesc;
      real->GetLevelDesc(mip, &mipDesc);
      totalSize +=
          GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height) * mipDesc.Depth;
    }

    D3D9InitialContents initial(D3D9ResourceType::VolumeTexture, NULL);
    initial.shadowData = new byte[totalSize];
    initial.shadowDataLen = totalSize;

    byte *dst = initial.shadowData;

    if(desc.Pool == D3DPOOL_MANAGED || desc.Pool == D3DPOOL_SYSTEMMEM)
    {
      for(DWORD mip = 0; mip < levelCount; mip++)
      {
        D3DVOLUME_DESC mipDesc;
        real->GetLevelDesc(mip, &mipDesc);

        uint32_t sliceSize =
            GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
        uint32_t expectedRowPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
        uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format)
                            ? (mipDesc.Height + 3) / 4
                            : mipDesc.Height;

        D3DLOCKED_BOX locked;
        HRESULT hr = real->LockBox(mip, &locked, NULL, D3DLOCK_READONLY);
        if(SUCCEEDED(hr))
        {
          byte *sliceBase = (byte *)locked.pBits;
          for(UINT depth = 0; depth < mipDesc.Depth; depth++)
          {
            byte *src = sliceBase + depth * locked.SlicePitch;
            for(uint32_t row = 0; row < rows; row++)
            {
              memcpy(dst, src, expectedRowPitch);
              dst += expectedRowPitch;
              src += locked.RowPitch;
            }
          }
          real->UnlockBox(mip);
        }
        else
        {
          RDCERR("Failed to lock volume texture mip %u: %08x", mip, hr);
          memset(dst, 0, sliceSize * mipDesc.Depth);
          dst += sliceSize * mipDesc.Depth;
        }
      }
    }
    else
    {
      RDCWARN("Cannot read initial state from DEFAULT pool volume texture");
      memset(initial.shadowData, 0, (size_t)totalSize);
    }

    GetResourceManager()->SetInitialContents(id, std::move(initial));
    return true;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Standalone Surfaces (not part of a texture)
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::Surface)
  {
    WrappedIDirect3DSurface9 *wrapper = dynamic_cast<WrappedIDirect3DSurface9 *>(res);
    if(!wrapper)
      return false;

    IDirect3DSurface9 *real = wrapper->GetReal();

    D3DSURFACE_DESC desc;
    real->GetDesc(&desc);

    uint32_t surfSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);

    D3D9InitialContents initial(D3D9ResourceType::Surface, NULL);
    initial.shadowData = new byte[surfSize];
    initial.shadowDataLen = surfSize;

    byte *dst = initial.shadowData;

    if(desc.Pool == D3DPOOL_MANAGED || desc.Pool == D3DPOOL_SYSTEMMEM)
    {
      uint32_t expectedPitch = ComputeRowPitch(desc.Format, desc.Width);
      uint32_t rows =
          IsD3D9FormatCompressed(desc.Format) ? (desc.Height + 3) / 4 : desc.Height;

      D3DLOCKED_RECT locked;
      HRESULT hr = real->LockRect(&locked, NULL, D3DLOCK_READONLY);
      if(SUCCEEDED(hr))
      {
        byte *src = (byte *)locked.pBits;
        for(uint32_t row = 0; row < rows; row++)
        {
          memcpy(dst, src, expectedPitch);
          dst += expectedPitch;
          src += locked.Pitch;
        }
        real->UnlockRect();
      }
      else
      {
        RDCERR("Failed to lock surface for initial state: %08x", hr);
        memset(dst, 0, surfSize);
      }
    }
    else if(desc.Pool == D3DPOOL_DEFAULT && (desc.Usage & D3DUSAGE_RENDERTARGET))
    {
      IDirect3DSurface9 *sysSurf = NULL;
      HRESULT hr = m_pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                          D3DPOOL_SYSTEMMEM, &sysSurf, NULL);
      if(SUCCEEDED(hr))
      {
        hr = m_pDevice->GetRenderTargetData(real, sysSurf);
        if(SUCCEEDED(hr))
        {
          uint32_t expectedPitch = ComputeRowPitch(desc.Format, desc.Width);
          uint32_t rows =
              IsD3D9FormatCompressed(desc.Format) ? (desc.Height + 3) / 4 : desc.Height;

          D3DLOCKED_RECT locked;
          hr = sysSurf->LockRect(&locked, NULL, D3DLOCK_READONLY);
          if(SUCCEEDED(hr))
          {
            byte *src = (byte *)locked.pBits;
            for(uint32_t row = 0; row < rows; row++)
            {
              memcpy(dst, src, expectedPitch);
              dst += expectedPitch;
              src += locked.Pitch;
            }
            sysSurf->UnlockRect();
          }
          else
          {
            memset(dst, 0, surfSize);
          }
        }
        else
        {
          RDCWARN("GetRenderTargetData failed for surface initial state: %08x", hr);
          memset(dst, 0, surfSize);
        }
        SAFE_RELEASE(sysSurf);
      }
      else
      {
        RDCERR("Failed to create offscreen plain surface: %08x", hr);
        memset(dst, 0, surfSize);
      }
    }
    else
    {
      RDCWARN("Cannot read initial state from DEFAULT pool non-RT surface");
      memset(dst, 0, surfSize);
    }

    GetResourceManager()->SetInitialContents(id, std::move(initial));
    return true;
  }

  // For resource types that don't need initial state (shaders, declarations, etc.)
  return true;
}

///////////////////////////////////////////////////////////////////////////
// Create_InitialState
///////////////////////////////////////////////////////////////////////////

void WrappedIDirect3DDevice9::Create_InitialState(ResourceId id, IUnknown *live, bool hasData)
{
  if(hasData)
  {
    // Data was already deserialized and set via Serialise_InitialState.
    // Nothing more to do.
    return;
  }

  // No serialized data: create an empty initial state.
  D3D9ResourceType type = IdentifyResourceType(live);

  D3D9InitialContents initial(type, NULL);

  GetResourceManager()->SetInitialContents(id, std::move(initial));
}

///////////////////////////////////////////////////////////////////////////
// Apply_InitialState
///////////////////////////////////////////////////////////////////////////

void WrappedIDirect3DDevice9::Apply_InitialState(IUnknown *live, D3D9InitialContents &data)
{
  D3D9ResourceType type = data.resourceType;

  if(!data.shadowData || data.shadowDataLen == 0)
    return;

  ///////////////////////////////////////////////////////////////////////////
  // Vertex Buffers
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::VertexBuffer)
  {
    WrappedIDirect3DVertexBuffer9 *wrapper =
        dynamic_cast<WrappedIDirect3DVertexBuffer9 *>(live);
    if(!wrapper)
      return;

    void *bufData = NULL;
    HRESULT hr = wrapper->GetReal()->Lock(0, 0, &bufData, 0);
    if(SUCCEEDED(hr) && bufData)
    {
      UINT copyLen = RDCMIN((UINT)data.shadowDataLen, wrapper->GetLength());
      memcpy(bufData, data.shadowData, copyLen);
      wrapper->GetReal()->Unlock();
    }
    else
    {
      RDCERR("Failed to lock vertex buffer for Apply_InitialState: %08x", hr);
    }
    return;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Index Buffers
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::IndexBuffer)
  {
    WrappedIDirect3DIndexBuffer9 *wrapper =
        dynamic_cast<WrappedIDirect3DIndexBuffer9 *>(live);
    if(!wrapper)
      return;

    void *bufData = NULL;
    HRESULT hr = wrapper->GetReal()->Lock(0, 0, &bufData, 0);
    if(SUCCEEDED(hr) && bufData)
    {
      UINT copyLen = RDCMIN((UINT)data.shadowDataLen, wrapper->GetLength());
      memcpy(bufData, data.shadowData, copyLen);
      wrapper->GetReal()->Unlock();
    }
    else
    {
      RDCERR("Failed to lock index buffer for Apply_InitialState: %08x", hr);
    }
    return;
  }

  ///////////////////////////////////////////////////////////////////////////
  // 2D Textures
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::Texture)
  {
    WrappedIDirect3DTexture9 *wrapper = dynamic_cast<WrappedIDirect3DTexture9 *>(live);
    if(!wrapper)
      return;

    IDirect3DTexture9 *real = wrapper->GetReal();
    DWORD levelCount = real->GetLevelCount();

    byte *src = data.shadowData;
    uint64_t remaining = data.shadowDataLen;

    for(DWORD mip = 0; mip < levelCount; mip++)
    {
      D3DSURFACE_DESC mipDesc;
      real->GetLevelDesc(mip, &mipDesc);

      uint32_t mipSize = GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
      uint32_t expectedPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
      uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format) ? (mipDesc.Height + 3) / 4
                                                              : mipDesc.Height;

      if(remaining < mipSize)
        break;

      D3DLOCKED_RECT locked;
      HRESULT hr = real->LockRect(mip, &locked, NULL, 0);
      if(SUCCEEDED(hr))
      {
        byte *dst = (byte *)locked.pBits;
        for(uint32_t row = 0; row < rows; row++)
        {
          memcpy(dst, src, expectedPitch);
          dst += locked.Pitch;
          src += expectedPitch;
        }
        real->UnlockRect(mip);
      }
      else
      {
        RDCERR("Failed to lock texture mip %u for Apply_InitialState: %08x", mip, hr);
        src += mipSize;
      }

      remaining -= mipSize;
    }
    return;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Cube Textures
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::CubeTexture)
  {
    WrappedIDirect3DCubeTexture9 *wrapper =
        dynamic_cast<WrappedIDirect3DCubeTexture9 *>(live);
    if(!wrapper)
      return;

    IDirect3DCubeTexture9 *real = wrapper->GetReal();
    DWORD levelCount = real->GetLevelCount();

    byte *src = data.shadowData;
    uint64_t remaining = data.shadowDataLen;

    for(int face = 0; face < 6; face++)
    {
      D3DCUBEMAP_FACES d3dFace = (D3DCUBEMAP_FACES)face;

      for(DWORD mip = 0; mip < levelCount; mip++)
      {
        D3DSURFACE_DESC mipDesc;
        real->GetLevelDesc(mip, &mipDesc);

        uint32_t mipSize =
            GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
        uint32_t expectedPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
        uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format) ? (mipDesc.Height + 3) / 4
                                                                : mipDesc.Height;

        if(remaining < mipSize)
          break;

        D3DLOCKED_RECT locked;
        HRESULT hr = real->LockRect(d3dFace, mip, &locked, NULL, 0);
        if(SUCCEEDED(hr))
        {
          byte *dst = (byte *)locked.pBits;
          for(uint32_t row = 0; row < rows; row++)
          {
            memcpy(dst, src, expectedPitch);
            dst += locked.Pitch;
            src += expectedPitch;
          }
          real->UnlockRect(d3dFace, mip);
        }
        else
        {
          RDCERR("Failed to lock cube texture face %d mip %u: %08x", face, mip, hr);
          src += mipSize;
        }

        remaining -= mipSize;
      }
    }
    return;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Volume Textures
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::VolumeTexture)
  {
    WrappedIDirect3DVolumeTexture9 *wrapper =
        dynamic_cast<WrappedIDirect3DVolumeTexture9 *>(live);
    if(!wrapper)
      return;

    IDirect3DVolumeTexture9 *real = wrapper->GetReal();
    DWORD levelCount = real->GetLevelCount();

    byte *src = data.shadowData;
    uint64_t remaining = data.shadowDataLen;

    for(DWORD mip = 0; mip < levelCount; mip++)
    {
      D3DVOLUME_DESC mipDesc;
      real->GetLevelDesc(mip, &mipDesc);

      uint32_t sliceSize =
          GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
      uint32_t totalMipSize = sliceSize * mipDesc.Depth;
      uint32_t expectedRowPitch = ComputeRowPitch(mipDesc.Format, mipDesc.Width);
      uint32_t rows = IsD3D9FormatCompressed(mipDesc.Format) ? (mipDesc.Height + 3) / 4
                                                              : mipDesc.Height;

      if(remaining < totalMipSize)
        break;

      D3DLOCKED_BOX locked;
      HRESULT hr = real->LockBox(mip, &locked, NULL, 0);
      if(SUCCEEDED(hr))
      {
        byte *sliceBase = (byte *)locked.pBits;
        for(UINT depth = 0; depth < mipDesc.Depth; depth++)
        {
          byte *dst = sliceBase + depth * locked.SlicePitch;
          for(uint32_t row = 0; row < rows; row++)
          {
            memcpy(dst, src, expectedRowPitch);
            dst += locked.RowPitch;
            src += expectedRowPitch;
          }
        }
        real->UnlockBox(mip);
      }
      else
      {
        RDCERR("Failed to lock volume texture mip %u: %08x", mip, hr);
        src += totalMipSize;
      }

      remaining -= totalMipSize;
    }
    return;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Surfaces
  ///////////////////////////////////////////////////////////////////////////
  if(type == D3D9ResourceType::Surface)
  {
    WrappedIDirect3DSurface9 *wrapper = dynamic_cast<WrappedIDirect3DSurface9 *>(live);
    if(!wrapper)
      return;

    IDirect3DSurface9 *real = wrapper->GetReal();

    D3DSURFACE_DESC desc;
    real->GetDesc(&desc);

    uint32_t expectedPitch = ComputeRowPitch(desc.Format, desc.Width);
    uint32_t rows =
        IsD3D9FormatCompressed(desc.Format) ? (desc.Height + 3) / 4 : desc.Height;

    D3DLOCKED_RECT locked;
    HRESULT hr = real->LockRect(&locked, NULL, 0);
    if(SUCCEEDED(hr))
    {
      byte *src = data.shadowData;
      byte *dst = (byte *)locked.pBits;
      for(uint32_t row = 0; row < rows; row++)
      {
        memcpy(dst, src, expectedPitch);
        dst += locked.Pitch;
        src += expectedPitch;
      }
      real->UnlockRect();
    }
    else
    {
      RDCERR("Failed to lock surface for Apply_InitialState: %08x", hr);
    }
    return;
  }
}

///////////////////////////////////////////////////////////////////////////
// Serialise_InitialState
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_InitialState(SerialiserType &ser, ResourceId id,
                                                     D3D9ResourceRecord *record,
                                                     const D3D9InitialContents *initial)
{
  D3D9ResourceType type;
  uint64_t dataLen = 0;

  if(ser.IsWriting())
  {
    if(initial)
    {
      type = initial->resourceType;
      dataLen = initial->shadowDataLen;
    }
    else
    {
      type = record ? record->resType : D3D9ResourceType::Unknown;
      dataLen = 0;
    }
  }

  SERIALISE_ELEMENT(type);
  SERIALISE_ELEMENT(dataLen);

  if(dataLen > 0)
  {
    if(ser.IsWriting())
    {
      ser.Serialise("data"_lit, initial->shadowData, dataLen, SerialiserFlags::NoFlags);
    }
    else
    {
      // Reading: allocate and deserialise
      byte *buf = new byte[(size_t)dataLen];
      ser.Serialise("data"_lit, buf, dataLen, SerialiserFlags::NoFlags);

      D3D9InitialContents contents(type, NULL);
      contents.shadowData = buf;
      contents.shadowDataLen = dataLen;

      GetResourceManager()->SetInitialContents(id, std::move(contents));
    }
  }
  else
  {
    if(ser.IsReading())
    {
      D3D9InitialContents contents(type, NULL);
      GetResourceManager()->SetInitialContents(id, std::move(contents));
    }
  }

  return true;
}

template bool WrappedIDirect3DDevice9::Serialise_InitialState(ReadSerialiser &ser, ResourceId id,
                                                              D3D9ResourceRecord *record,
                                                              const D3D9InitialContents *initial);
template bool WrappedIDirect3DDevice9::Serialise_InitialState(WriteSerialiser &ser, ResourceId id,
                                                              D3D9ResourceRecord *record,
                                                              const D3D9InitialContents *initial);
