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

#include "d3d9_replay.h"
#include "maths/formatpacking.h"
#include "replay/dummy_driver.h"
#include "serialise/rdcfile.h"
#include "strings/string_utils.h"
#include "d3d9_buffers.h"
#include "d3d9_device.h"
#include "d3d9_resources.h"
#include "d3d9_shaders.h"

D3D9Replay::D3D9Replay(WrappedIDirect3DDevice9 *d)
{
  RenderDoc::Inst().RegisterMemoryRegion(this, sizeof(D3D9Replay));

  m_pDevice = d;
  m_Proxy = false;

  RDCEraseEl(m_DriverInfo);
}

D3D9Replay::~D3D9Replay()
{
  RenderDoc::Inst().UnregisterMemoryRegion(this);
}

void D3D9Replay::Shutdown()
{
  for(auto it = m_OutputWindows.begin(); it != m_OutputWindows.end(); ++it)
  {
    SAFE_RELEASE(it->second.swap);
    SAFE_RELEASE(it->second.bb);
    SAFE_RELEASE(it->second.ds);
  }
  m_OutputWindows.clear();

  // explicitly delete the device, as all the replay resources created will be keeping refs on it
  delete m_pDevice;
}

RDResult D3D9Replay::FatalErrorCheck()
{
  return ResultCode::Succeeded;
}

IReplayDriver *D3D9Replay::MakeDummyDriver()
{
  IReplayDriver *dummy = new DummyDriver(this, {}, m_pDevice->DetachStructuredFile());

  return dummy;
}

////////////////////////////////////////////////////////////////
// API Properties
////////////////////////////////////////////////////////////////

rdcarray<GPUDevice> D3D9Replay::GetAvailableGPUs()
{
  rdcarray<GPUDevice> ret;

  // D3D9 doesn't have robust adapter enumeration like DXGI;
  // report a single default device.
  GPUDevice dev;
  dev.vendor = GPUVendor::Unknown;
  dev.deviceID = 0;
  dev.driver = "";
  dev.name = "Default D3D9 Adapter";
  dev.apis = {GraphicsAPI::D3D11};    // closest match for UI purposes
  ret.push_back(dev);

  return ret;
}

APIProperties D3D9Replay::GetAPIProperties()
{
  APIProperties ret = {};

  ret.pipelineType = GraphicsAPI::D3D11;    // D3D9 is not a separate enum; use D3D11 as proxy
  ret.localRenderer = GraphicsAPI::D3D11;
  ret.vendor = m_DriverInfo.vendor;
  ret.degraded = false;
  ret.shaderDebugging = false;
  ret.pixelHistory = false;

  return ret;
}

////////////////////////////////////////////////////////////////
// Resources
////////////////////////////////////////////////////////////////

ResourceDescription &D3D9Replay::GetResourceDesc(ResourceId id)
{
  auto it = m_ResourceIdx.find(id);
  if(it == m_ResourceIdx.end())
  {
    m_ResourceIdx[id] = m_Resources.size();
    m_Resources.push_back(ResourceDescription());
    m_Resources.back().resourceId = id;
    return m_Resources.back();
  }

  return m_Resources[it->second];
}

rdcarray<ResourceDescription> D3D9Replay::GetResources()
{
  return m_Resources;
}

rdcarray<BufferDescription> D3D9Replay::GetBuffers()
{
  rdcarray<BufferDescription> ret;

  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  const auto &resMap = rm->GetResourceMap();

  for(auto it = resMap.begin(); it != resMap.end(); ++it)
  {
    ResourceId id = it->first;
    IUnknown *res = it->second;
    if(!res)
      continue;

    // skip replay-only resources
    if(ResourceIDGen::IsReplayOnlyID(id))
      continue;

    // Check if this is a vertex or index buffer
    IDirect3DVertexBuffer9 *vb = NULL;
    IDirect3DIndexBuffer9 *ib = NULL;

    if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DVertexBuffer9), (void **)&vb)) && vb)
    {
      D3DVERTEXBUFFER_DESC desc;
      vb->GetDesc(&desc);
      vb->Release();

      BufferDescription buf;
      buf.resourceId = id;
      buf.length = desc.Size;
      buf.creationFlags = BufferCategory::Vertex;
      buf.gpuAddress = 0;
      ret.push_back(buf);
    }
    else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DIndexBuffer9), (void **)&ib)) && ib)
    {
      D3DINDEXBUFFER_DESC desc;
      ib->GetDesc(&desc);
      ib->Release();

      BufferDescription buf;
      buf.resourceId = id;
      buf.length = desc.Size;
      buf.creationFlags = BufferCategory::Index;
      buf.gpuAddress = 0;
      ret.push_back(buf);
    }
  }

  return ret;
}

BufferDescription D3D9Replay::GetBuffer(ResourceId id)
{
  BufferDescription ret = {};
  ret.resourceId = id;

  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  IUnknown *res = rm->GetResource(id, true);
  if(!res)
    return ret;

  IDirect3DVertexBuffer9 *vb = NULL;
  IDirect3DIndexBuffer9 *ib = NULL;

  if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DVertexBuffer9), (void **)&vb)) && vb)
  {
    D3DVERTEXBUFFER_DESC desc;
    vb->GetDesc(&desc);
    vb->Release();

    ret.length = desc.Size;
    ret.creationFlags = BufferCategory::Vertex;
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DIndexBuffer9), (void **)&ib)) && ib)
  {
    D3DINDEXBUFFER_DESC desc;
    ib->GetDesc(&desc);
    ib->Release();

    ret.length = desc.Size;
    ret.creationFlags = BufferCategory::Index;
  }

  return ret;
}

