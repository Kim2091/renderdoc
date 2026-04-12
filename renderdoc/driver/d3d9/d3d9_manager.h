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

#include "core/resource_manager.h"
#include "d3d9_common.h"

enum class D3D9ResourceType
{
  Unknown,
  Device,
  SwapChain,
  Texture,
  CubeTexture,
  VolumeTexture,
  Surface,
  Volume,
  VertexBuffer,
  IndexBuffer,
  VertexShader,
  PixelShader,
  VertexDeclaration,
  StateBlock,
  Query,
};

DECLARE_REFLECTION_ENUM(D3D9ResourceType);

struct D3D9InitialContents
{
  D3D9InitialContents() = default;
  D3D9InitialContents(D3D9ResourceType t, IUnknown *r) : resourceType(t), resource(r) {}
  D3D9InitialContents(const D3D9InitialContents &) = delete;
  D3D9InitialContents(D3D9InitialContents &&other)
  {
    resourceType = other.resourceType;
    resource = other.resource;
    other.resource = NULL;
    shadowData = other.shadowData;
    other.shadowData = NULL;
    shadowDataLen = other.shadowDataLen;
    other.shadowDataLen = 0;
  }
  D3D9InitialContents &operator=(D3D9InitialContents &&other)
  {
    FreeInternal();
    resourceType = other.resourceType;
    resource = other.resource;
    other.resource = NULL;
    shadowData = other.shadowData;
    other.shadowData = NULL;
    shadowDataLen = other.shadowDataLen;
    other.shadowDataLen = 0;
    return *this;
  }

  ~D3D9InitialContents() { FreeInternal(); }

  template <typename Configuration>
  void Free(ResourceManager<Configuration> *rm)
  {
    FreeInternal();
  }

  D3D9ResourceType resourceType = D3D9ResourceType::Unknown;
  IUnknown *resource = NULL;

  // For write-only buffers, we keep a shadow copy
  byte *shadowData = NULL;
  uint64_t shadowDataLen = 0;

private:
  void FreeInternal()
  {
    SAFE_RELEASE(resource);
    SAFE_DELETE_ARRAY(shadowData);
    shadowDataLen = 0;
  }
};

struct D3D9ResourceRecord : public ResourceRecord
{
  D3D9ResourceRecord(ResourceId id) : ResourceRecord(id, true) {}
  ~D3D9ResourceRecord() {}

  D3D9ResourceType resType = D3D9ResourceType::Unknown;
  D3DPOOL pool = D3DPOOL_DEFAULT;
  DWORD usage = 0;
  uint64_t Length = 0;

  // For tracking Lock/Unlock shadow data on write-only resources
  byte *shadowData = NULL;
  uint64_t shadowDataLen = 0;
};

struct D3D9ResourceManagerConfiguration
{
  typedef IUnknown *WrappedResourceType;
  typedef IUnknown *RealResourceType;
  typedef D3D9ResourceRecord RecordType;
  typedef D3D9InitialContents InitialContentData;
};

class D3D9ResourceManager : public ResourceManager<D3D9ResourceManagerConfiguration>
{
public:
  D3D9ResourceManager(CaptureState &state, WrappedIDirect3DDevice9 *dev)
      : ResourceManager(state), m_Device(dev)
  {
  }

  void SetInternalResource(IUnknown *res);
  void FreeCaptureData();

  // Expose resource map iteration for replay enumeration
  typedef std::unordered_map<ResourceId, IUnknown *> ResourceMap;
  const ResourceMap &GetResourceMap() { return m_ResourceMap; }

  template <typename SerialiserType>
  bool Serialise_InitialState(SerialiserType &ser, ResourceId id, D3D9ResourceRecord *record,
                              const D3D9InitialContents *initial);

private:
  ResourceId GetID(IUnknown *res);
  bool ResourceTypeRelease(IUnknown *res);
  bool Need_InitialStateChunk(ResourceId id, const D3D9InitialContents &initial);
  bool Prepare_InitialState(IUnknown *res);
  uint64_t GetSize_InitialState(ResourceId id, const D3D9InitialContents &initial);
  bool Serialise_InitialState(WriteSerialiser &ser, ResourceId id, D3D9ResourceRecord *record,
                              const D3D9InitialContents *initial);
  void Create_InitialState(ResourceId id, IUnknown *live, bool hasData);
  void Apply_InitialState(IUnknown *live, D3D9InitialContents &data);

  WrappedIDirect3DDevice9 *m_Device;
};