rdcarray<TextureDescription> D3D9Replay::GetTextures()
{
  rdcarray<TextureDescription> ret;

  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  const auto &resMap = rm->GetResourceMap();

  for(auto it = resMap.begin(); it != resMap.end(); ++it)
  {
    ResourceId id = it->first;
    IUnknown *res = it->second;
    if(!res)
      continue;

    // skip replay-only resources
    if(ResourceIDGen::IsReplayOnlyID(id))
      continue;

    IDirect3DTexture9 *tex2d = NULL;
    IDirect3DCubeTexture9 *texCube = NULL;
    IDirect3DVolumeTexture9 *tex3d = NULL;

    if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DTexture9), (void **)&tex2d)) && tex2d)
    {
      D3DSURFACE_DESC desc;
      tex2d->GetLevelDesc(0, &desc);

      TextureDescription tex;
      tex.resourceId = id;
      tex.type = TextureType::Texture2D;
      tex.width = desc.Width;
      tex.height = desc.Height;
      tex.depth = 1;
      tex.arraysize = 1;
      tex.mips = tex2d->GetLevelCount();
      tex.msQual = 0;
      tex.msSamp = (desc.MultiSampleType == D3DMULTISAMPLE_NONE) ? 1 : (uint32_t)desc.MultiSampleType;
      tex.format = MakeResourceFormat(desc.Format);
      tex.creationFlags = TextureCategory::NoFlags;
      if(desc.Usage & D3DUSAGE_RENDERTARGET)
        tex.creationFlags |= TextureCategory::ColorTarget;
      if(desc.Usage & D3DUSAGE_DEPTHSTENCIL)
        tex.creationFlags |= TextureCategory::DepthTarget;
      tex.byteSize = 0;
      for(DWORD m = 0; m < tex.mips; m++)
      {
        D3DSURFACE_DESC mipDesc;
        tex2d->GetLevelDesc(m, &mipDesc);
        tex.byteSize += GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height);
      }

      tex2d->Release();
      ret.push_back(tex);
    }
    else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DCubeTexture9), (void **)&texCube)) &&
            texCube)
    {
      D3DSURFACE_DESC desc;
      texCube->GetLevelDesc(0, &desc);

      TextureDescription tex;
      tex.resourceId = id;
      tex.type = TextureType::TextureCube;
      tex.width = desc.Width;
      tex.height = desc.Height;
      tex.depth = 1;
      tex.arraysize = 6;
      tex.mips = texCube->GetLevelCount();
      tex.msQual = 0;
      tex.msSamp = 1;
      tex.format = MakeResourceFormat(desc.Format);
      tex.creationFlags = TextureCategory::NoFlags;
      tex.byteSize = 0;
      for(DWORD m = 0; m < tex.mips; m++)
      {
        D3DSURFACE_DESC mipDesc;
        texCube->GetLevelDesc(m, &mipDesc);
        tex.byteSize +=
            GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height) * 6;
      }

      texCube->Release();
      ret.push_back(tex);
    }
    else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DVolumeTexture9), (void **)&tex3d)) &&
            tex3d)
    {
      D3DVOLUME_DESC desc;
      tex3d->GetLevelDesc(0, &desc);

      TextureDescription tex;
      tex.resourceId = id;
      tex.type = TextureType::Texture3D;
      tex.width = desc.Width;
      tex.height = desc.Height;
      tex.depth = desc.Depth;
      tex.arraysize = 1;
      tex.mips = tex3d->GetLevelCount();
      tex.msQual = 0;
      tex.msSamp = 1;
      tex.format = MakeResourceFormat(desc.Format);
      tex.creationFlags = TextureCategory::NoFlags;
      tex.byteSize = 0;
      for(DWORD m = 0; m < tex.mips; m++)
      {
        D3DVOLUME_DESC mipDesc;
        tex3d->GetLevelDesc(m, &mipDesc);
        tex.byteSize +=
            GetD3D9SurfaceByteSize(mipDesc.Format, mipDesc.Width, mipDesc.Height) * mipDesc.Depth;
      }

      tex3d->Release();
      ret.push_back(tex);
    }
  }

  return ret;
}

TextureDescription D3D9Replay::GetTexture(ResourceId id)
{
  TextureDescription ret = {};
  ret.resourceId = id;

  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  IUnknown *res = rm->GetResource(id, true);
  if(!res)
    return ret;

  IDirect3DTexture9 *tex2d = NULL;
  IDirect3DCubeTexture9 *texCube = NULL;
  IDirect3DVolumeTexture9 *tex3d = NULL;

  if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DTexture9), (void **)&tex2d)) && tex2d)
  {
    D3DSURFACE_DESC desc;
    tex2d->GetLevelDesc(0, &desc);

    ret.type = TextureType::Texture2D;
    ret.width = desc.Width;
    ret.height = desc.Height;
    ret.depth = 1;
    ret.arraysize = 1;
    ret.mips = tex2d->GetLevelCount();
    ret.msQual = 0;
    ret.msSamp =
        (desc.MultiSampleType == D3DMULTISAMPLE_NONE) ? 1 : (uint32_t)desc.MultiSampleType;
    ret.format = MakeResourceFormat(desc.Format);
    tex2d->Release();
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DCubeTexture9), (void **)&texCube)) &&
          texCube)
  {
    D3DSURFACE_DESC desc;
    texCube->GetLevelDesc(0, &desc);

    ret.type = TextureType::TextureCube;
    ret.width = desc.Width;
    ret.height = desc.Height;
    ret.depth = 1;
    ret.arraysize = 6;
    ret.mips = texCube->GetLevelCount();
    ret.msQual = 0;
    ret.msSamp = 1;
    ret.format = MakeResourceFormat(desc.Format);
    texCube->Release();
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DVolumeTexture9), (void **)&tex3d)) &&
          tex3d)
  {
    D3DVOLUME_DESC desc;
    tex3d->GetLevelDesc(0, &desc);

    ret.type = TextureType::Texture3D;
    ret.width = desc.Width;
    ret.height = desc.Height;
    ret.depth = desc.Depth;
    ret.arraysize = 1;
    ret.mips = tex3d->GetLevelCount();
    ret.msQual = 0;
    ret.msSamp = 1;
    ret.format = MakeResourceFormat(desc.Format);
    tex3d->Release();
  }

  return ret;
}

rdcarray<DebugMessage> D3D9Replay::GetDebugMessages()
{
  return {};
}

////////////////////////////////////////////////////////////////
// Shaders
////////////////////////////////////////////////////////////////

rdcarray<ShaderEntryPoint> D3D9Replay::GetShaderEntryPoints(ResourceId shader)
{
  rdcarray<ShaderEntryPoint> ret;
  ShaderEntryPoint entry;
  entry.name = "main";
  entry.stage = ShaderStage::Vertex;    // will be overridden when we can tell VS from PS
  ret.push_back(entry);
  return ret;
}

const ShaderReflection *D3D9Replay::GetShader(ResourceId pipeline, ResourceId shader,
                                              ShaderEntryPoint entry)
{
  return NULL;
}

rdcarray<rdcstr> D3D9Replay::GetDisassemblyTargets(bool withPipeline)
{
  rdcarray<rdcstr> ret;
  ret.push_back("D3D9 Bytecode");
  return ret;
}

rdcstr D3D9Replay::DisassembleShader(ResourceId pipeline, const ShaderReflection *refl,
                                     const rdcstr &target)
{
  return "";
}

rdcarray<EventUsage> D3D9Replay::GetUsage(ResourceId id)
{
  return {};
}

////////////////////////////////////////////////////////////////
// Pipeline state
////////////////////////////////////////////////////////////////

void D3D9Replay::SavePipelineState(uint32_t eventId)
{
  const D3D9RenderState &rs = m_pDevice->GetRenderState();

  // Input Assembly
  m_PipeState.inputAssembly.FVF = rs.FVF;
  m_PipeState.inputAssembly.vertexElements.clear();
  m_PipeState.inputAssembly.vertexBuffers.clear();

  // Populate vertex buffers from stream sources
  for(UINT i = 0; i < D3D9_MAX_STREAMS; i++)
  {
    if(rs.streamSources[i].buffer != ResourceId())
    {
      D3D9Pipe::VertexBuffer vb;
      vb.resourceId = rs.streamSources[i].buffer;
      vb.byteOffset = rs.streamSources[i].offsetInBytes;
      vb.byteStride = rs.streamSources[i].stride;
      vb.frequency = rs.streamSources[i].freq;
      m_PipeState.inputAssembly.vertexBuffers.push_back(vb);
    }
  }

  // Index buffer
  m_PipeState.inputAssembly.indexBuffer.resourceId = rs.indices;
  m_PipeState.inputAssembly.indexBuffer.byteStride = 0;    // determined at draw time

  // Vertex shader
  m_PipeState.vertexShader.resourceId = rs.vertexShader;
  m_PipeState.vertexShader.reflection = NULL;

  // VS constants
  m_PipeState.vertexShader.constants.floatConstants.resize(D3D9_MAX_VS_CONSTANTS_F * 4);
  memcpy(m_PipeState.vertexShader.constants.floatConstants.data(), rs.vsConstantsF,
         D3D9_MAX_VS_CONSTANTS_F * 4 * sizeof(float));

  m_PipeState.vertexShader.constants.intConstants.resize(D3D9_MAX_VS_CONSTANTS_I * 4);
  memcpy(m_PipeState.vertexShader.constants.intConstants.data(), rs.vsConstantsI,
         D3D9_MAX_VS_CONSTANTS_I * 4 * sizeof(int32_t));

  m_PipeState.vertexShader.constants.boolConstants.resize(D3D9_MAX_VS_CONSTANTS_B);
  for(UINT i = 0; i < D3D9_MAX_VS_CONSTANTS_B; i++)
    m_PipeState.vertexShader.constants.boolConstants[i] = rs.vsConstantsB[i] ? 1 : 0;

  // Pixel shader
  m_PipeState.pixelShader.resourceId = rs.pixelShader;
  m_PipeState.pixelShader.reflection = NULL;

  // PS constants
  m_PipeState.pixelShader.constants.floatConstants.resize(D3D9_MAX_PS_CONSTANTS_F * 4);
  memcpy(m_PipeState.pixelShader.constants.floatConstants.data(), rs.psConstantsF,
         D3D9_MAX_PS_CONSTANTS_F * 4 * sizeof(float));

  m_PipeState.pixelShader.constants.intConstants.resize(D3D9_MAX_PS_CONSTANTS_I * 4);
  memcpy(m_PipeState.pixelShader.constants.intConstants.data(), rs.psConstantsI,
         D3D9_MAX_PS_CONSTANTS_I * 4 * sizeof(int32_t));

  m_PipeState.pixelShader.constants.boolConstants.resize(D3D9_MAX_PS_CONSTANTS_B);
  for(UINT i = 0; i < D3D9_MAX_PS_CONSTANTS_B; i++)
    m_PipeState.pixelShader.constants.boolConstants[i] = rs.psConstantsB[i] ? 1 : 0;

  // Fixed function transforms
  memcpy(m_PipeState.fixedFunction.transforms.world.data(),
         &rs.transforms[D3DTS_WORLDMATRIX(0)], 16 * sizeof(float));
  memcpy(m_PipeState.fixedFunction.transforms.view.data(),
         &rs.transforms[D3DTS_VIEW], 16 * sizeof(float));
  memcpy(m_PipeState.fixedFunction.transforms.projection.data(),
         &rs.transforms[D3DTS_PROJECTION], 16 * sizeof(float));
  for(int i = 0; i < 8; i++)
  {
    memcpy(m_PipeState.fixedFunction.transforms.texture[i].data(),
           &rs.transforms[D3DTS_TEXTURE0 + i], 16 * sizeof(float));
  }

  // Lights
  m_PipeState.fixedFunction.lights.clear();
  for(size_t i = 0; i < rs.lights.size(); i++)
  {
    D3D9Pipe::Light l;
    l.type = rs.lights[i].light.Type;
    l.diffuse = FloatVector(rs.lights[i].light.Diffuse.r, rs.lights[i].light.Diffuse.g,
                            rs.lights[i].light.Diffuse.b, rs.lights[i].light.Diffuse.a);
    l.specular = FloatVector(rs.lights[i].light.Specular.r, rs.lights[i].light.Specular.g,
                             rs.lights[i].light.Specular.b, rs.lights[i].light.Specular.a);
    l.ambient = FloatVector(rs.lights[i].light.Ambient.r, rs.lights[i].light.Ambient.g,
                            rs.lights[i].light.Ambient.b, rs.lights[i].light.Ambient.a);
    l.position = FloatVector(rs.lights[i].light.Position.x, rs.lights[i].light.Position.y,
                             rs.lights[i].light.Position.z, 0.0f);
    l.direction = FloatVector(rs.lights[i].light.Direction.x, rs.lights[i].light.Direction.y,
                              rs.lights[i].light.Direction.z, 0.0f);
    l.range = rs.lights[i].light.Range;
    l.falloff = rs.lights[i].light.Falloff;
    l.attenuation0 = rs.lights[i].light.Attenuation0;
    l.attenuation1 = rs.lights[i].light.Attenuation1;
    l.attenuation2 = rs.lights[i].light.Attenuation2;
    l.theta = rs.lights[i].light.Theta;
    l.phi = rs.lights[i].light.Phi;
    l.enabled = rs.lights[i].enabled;
    m_PipeState.fixedFunction.lights.push_back(l);
  }

  // Material
  m_PipeState.fixedFunction.material.diffuse =
      FloatVector(rs.material.Diffuse.r, rs.material.Diffuse.g, rs.material.Diffuse.b,
                  rs.material.Diffuse.a);
  m_PipeState.fixedFunction.material.specular =
      FloatVector(rs.material.Specular.r, rs.material.Specular.g, rs.material.Specular.b,
                  rs.material.Specular.a);
  m_PipeState.fixedFunction.material.ambient =
      FloatVector(rs.material.Ambient.r, rs.material.Ambient.g, rs.material.Ambient.b,
                  rs.material.Ambient.a);
  m_PipeState.fixedFunction.material.emissive =
      FloatVector(rs.material.Emissive.r, rs.material.Emissive.g, rs.material.Emissive.b,
                  rs.material.Emissive.a);
  m_PipeState.fixedFunction.material.power = rs.material.Power;

  m_PipeState.fixedFunction.lightingEnabled = (rs.renderStates[D3DRS_LIGHTING] != FALSE);
  m_PipeState.fixedFunction.fogEnabled = (rs.renderStates[D3DRS_FOGENABLE] != FALSE);

  // Texture stages
  m_PipeState.textureStages.resize(D3D9_MAX_TEXTURE_STAGES);
  for(UINT i = 0; i < D3D9_MAX_TEXTURE_STAGES; i++)
  {
    D3D9Pipe::TextureStage &stage = m_PipeState.textureStages[i];
    stage.texture = rs.textures[i];

    // Sampler state
    stage.sampler.addressU = rs.samplerStates[i][D3DSAMP_ADDRESSU];
    stage.sampler.addressV = rs.samplerStates[i][D3DSAMP_ADDRESSV];
    stage.sampler.addressW = rs.samplerStates[i][D3DSAMP_ADDRESSW];
    stage.sampler.magFilter = rs.samplerStates[i][D3DSAMP_MAGFILTER];
    stage.sampler.minFilter = rs.samplerStates[i][D3DSAMP_MINFILTER];
    stage.sampler.mipFilter = rs.samplerStates[i][D3DSAMP_MIPFILTER];
    stage.sampler.maxAnisotropy = rs.samplerStates[i][D3DSAMP_MAXANISOTROPY];
    stage.sampler.maxMipLevel = rs.samplerStates[i][D3DSAMP_MAXMIPLEVEL];

    float mipBias;
    memcpy(&mipBias, &rs.samplerStates[i][D3DSAMP_MIPMAPLODBIAS], sizeof(float));
    stage.sampler.mipLODBias = mipBias;

    stage.sampler.sRGB = (rs.samplerStates[i][D3DSAMP_SRGBTEXTURE] != 0);

    // Texture stage state
    stage.stageState.colorOp = rs.textureStageStates[i][D3DTSS_COLOROP];
    stage.stageState.colorArg1 = rs.textureStageStates[i][D3DTSS_COLORARG1];
    stage.stageState.colorArg2 = rs.textureStageStates[i][D3DTSS_COLORARG2];
    stage.stageState.alphaOp = rs.textureStageStates[i][D3DTSS_ALPHAOP];
    stage.stageState.alphaArg1 = rs.textureStageStates[i][D3DTSS_ALPHAARG1];
    stage.stageState.alphaArg2 = rs.textureStageStates[i][D3DTSS_ALPHAARG2];
    stage.stageState.texCoordIndex = rs.textureStageStates[i][D3DTSS_TEXCOORDINDEX];
    stage.stageState.textureTransformFlags = rs.textureStageStates[i][D3DTSS_TEXTURETRANSFORMFLAGS];
  }

  // Rasterizer
  m_PipeState.rasterizer.fillMode = rs.renderStates[D3DRS_FILLMODE];
  m_PipeState.rasterizer.cullMode = rs.renderStates[D3DRS_CULLMODE];

  float depthBias, slopeScaledDepthBias;
  memcpy(&depthBias, &rs.renderStates[D3DRS_DEPTHBIAS], sizeof(float));
  memcpy(&slopeScaledDepthBias, &rs.renderStates[D3DRS_SLOPESCALEDEPTHBIAS], sizeof(float));
  m_PipeState.rasterizer.depthBias = depthBias;
  m_PipeState.rasterizer.slopeScaledDepthBias = slopeScaledDepthBias;

  m_PipeState.rasterizer.scissorEnable = (rs.renderStates[D3DRS_SCISSORTESTENABLE] != FALSE);
  m_PipeState.rasterizer.multisampleEnable = (rs.renderStates[D3DRS_MULTISAMPLEANTIALIAS] != FALSE);
  m_PipeState.rasterizer.antialiasedLineEnable =
      (rs.renderStates[D3DRS_ANTIALIASEDLINEENABLE] != FALSE);

  m_PipeState.rasterizer.clipPlaneEnable = rs.renderStates[D3DRS_CLIPPLANEENABLE];

  for(int i = 0; i < 6; i++)
  {
    for(int j = 0; j < 4; j++)
      m_PipeState.rasterizer.clipPlanes[i][j] = rs.clipPlanes[i][j];
  }

  // Output merger - blend
  m_PipeState.outputMerger.blendState.alphaBlendEnable =
      (rs.renderStates[D3DRS_ALPHABLENDENABLE] != FALSE);
  m_PipeState.outputMerger.blendState.srcBlend = rs.renderStates[D3DRS_SRCBLEND];
  m_PipeState.outputMerger.blendState.destBlend = rs.renderStates[D3DRS_DESTBLEND];
  m_PipeState.outputMerger.blendState.blendOp = rs.renderStates[D3DRS_BLENDOP];
  m_PipeState.outputMerger.blendState.separateAlphaBlendEnable =
      (rs.renderStates[D3DRS_SEPARATEALPHABLENDENABLE] != FALSE);
  m_PipeState.outputMerger.blendState.srcBlendAlpha = rs.renderStates[D3DRS_SRCBLENDALPHA];
  m_PipeState.outputMerger.blendState.destBlendAlpha = rs.renderStates[D3DRS_DESTBLENDALPHA];
  m_PipeState.outputMerger.blendState.blendOpAlpha = rs.renderStates[D3DRS_BLENDOPALPHA];
  m_PipeState.outputMerger.blendState.writeMask = rs.renderStates[D3DRS_COLORWRITEENABLE];

  m_PipeState.outputMerger.blendState.alphaTestEnable =
      (rs.renderStates[D3DRS_ALPHATESTENABLE] != FALSE);
  m_PipeState.outputMerger.blendState.alphaFunc = rs.renderStates[D3DRS_ALPHAFUNC];
  m_PipeState.outputMerger.blendState.alphaRef = rs.renderStates[D3DRS_ALPHAREF];

  // Output merger - depth/stencil
  m_PipeState.outputMerger.depthStencilState.depthEnable =
      (rs.renderStates[D3DRS_ZENABLE] != D3DZB_FALSE);
  m_PipeState.outputMerger.depthStencilState.depthWrite =
      (rs.renderStates[D3DRS_ZWRITEENABLE] != FALSE);
  m_PipeState.outputMerger.depthStencilState.depthFunc = rs.renderStates[D3DRS_ZFUNC];
  m_PipeState.outputMerger.depthStencilState.stencilEnable =
      (rs.renderStates[D3DRS_STENCILENABLE] != FALSE);
  m_PipeState.outputMerger.depthStencilState.stencilReadMask = rs.renderStates[D3DRS_STENCILMASK];
  m_PipeState.outputMerger.depthStencilState.stencilWriteMask =
      rs.renderStates[D3DRS_STENCILWRITEMASK];
  m_PipeState.outputMerger.depthStencilState.stencilRef = rs.renderStates[D3DRS_STENCILREF];
  m_PipeState.outputMerger.depthStencilState.stencilFail = rs.renderStates[D3DRS_STENCILFAIL];
  m_PipeState.outputMerger.depthStencilState.stencilZFail = rs.renderStates[D3DRS_STENCILZFAIL];
  m_PipeState.outputMerger.depthStencilState.stencilPass = rs.renderStates[D3DRS_STENCILPASS];
  m_PipeState.outputMerger.depthStencilState.stencilFunc = rs.renderStates[D3DRS_STENCILFUNC];
  m_PipeState.outputMerger.depthStencilState.twoSidedStencil =
      (rs.renderStates[D3DRS_TWOSIDEDSTENCILMODE] != FALSE);
  m_PipeState.outputMerger.depthStencilState.ccwStencilFail =
      rs.renderStates[D3DRS_CCW_STENCILFAIL];
  m_PipeState.outputMerger.depthStencilState.ccwStencilZFail =
      rs.renderStates[D3DRS_CCW_STENCILZFAIL];
  m_PipeState.outputMerger.depthStencilState.ccwStencilPass =
      rs.renderStates[D3DRS_CCW_STENCILPASS];
  m_PipeState.outputMerger.depthStencilState.ccwStencilFunc =
      rs.renderStates[D3DRS_CCW_STENCILFUNC];

  // Render targets
  m_PipeState.outputMerger.renderTargets.resize(D3D9_MAX_RENDER_TARGETS);
  for(UINT i = 0; i < D3D9_MAX_RENDER_TARGETS; i++)
    m_PipeState.outputMerger.renderTargets[i] = rs.renderTargets[i];

  m_PipeState.outputMerger.depthStencil = rs.depthStencil;
}

////////////////////////////////////////////////////////////////
// Descriptors (not applicable to D3D9)
////////////////////////////////////////////////////////////////

rdcarray<Descriptor> D3D9Replay::GetDescriptors(ResourceId descriptorStore,
                                                const rdcarray<DescriptorRange> &ranges)
{
  return {};
}

rdcarray<SamplerDescriptor> D3D9Replay::GetSamplerDescriptors(
    ResourceId descriptorStore, const rdcarray<DescriptorRange> &ranges)
{
  return {};
}

rdcarray<DescriptorAccess> D3D9Replay::GetDescriptorAccess(uint32_t eventId)
{
  return {};
}

rdcarray<DescriptorLogicalLocation> D3D9Replay::GetDescriptorLocations(
    ResourceId descriptorStore, const rdcarray<DescriptorRange> &ranges)
{
  return {};
}

////////////////////////////////////////////////////////////////
// Replay
////////////////////////////////////////////////////////////////

RDResult D3D9Replay::ReadLogInitialisation(RDCFile *rdc, bool storeStructuredBuffers)
{
  return m_pDevice->ReadLogInitialisation(rdc, storeStructuredBuffers);
}

void D3D9Replay::ReplayLog(uint32_t endEventID, ReplayLogType replayType)
{
  m_pDevice->ReplayLog(0, endEventID, replayType);
}

SDFile *D3D9Replay::GetStructuredFile()
{
  return m_pDevice->GetStructuredFile();
}

rdcarray<uint32_t> D3D9Replay::GetPassEvents(uint32_t eventId)
{
  rdcarray<uint32_t> passEvents;

  const ActionDescription *action = m_pDevice->GetAction(eventId);

  if(!action)
    return passEvents;

  const ActionDescription *start = action;
  while(start && start->previous && !(start->previous->flags & ActionFlags::Clear))
  {
    const ActionDescription *prev = start->previous;

    if(start->outputs != prev->outputs || start->depthOut != prev->depthOut)
      break;

    start = prev;
  }

  while(start)
  {
    if(start == action)
      break;

    if(start->flags & (ActionFlags::Drawcall | ActionFlags::Dispatch))
      passEvents.push_back(start->eventId);

    start = start->next;
  }

  return passEvents;
}

////////////////////////////////////////////////////////////////
// Post VS
////////////////////////////////////////////////////////////////

void D3D9Replay::InitPostVSBuffers(uint32_t eventId)
{
  // Not implemented for D3D9
}

void D3D9Replay::InitPostVSBuffers(const rdcarray<uint32_t> &passEvents)
{
  // Not implemented for D3D9
}

MeshFormat D3D9Replay::GetPostVSBuffers(uint32_t eventId, uint32_t instID, uint32_t viewID,
                                        MeshDataStage stage)
{
  MeshFormat ret;
  RDCEraseEl(ret);
  return ret;
}

////////////////////////////////////////////////////////////////
// Buffer & Texture Data
////////////////////////////////////////////////////////////////

void D3D9Replay::GetBufferData(ResourceId buff, uint64_t offset, uint64_t len, bytebuf &retData)
{
  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  IUnknown *res = rm->GetResource(buff, true);
  if(!res)
    return;

  IDirect3DVertexBuffer9 *vb = NULL;
  IDirect3DIndexBuffer9 *ib = NULL;

  if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DVertexBuffer9), (void **)&vb)) && vb)
  {
    D3DVERTEXBUFFER_DESC desc;
    vb->GetDesc(&desc);

    if(len == 0)
      len = desc.Size - offset;

    if(offset + len > desc.Size)
      len = desc.Size - offset;

    void *data = NULL;
    HRESULT hr = vb->Lock((UINT)offset, (UINT)len, &data, D3DLOCK_READONLY);
    if(SUCCEEDED(hr) && data)
    {
      retData.assign((byte *)data, (size_t)len);
      vb->Unlock();
    }
    vb->Release();
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DIndexBuffer9), (void **)&ib)) && ib)
  {
    D3DINDEXBUFFER_DESC desc;
    ib->GetDesc(&desc);

    if(len == 0)
      len = desc.Size - offset;

    if(offset + len > desc.Size)
      len = desc.Size - offset;

    void *data = NULL;
    HRESULT hr = ib->Lock((UINT)offset, (UINT)len, &data, D3DLOCK_READONLY);
    if(SUCCEEDED(hr) && data)
    {
      retData.assign((byte *)data, (size_t)len);
      ib->Unlock();
    }
    ib->Release();
  }
}

void D3D9Replay::GetTextureData(ResourceId tex, const Subresource &sub,
                                const GetTextureDataParams &params, bytebuf &data)
{
  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  IUnknown *res = rm->GetResource(tex, true);
  if(!res)
    return;

  IDirect3DTexture9 *tex2d = NULL;
  IDirect3DCubeTexture9 *texCube = NULL;
  IDirect3DVolumeTexture9 *tex3d = NULL;
  IDirect3DSurface9 *surf = NULL;

  if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DTexture9), (void **)&tex2d)) && tex2d)
  {
    D3DSURFACE_DESC desc;
    tex2d->GetLevelDesc(sub.mip, &desc);

    // For non-lockable textures (render targets, D3DPOOL_DEFAULT), use GetRenderTargetData
    IDirect3DSurface9 *srcSurf = NULL;
    tex2d->GetSurfaceLevel(sub.mip, &srcSurf);

    if(srcSurf)
    {
      // Create a lockable offscreen surface
      IDirect3DSurface9 *staging = NULL;
      HRESULT hr = m_pDevice->GetReal()->CreateOffscreenPlainSurface(
          desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL);

      if(SUCCEEDED(hr) && staging)
      {
        hr = m_pDevice->GetReal()->GetRenderTargetData(srcSurf, staging);
        if(FAILED(hr))
        {
          // If GetRenderTargetData fails (e.g., not a render target), try StretchRect or lock
          // directly
          D3DLOCKED_RECT locked;
          hr = srcSurf->LockRect(&locked, NULL, D3DLOCK_READONLY);
          if(SUCCEEDED(hr))
          {
            uint32_t byteSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
            data.resize(byteSize);

            uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
            if(IsD3D9FormatCompressed(desc.Format))
            {
              uint32_t blockSize = GetD3D9FormatBlockSize(desc.Format);
              uint32_t widthInBlocks = RDCMAX(1U, (desc.Width + blockSize - 1) / blockSize);
              uint32_t heightInBlocks = RDCMAX(1U, (desc.Height + blockSize - 1) / blockSize);
              rowSize = widthInBlocks * GetD3D9FormatByteSize(desc.Format);
              for(uint32_t row = 0; row < heightInBlocks; row++)
                memcpy(data.data() + row * rowSize,
                       (byte *)locked.pBits + row * locked.Pitch, rowSize);
            }
            else
            {
              for(uint32_t row = 0; row < desc.Height; row++)
                memcpy(data.data() + row * rowSize,
                       (byte *)locked.pBits + row * locked.Pitch, rowSize);
            }

            srcSurf->UnlockRect();
            SAFE_RELEASE(staging);
            SAFE_RELEASE(srcSurf);
            tex2d->Release();
            return;
          }
        }

        // If GetRenderTargetData succeeded, lock the staging surface
        if(SUCCEEDED(hr))
        {
          D3DLOCKED_RECT locked;
          hr = staging->LockRect(&locked, NULL, D3DLOCK_READONLY);
          if(SUCCEEDED(hr))
          {
            uint32_t byteSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
            data.resize(byteSize);

            uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
            if(IsD3D9FormatCompressed(desc.Format))
            {
              uint32_t blockSize = GetD3D9FormatBlockSize(desc.Format);
              uint32_t widthInBlocks = RDCMAX(1U, (desc.Width + blockSize - 1) / blockSize);
              uint32_t heightInBlocks = RDCMAX(1U, (desc.Height + blockSize - 1) / blockSize);
              rowSize = widthInBlocks * GetD3D9FormatByteSize(desc.Format);
              for(uint32_t row = 0; row < heightInBlocks; row++)
                memcpy(data.data() + row * rowSize,
                       (byte *)locked.pBits + row * locked.Pitch, rowSize);
            }
            else
            {
              for(uint32_t row = 0; row < desc.Height; row++)
                memcpy(data.data() + row * rowSize,
                       (byte *)locked.pBits + row * locked.Pitch, rowSize);
            }

            staging->UnlockRect();
          }
        }

        SAFE_RELEASE(staging);
      }

      SAFE_RELEASE(srcSurf);
    }

    tex2d->Release();
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DCubeTexture9), (void **)&texCube)) &&
          texCube)
  {
    D3DSURFACE_DESC desc;
    texCube->GetLevelDesc(sub.mip, &desc);

    D3DCUBEMAP_FACES face = (D3DCUBEMAP_FACES)RDCMIN(sub.slice, (uint32_t)5);

    IDirect3DSurface9 *srcSurf = NULL;
    texCube->GetCubeMapSurface(face, sub.mip, &srcSurf);

    if(srcSurf)
    {
      IDirect3DSurface9 *staging = NULL;
      HRESULT hr = m_pDevice->GetReal()->CreateOffscreenPlainSurface(
          desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL);

      if(SUCCEEDED(hr) && staging)
      {
        hr = m_pDevice->GetReal()->GetRenderTargetData(srcSurf, staging);
        if(FAILED(hr))
        {
          D3DLOCKED_RECT locked;
          hr = srcSurf->LockRect(&locked, NULL, D3DLOCK_READONLY);
          if(SUCCEEDED(hr))
          {
            uint32_t byteSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
            data.resize(byteSize);
            uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
            for(uint32_t row = 0; row < desc.Height; row++)
              memcpy(data.data() + row * rowSize,
                     (byte *)locked.pBits + row * locked.Pitch, rowSize);
            srcSurf->UnlockRect();
            SAFE_RELEASE(staging);
            SAFE_RELEASE(srcSurf);
            texCube->Release();
            return;
          }
        }

        if(SUCCEEDED(hr))
        {
          D3DLOCKED_RECT locked;
          hr = staging->LockRect(&locked, NULL, D3DLOCK_READONLY);
          if(SUCCEEDED(hr))
          {
            uint32_t byteSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
            data.resize(byteSize);
            uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
            for(uint32_t row = 0; row < desc.Height; row++)
              memcpy(data.data() + row * rowSize,
                     (byte *)locked.pBits + row * locked.Pitch, rowSize);
            staging->UnlockRect();
          }
        }

        SAFE_RELEASE(staging);
      }

      SAFE_RELEASE(srcSurf);
    }

    texCube->Release();
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DVolumeTexture9), (void **)&tex3d)) &&
          tex3d)
  {
    D3DVOLUME_DESC desc;
    tex3d->GetLevelDesc(sub.mip, &desc);

    D3DLOCKED_BOX locked;
    HRESULT hr = tex3d->LockBox(sub.mip, &locked, NULL, D3DLOCK_READONLY);
    if(SUCCEEDED(hr))
    {
      uint32_t sliceSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
      uint32_t totalSize = sliceSize * desc.Depth;
      data.resize(totalSize);

      uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
      for(UINT d = 0; d < desc.Depth; d++)
      {
        byte *sliceStart = (byte *)locked.pBits + d * locked.SlicePitch;
        for(UINT row = 0; row < desc.Height; row++)
        {
          memcpy(data.data() + d * sliceSize + row * rowSize,
                 sliceStart + row * locked.RowPitch, rowSize);
        }
      }

      tex3d->UnlockBox(sub.mip);
    }

    tex3d->Release();
  }
  else if(SUCCEEDED(res->QueryInterface(__uuidof(IDirect3DSurface9), (void **)&surf)) && surf)
  {
    D3DSURFACE_DESC desc;
    surf->GetDesc(&desc);

    IDirect3DSurface9 *staging = NULL;
    HRESULT hr = m_pDevice->GetReal()->CreateOffscreenPlainSurface(
        desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL);

    if(SUCCEEDED(hr) && staging)
    {
      hr = m_pDevice->GetReal()->GetRenderTargetData(surf, staging);
      if(FAILED(hr))
      {
        D3DLOCKED_RECT locked;
        hr = surf->LockRect(&locked, NULL, D3DLOCK_READONLY);
        if(SUCCEEDED(hr))
        {
          uint32_t byteSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
          data.resize(byteSize);
          uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
          for(uint32_t row = 0; row < desc.Height; row++)
            memcpy(data.data() + row * rowSize,
                   (byte *)locked.pBits + row * locked.Pitch, rowSize);
          surf->UnlockRect();
          SAFE_RELEASE(staging);
          surf->Release();
          return;
        }
      }

      if(SUCCEEDED(hr))
      {
        D3DLOCKED_RECT locked;
        hr = staging->LockRect(&locked, NULL, D3DLOCK_READONLY);
        if(SUCCEEDED(hr))
        {
          uint32_t byteSize = GetD3D9SurfaceByteSize(desc.Format, desc.Width, desc.Height);
          data.resize(byteSize);
          uint32_t rowSize = GetD3D9FormatByteSize(desc.Format) * desc.Width;
          for(uint32_t row = 0; row < desc.Height; row++)
            memcpy(data.data() + row * rowSize,
                   (byte *)locked.pBits + row * locked.Pitch, rowSize);
          staging->UnlockRect();
        }
      }

      SAFE_RELEASE(staging);
    }

    surf->Release();
  }
}

////////////////////////////////////////////////////////////////
// Shader compilation stubs
////////////////////////////////////////////////////////////////

void D3D9Replay::BuildTargetShader(ShaderEncoding sourceEncoding, const bytebuf &source,
                                   const rdcstr &entry, const ShaderCompileFlags &compileFlags,
                                   ShaderStage type, ResourceId &id, rdcstr &errors)
{
  id = ResourceId();
  errors = "D3D9 shader compilation not implemented in replay";
}

rdcarray<ShaderEncoding> D3D9Replay::GetTargetShaderEncodings()
{
  return {ShaderEncoding::DXBC};
}

void D3D9Replay::ReplaceResource(ResourceId from, ResourceId to)
{
}

void D3D9Replay::RemoveReplacement(ResourceId id)
{
}

void D3D9Replay::FreeTargetResource(ResourceId id)
{
}

void D3D9Replay::ClearReplayCache()
{
}

void D3D9Replay::ReloadShaderDebugInformation()
{
}

////////////////////////////////////////////////////////////////
// Counters
////////////////////////////////////////////////////////////////

rdcarray<GPUCounter> D3D9Replay::EnumerateCounters()
{
  return {};
}

CounterDescription D3D9Replay::DescribeCounter(GPUCounter counterID)
{
  CounterDescription desc = {};
  desc.counter = counterID;
  return desc;
}

rdcarray<CounterResult> D3D9Replay::FetchCounters(const rdcarray<GPUCounter> &counters)
{
  return {};
}

////////////////////////////////////////////////////////////////
// CBuffer variables
////////////////////////////////////////////////////////////////

void D3D9Replay::FillCBufferVariables(ResourceId pipeline, ResourceId shader, ShaderStage stage,
                                      rdcstr entryPoint, uint32_t cbufSlot,
                                      rdcarray<ShaderVariable> &outvars, const bytebuf &data)
{
  // D3D9 uses individual constant registers, not constant buffers
}

////////////////////////////////////////////////////////////////
// Debug / pixel history stubs
////////////////////////////////////////////////////////////////

rdcarray<PixelModification> D3D9Replay::PixelHistory(rdcarray<EventUsage> events,
                                                     ResourceId target, uint32_t x, uint32_t y,
                                                     const Subresource &sub, CompType typeCast)
{
  return {};
}

ShaderDebugTrace *D3D9Replay::DebugVertex(uint32_t eventId, uint32_t vertid, uint32_t instid,
                                          uint32_t idx, uint32_t view)
{
  return new ShaderDebugTrace;
}

ShaderDebugTrace *D3D9Replay::DebugPixel(uint32_t eventId, uint32_t x, uint32_t y,
                                         const DebugPixelInputs &inputs)
{
  return new ShaderDebugTrace;
}

ShaderDebugTrace *D3D9Replay::DebugThread(uint32_t eventId,
                                          const rdcfixedarray<uint32_t, 3> &groupid,
                                          const rdcfixedarray<uint32_t, 3> &threadid)
{
  return new ShaderDebugTrace;
}

ShaderDebugTrace *D3D9Replay::DebugMeshThread(uint32_t eventId,
                                              const rdcfixedarray<uint32_t, 3> &groupid,
                                              const rdcfixedarray<uint32_t, 3> &threadid)
{
  return new ShaderDebugTrace;
}

rdcarray<ShaderDebugState> D3D9Replay::ContinueDebug(ShaderDebugger *debugger)
{
  return {};
}

void D3D9Replay::FreeDebugger(ShaderDebugger *debugger)
{
  delete debugger;
}

// RenderOverlay is implemented in d3d9_overlay.cpp

bool D3D9Replay::IsRenderOutput(ResourceId id)
{
  const D3D9RenderState &rs = m_pDevice->GetRenderState();

  for(UINT i = 0; i < D3D9_MAX_RENDER_TARGETS; i++)
  {
    if(rs.renderTargets[i] == id)
      return true;
  }

  if(rs.depthStencil == id)
    return true;

  return false;
}

bool D3D9Replay::NeedRemapForFetch(const ResourceFormat &format)
{
  return false;
}

////////////////////////////////////////////////////////////////
// Output windows
////////////////////////////////////////////////////////////////

uint64_t D3D9Replay::MakeOutputWindow(WindowingData window, bool depth)
{
  OutputWindow outw;
  outw.wnd = window.win32.window;
  outw.swap = NULL;
  outw.bb = NULL;
  outw.ds = NULL;
  outw.width = 0;
  outw.height = 0;

  RECT rect;
  GetClientRect(outw.wnd, &rect);
  outw.width = rect.right - rect.left;
  outw.height = rect.bottom - rect.top;

  if(outw.width > 0 && outw.height > 0)
  {
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = outw.width;
    pp.BackBufferHeight = outw.height;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = outw.wnd;

    IDirect3DSwapChain9 *swap = NULL;
    HRESULT hr = m_pDevice->GetReal()->CreateAdditionalSwapChain(&pp, &swap);
    if(SUCCEEDED(hr) && swap)
    {
      outw.swap = swap;
      swap->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &outw.bb);

      if(depth)
      {
        m_pDevice->GetReal()->CreateDepthStencilSurface(
            outw.width, outw.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &outw.ds, NULL);
      }
    }
  }

  uint64_t id = m_OutputWindowID++;
  m_OutputWindows[id] = outw;
  return id;
}

void D3D9Replay::DestroyOutputWindow(uint64_t id)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return;

  SAFE_RELEASE(it->second.ds);
  SAFE_RELEASE(it->second.bb);
  SAFE_RELEASE(it->second.swap);
  m_OutputWindows.erase(it);
}

bool D3D9Replay::CheckResizeOutputWindow(uint64_t id)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return false;

  OutputWindow &outw = it->second;

  RECT rect;
  GetClientRect(outw.wnd, &rect);
  int w = rect.right - rect.left;
  int h = rect.bottom - rect.top;

  if(w != outw.width || h != outw.height)
  {
    outw.width = w;
    outw.height = h;

    SAFE_RELEASE(outw.ds);
    SAFE_RELEASE(outw.bb);
    SAFE_RELEASE(outw.swap);

    if(w > 0 && h > 0)
    {
      D3DPRESENT_PARAMETERS pp = {};
      pp.BackBufferWidth = w;
      pp.BackBufferHeight = h;
      pp.BackBufferFormat = D3DFMT_A8R8G8B8;
      pp.BackBufferCount = 1;
      pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
      pp.Windowed = TRUE;
      pp.hDeviceWindow = outw.wnd;

      IDirect3DSwapChain9 *swap = NULL;
      HRESULT hr = m_pDevice->GetReal()->CreateAdditionalSwapChain(&pp, &swap);
      if(SUCCEEDED(hr) && swap)
      {
        outw.swap = swap;
        swap->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &outw.bb);
      }
    }

    return true;
  }

  return false;
}

void D3D9Replay::GetOutputWindowDimensions(uint64_t id, int32_t &w, int32_t &h)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
  {
    w = h = 0;
    return;
  }

  w = it->second.width;
  h = it->second.height;
}

void D3D9Replay::GetOutputWindowData(uint64_t id, bytebuf &retData)
{
  // Not implemented
}

void D3D9Replay::ClearOutputWindowColor(uint64_t id, FloatVector col)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return;

  D3DCOLOR d3dCol = D3DCOLOR_COLORVALUE(col.x, col.y, col.z, col.w);
  m_pDevice->GetReal()->Clear(0, NULL, D3DCLEAR_TARGET, d3dCol, 1.0f, 0);
}

void D3D9Replay::ClearOutputWindowDepth(uint64_t id, float depth, uint8_t stencil)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return;

  m_pDevice->GetReal()->Clear(0, NULL, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, depth, stencil);
}

void D3D9Replay::BindOutputWindow(uint64_t id, bool depth)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return;

  OutputWindow &outw = it->second;

  m_pDevice->GetReal()->SetRenderTarget(0, outw.bb);
  if(depth && outw.ds)
    m_pDevice->GetReal()->SetDepthStencilSurface(outw.ds);
  else
    m_pDevice->GetReal()->SetDepthStencilSurface(NULL);
}

bool D3D9Replay::IsOutputWindowVisible(uint64_t id)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return false;

  return (IsWindowVisible(it->second.wnd) == TRUE);
}

void D3D9Replay::FlipOutputWindow(uint64_t id)
{
  auto it = m_OutputWindows.find(id);
  if(it == m_OutputWindows.end())
    return;

  OutputWindow &outw = it->second;

  if(outw.swap)
    outw.swap->Present(NULL, NULL, outw.wnd, NULL, 0);
}

////////////////////////////////////////////////////////////////
// Min/max, histogram, pick pixel stubs
////////////////////////////////////////////////////////////////

bool D3D9Replay::GetMinMax(ResourceId texid, const Subresource &sub, CompType typeCast,
                           float *minval, float *maxval)
{
  // Not implemented
  return false;
}

bool D3D9Replay::GetHistogram(ResourceId texid, const Subresource &sub, CompType typeCast,
                              float minval, float maxval, const rdcfixedarray<bool, 4> &channels,
                              rdcarray<uint32_t> &histogram)
{
  // Not implemented
  return false;
}

void D3D9Replay::PickPixel(ResourceId texture, uint32_t x, uint32_t y, const Subresource &sub,
                           CompType typeCast, float pixel[4])
{
  pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0.0f;
}

////////////////////////////////////////////////////////////////
// Proxy texture/buffer
////////////////////////////////////////////////////////////////

ResourceId D3D9Replay::CreateProxyTexture(const TextureDescription &templateTex)
{
  return ResourceId();
}

void D3D9Replay::SetProxyTextureData(ResourceId texid, const Subresource &sub, byte *data,
                                     size_t dataSize)
{
}

bool D3D9Replay::IsTextureSupported(const TextureDescription &tex)
{
  return true;
}

ResourceId D3D9Replay::CreateProxyBuffer(const BufferDescription &templateBuf)
{
  return ResourceId();
}

void D3D9Replay::SetProxyBufferData(ResourceId bufid, byte *data, size_t dataSize)
{
}

////////////////////////////////////////////////////////////////
// Rendering stubs
////////////////////////////////////////////////////////////////

void D3D9Replay::RenderMesh(uint32_t eventId, const rdcarray<MeshFormat> &secondaryDraws,
                            const MeshDisplay &cfg)
{
}

bool D3D9Replay::RenderTexture(TextureDisplay cfg)
{
  return false;
}

void D3D9Replay::SetCustomShaderIncludes(const rdcarray<rdcstr> &directories)
{
}

void D3D9Replay::BuildCustomShader(ShaderEncoding sourceEncoding, const bytebuf &source,
                                   const rdcstr &entry, const ShaderCompileFlags &compileFlags,
                                   ShaderStage type, ResourceId &id, rdcstr &errors)
{
  id = ResourceId();
  errors = "D3D9 custom shaders not implemented";
}

rdcarray<ShaderEncoding> D3D9Replay::GetCustomShaderEncodings()
{
  return {};
}

rdcarray<ShaderSourcePrefix> D3D9Replay::GetCustomShaderSourcePrefixes()
{
  return {};
}

ResourceId D3D9Replay::ApplyCustomShader(TextureDisplay &display)
{
  return ResourceId();
}

void D3D9Replay::FreeCustomShader(ResourceId id)
{
}

void D3D9Replay::RenderCheckerboard(FloatVector dark, FloatVector light)
{
}

void D3D9Replay::RenderHighlightBox(float w, float h, float scale)
{
}

uint32_t D3D9Replay::PickVertex(uint32_t eventId, int32_t width, int32_t height,
                                const MeshDisplay &cfg, uint32_t x, uint32_t y)
{
  return ~0U;
}

////////////////////////////////////////////////////////////////
// Factory function + static registration
////////////////////////////////////////////////////////////////

RDResult D3D9_CreateReplayDevice(RDCFile *rdc, const ReplayOptions &opts, IReplayDriver **driver)
{
  RDCDEBUG("Creating a D3D9 replay device");

  HMODULE d3d9lib = LoadLibraryA("d3d9.dll");
  if(!d3d9lib)
  {
    RETURN_ERROR_RESULT(ResultCode::APIInitFailed, "Failed to load d3d9.dll");
  }

  typedef IDirect3D9 *(WINAPI * PFN_Direct3DCreate9)(UINT);
  PFN_Direct3DCreate9 createFn =
      (PFN_Direct3DCreate9)GetProcAddress(d3d9lib, "Direct3DCreate9");

  if(!createFn)
  {
    RETURN_ERROR_RESULT(ResultCode::APIInitFailed,
                        "Failed to get Direct3DCreate9 from d3d9.dll");
  }

  IDirect3D9 *d3d9Obj = createFn(D3D_SDK_VERSION);
  if(!d3d9Obj)
  {
    RETURN_ERROR_RESULT(ResultCode::APIInitFailed, "Direct3DCreate9 returned NULL");
  }

  D3D9InitParams initParams;

  uint64_t ver = D3D9InitParams::CurrentVersion;

  const bool isProxy = (rdc == NULL);

  // If we have an RDCFile, read the init params from the frame capture section
  if(rdc)
  {
    int sectionIdx = rdc->SectionIndex(SectionType::FrameCapture);

    if(sectionIdx < 0)
    {
      d3d9Obj->Release();
      RETURN_ERROR_RESULT(ResultCode::FileCorrupted, "File does not contain captured API data");
    }

    ver = rdc->GetSectionProperties(sectionIdx).version;

    if(!D3D9InitParams::IsSupportedVersion(ver))
    {
      d3d9Obj->Release();
      RETURN_ERROR_RESULT(ResultCode::APIIncompatibleVersion,
                          "D3D9 capture is incompatible version %llu, newest supported by this "
                          "build of RenderDoc is %llu",
                          ver, D3D9InitParams::CurrentVersion);
    }

    StreamReader *reader = rdc->ReadSection(sectionIdx);

    ReadSerialiser ser(reader, Ownership::Stream);

    ser.SetVersion(ver);

    SystemChunk chunk = ser.ReadChunk<SystemChunk>();

    if(chunk != SystemChunk::DriverInit)
    {
      d3d9Obj->Release();
      RETURN_ERROR_RESULT(ResultCode::FileCorrupted,
                          "Expected to get a DriverInit chunk, instead got %u", chunk);
    }

    SERIALISE_ELEMENT(initParams);

    if(ser.IsErrored())
    {
      d3d9Obj->Release();
      return ser.GetError();
    }
  }

  // Create a hidden window for the replay device
  WNDCLASSEXA wc = {};
  wc.cbSize = sizeof(WNDCLASSEXA);
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "RenderDoc_D3D9_Hidden";
  RegisterClassExA(&wc);

  HWND wnd = CreateWindowExA(0, "RenderDoc_D3D9_Hidden", "RenderDoc D3D9", WS_OVERLAPPEDWINDOW, 0,
                             0, 4, 4, NULL, NULL, wc.hInstance, NULL);

  D3DPRESENT_PARAMETERS pp = {};
  pp.BackBufferWidth = 1;
  pp.BackBufferHeight = 1;
  pp.BackBufferFormat = D3DFMT_A8R8G8B8;
  pp.BackBufferCount = 1;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.Windowed = TRUE;
  pp.hDeviceWindow = wnd;

  IDirect3DDevice9 *dev = NULL;
  HRESULT hr = d3d9Obj->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                                     &pp, &dev);

  if(FAILED(hr))
  {
    // Fall back to software vertex processing
    hr = d3d9Obj->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                               &pp, &dev);
  }

  // Don't release d3d9Obj here -- it must stay alive while the device is alive.
  // The device will be wrapped and will outlive this scope.

  if(FAILED(hr) || !dev)
  {
    d3d9Obj->Release();
    DestroyWindow(wnd);
    RETURN_ERROR_RESULT(ResultCode::APIInitFailed,
                        "Failed to create D3D9 replay device: %s", ToStr(hr).c_str());
  }

  WrappedIDirect3DDevice9 *wrappedDev = new WrappedIDirect3DDevice9(dev, initParams);

  D3D9Replay *replay = wrappedDev->GetReplay();

  replay->SetProxy(isProxy);

  if(!isProxy)
  {
    RDCLOG("Created D3D9 replay device.");
  }

  *driver = (IReplayDriver *)replay;
  return ResultCode::Succeeded;
}

static DriverRegistration D3D9DriverRegistration(RDCDriver::D3D9, &D3D9_CreateReplayDevice);
