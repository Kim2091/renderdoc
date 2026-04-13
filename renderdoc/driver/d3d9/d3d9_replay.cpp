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
#include "maths/camera.h"
#include "maths/formatpacking.h"
#include "maths/matrix.h"
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
  for(auto it = m_ShaderReflectionCache.begin(); it != m_ShaderReflectionCache.end(); ++it)
    delete it->second;
  m_ShaderReflectionCache.clear();

  delete m_FFPPixelReflection;
  m_FFPPixelReflection = NULL;

  RenderDoc::Inst().UnregisterMemoryRegion(this);
}

void D3D9Replay::Shutdown()
{
  SAFE_RELEASE(m_Overlay.RenderTarget);
  SAFE_RELEASE(m_Overlay.Texture);
  m_Overlay.resourceId = ResourceId();
  m_Overlay.width = 0;
  m_Overlay.height = 0;

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
  dev.apis = {GraphicsAPI::D3D9};
  ret.push_back(dev);

  return ret;
}

APIProperties D3D9Replay::GetAPIProperties()
{
  APIProperties ret = {};

  ret.pipelineType = GraphicsAPI::D3D9;
  ret.localRenderer = GraphicsAPI::D3D9;
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

// SM1-3 bytecode register type constants (from D3D9 shader bytecode spec)
// These are extracted from bits [28..31] of source/dest parameter tokens,
// with bit [11] providing the 5th bit for SM2+.
enum D3D9RegType
{
  D3D9_REG_TEMP = 0,
  D3D9_REG_INPUT = 1,
  D3D9_REG_CONST = 2,
  D3D9_REG_ADDR_TEXTURE = 3,    // address reg in VS, texture coord in PS
  D3D9_REG_RASTOUT = 4,
  D3D9_REG_ATTROUT = 5,
  D3D9_REG_TEXCRDOUT = 6,    // also output in VS SM3
  D3D9_REG_OUTPUT = 6,
  D3D9_REG_CONSTINT = 7,
  D3D9_REG_COLOROUT = 8,
  D3D9_REG_DEPTHOUT = 9,
  D3D9_REG_SAMPLER = 10,
  D3D9_REG_CONST2 = 11,
  D3D9_REG_CONST3 = 12,
  D3D9_REG_CONST4 = 13,
  D3D9_REG_CONSTBOOL = 14,
  D3D9_REG_LOOP = 15,
  D3D9_REG_TEMPFLOAT16 = 16,
  D3D9_REG_MISCTYPE = 17,
  D3D9_REG_LABEL = 18,
  D3D9_REG_PREDICATE = 19,
};

// SM1-3 bytecode opcode constants (lower 16 bits of instruction token)
enum D3D9Opcode
{
  D3D9_OP_NOP = 0,
  D3D9_OP_MOV = 1,
  D3D9_OP_ADD = 2,
  D3D9_OP_SUB = 3,
  D3D9_OP_MAD = 4,
  D3D9_OP_MUL = 5,
  D3D9_OP_RCP = 6,
  D3D9_OP_RSQ = 7,
  D3D9_OP_DP3 = 8,
  D3D9_OP_DP4 = 9,
  D3D9_OP_MIN = 10,
  D3D9_OP_MAX = 11,
  D3D9_OP_SLT = 12,
  D3D9_OP_SGE = 13,
  D3D9_OP_EXP = 14,
  D3D9_OP_LOG = 15,
  D3D9_OP_LIT = 16,
  D3D9_OP_DST = 17,
  D3D9_OP_LRP = 18,
  D3D9_OP_FRC = 19,
  D3D9_OP_DCL = 31,
  D3D9_OP_POW = 32,
  D3D9_OP_ABS = 35,
  D3D9_OP_NRM = 36,
  D3D9_OP_SINCOS = 37,
  D3D9_OP_DEF = 40,
  D3D9_OP_DEFI = 42,
  D3D9_OP_DEFB = 45,
  D3D9_OP_TEX = 66,       // texld in SM2+
  D3D9_OP_TEXLDL = 93,
  D3D9_OP_END = 0xFFFF,
  D3D9_OP_COMMENT = 0xFFFE,
};

// SM1-3 DCL usage values (for dcl instructions)
enum D3D9DeclUsage
{
  D3D9_DECL_POSITION = 0,
  D3D9_DECL_BLENDWEIGHT = 1,
  D3D9_DECL_BLENDINDICES = 2,
  D3D9_DECL_NORMAL = 3,
  D3D9_DECL_PSIZE = 4,
  D3D9_DECL_TEXCOORD = 5,
  D3D9_DECL_TANGENT = 6,
  D3D9_DECL_BINORMAL = 7,
  D3D9_DECL_TESSFACTOR = 8,
  D3D9_DECL_POSITIONT = 9,
  D3D9_DECL_COLOR = 10,
  D3D9_DECL_FOG = 11,
  D3D9_DECL_DEPTH = 12,
  D3D9_DECL_SAMPLE = 13,
};

// SM1-3 sampler type (from DCL token bits [27..30])
enum D3D9SamplerType
{
  D3D9_SAMPLER_UNKNOWN = 0,
  D3D9_SAMPLER_2D = 2,
  D3D9_SAMPLER_CUBE = 3,
  D3D9_SAMPLER_VOLUME = 4,
};

static uint32_t D3D9_GetRegType(DWORD token)
{
  // bits [28..31] are the low 4 bits, bit [11] is the high bit (SM2+ extension)
  uint32_t low = (token >> 28) & 0xF;
  uint32_t high = (token >> 11) & 0x1;
  return low | (high << 4);
}

static uint32_t D3D9_GetRegNum(DWORD token)
{
  return token & 0x7FF;
}

static uint32_t D3D9_GetWriteMask(DWORD token)
{
  return (token >> 16) & 0xF;
}

static const char *D3D9_DeclUsageName(uint32_t usage)
{
  switch(usage)
  {
    case D3D9_DECL_POSITION: return "POSITION";
    case D3D9_DECL_BLENDWEIGHT: return "BLENDWEIGHT";
    case D3D9_DECL_BLENDINDICES: return "BLENDINDICES";
    case D3D9_DECL_NORMAL: return "NORMAL";
    case D3D9_DECL_PSIZE: return "PSIZE";
    case D3D9_DECL_TEXCOORD: return "TEXCOORD";
    case D3D9_DECL_TANGENT: return "TANGENT";
    case D3D9_DECL_BINORMAL: return "BINORMAL";
    case D3D9_DECL_TESSFACTOR: return "TESSFACTOR";
    case D3D9_DECL_POSITIONT: return "POSITIONT";
    case D3D9_DECL_COLOR: return "COLOR";
    case D3D9_DECL_FOG: return "FOG";
    case D3D9_DECL_DEPTH: return "DEPTH";
    case D3D9_DECL_SAMPLE: return "SAMPLE";
    default: return "UNKNOWN";
  }
}

static uint32_t D3D9_WriteMaskCompCount(uint32_t mask)
{
  uint32_t count = 0;
  if(mask & 0x1)
    count++;
  if(mask & 0x2)
    count++;
  if(mask & 0x4)
    count++;
  if(mask & 0x8)
    count++;
  return count;
}

ShaderReflection *D3D9Replay::BuildShaderReflection(ResourceId shaderId,
                                                    const rdcarray<DWORD> &bytecode,
                                                    ShaderStage stage)
{
  if(bytecode.empty())
    return NULL;

  ShaderReflection *refl = new ShaderReflection;
  refl->resourceId = shaderId;
  refl->entryPoint = "main";
  refl->stage = stage;
  refl->encoding = ShaderEncoding::DXBC;    // SM1-3 bytecode is the precursor to DXBC

  // Store raw bytecode
  refl->rawBytes.resize(bytecode.count() * sizeof(DWORD));
  memcpy(refl->rawBytes.data(), bytecode.data(), refl->rawBytes.size());

  const DWORD *tokens = bytecode.data();
  size_t numTokens = bytecode.count();

  if(numTokens < 1)
  {
    delete refl;
    return NULL;
  }

  // First token is the version token
  DWORD versionToken = tokens[0];
  uint32_t shaderMajor = (versionToken >> 8) & 0xFF;
  uint32_t shaderMinor = versionToken & 0xFF;
  bool isPS = ((versionToken >> 16) & 0xFFFF) == 0xFFFF;
  bool isVS = ((versionToken >> 16) & 0xFFFF) == 0xFFFE;

  (void)shaderMinor;

  // Override stage based on actual shader type in bytecode
  if(isPS)
    refl->stage = ShaderStage::Pixel;
  else if(isVS)
    refl->stage = ShaderStage::Vertex;

  // Track which registers are used, for building reflection data.
  // For DCL instructions, we track usage/index. For other instructions we track
  // register reads/writes to detect used float/int/bool constants and samplers.

  struct DeclInfo
  {
    uint32_t regType;
    uint32_t regNum;
    uint32_t usage;
    uint32_t usageIndex;
    uint32_t writeMask;
    uint32_t samplerType;    // for sampler DCLs
  };

  rdcarray<DeclInfo> inputDecls;
  rdcarray<DeclInfo> outputDecls;
  rdcarray<DeclInfo> samplerDecls;

  // Track highest constant register index used for float/int/bool constants
  uint32_t maxFloatConstUsed = 0;
  uint32_t maxIntConstUsed = 0;
  uint32_t maxBoolConstUsed = 0;
  bool hasFloatConsts = false;
  bool hasIntConsts = false;
  bool hasBoolConsts = false;

  // Track which sampler registers are referenced by tex* instructions
  bool samplerUsed[16] = {};

  size_t i = 1;    // skip version token
  while(i < numTokens)
  {
    DWORD instrToken = tokens[i];

    // Check for END token
    if(instrToken == 0x0000FFFF)
      break;

    uint32_t opcode = instrToken & 0xFFFF;

    // Comment block - skip
    if(opcode == D3D9_OP_COMMENT)
    {
      uint32_t commentLen = (instrToken >> 16) & 0x7FFF;
      i += 1 + commentLen;
      continue;
    }

    // Number of additional tokens after the instruction token
    // For SM2+, instruction length is encoded in bits [24..27] for most opcodes
    uint32_t instrLen = 0;
    if(shaderMajor >= 2)
    {
      instrLen = ((instrToken >> 24) & 0xF);
    }

    // Handle specific opcodes
    if(opcode == D3D9_OP_DCL && i + 2 < numTokens)
    {
      DWORD dclToken = tokens[i + 1];
      DWORD dstToken = tokens[i + 2];

      uint32_t regType = D3D9_GetRegType(dstToken);
      uint32_t regNum = D3D9_GetRegNum(dstToken);
      uint32_t writeMask = D3D9_GetWriteMask(dstToken);

      DeclInfo dcl;
      dcl.regType = regType;
      dcl.regNum = regNum;
      dcl.usage = dclToken & 0x1F;
      dcl.usageIndex = (dclToken >> 16) & 0xF;
      dcl.writeMask = writeMask;
      dcl.samplerType = (dclToken >> 27) & 0xF;

      if(regType == D3D9_REG_INPUT)
      {
        inputDecls.push_back(dcl);
      }
      else if(regType == D3D9_REG_SAMPLER)
      {
        samplerDecls.push_back(dcl);
        if(regNum < 16)
          samplerUsed[regNum] = true;
      }
      else if(regType == D3D9_REG_OUTPUT || regType == D3D9_REG_TEXCRDOUT ||
              regType == D3D9_REG_ATTROUT || regType == D3D9_REG_COLOROUT ||
              regType == D3D9_REG_DEPTHOUT || regType == D3D9_REG_RASTOUT)
      {
        outputDecls.push_back(dcl);
      }
      else if(regType == D3D9_REG_ADDR_TEXTURE && isPS)
      {
        // In pixel shaders, dcl of texture coordinate registers are inputs
        inputDecls.push_back(dcl);
      }

      i += 3;
      continue;
    }
    else if(opcode == D3D9_OP_DEF && i + 5 <= numTokens)
    {
      // def cN, x, y, z, w  --  5 tokens total (instr + dst + 4 floats)
      DWORD dstToken = tokens[i + 1];
      uint32_t regNum = D3D9_GetRegNum(dstToken);
      if(regNum + 1 > maxFloatConstUsed)
        maxFloatConstUsed = regNum + 1;
      hasFloatConsts = true;
      i += 5;
      continue;
    }
    else if(opcode == D3D9_OP_DEFI && i + 5 <= numTokens)
    {
      DWORD dstToken = tokens[i + 1];
      uint32_t regNum = D3D9_GetRegNum(dstToken);
      if(regNum + 1 > maxIntConstUsed)
        maxIntConstUsed = regNum + 1;
      hasIntConsts = true;
      i += 5;
      continue;
    }
    else if(opcode == D3D9_OP_DEFB && i + 2 < numTokens)
    {
      DWORD dstToken = tokens[i + 1];
      uint32_t regNum = D3D9_GetRegNum(dstToken);
      if(regNum + 1 > maxBoolConstUsed)
        maxBoolConstUsed = regNum + 1;
      hasBoolConsts = true;
      i += 3;
      continue;
    }

    // For non-DCL/DEF instructions, scan all operand tokens for constant register references
    if(shaderMajor >= 2 && instrLen > 0)
    {
      for(uint32_t t = 1; t <= instrLen && (i + t) < numTokens; t++)
      {
        DWORD paramToken = tokens[i + t];
        // Skip if this is an addressing mode token (bit 13 set in the previous token is relative)
        uint32_t regType = D3D9_GetRegType(paramToken);
        uint32_t regNum = D3D9_GetRegNum(paramToken);

        switch(regType)
        {
          case D3D9_REG_CONST:
          case D3D9_REG_CONST2:
          case D3D9_REG_CONST3:
          case D3D9_REG_CONST4:
          {
            uint32_t actualReg = regNum;
            if(regType == D3D9_REG_CONST2)
              actualReg += 2048;
            else if(regType == D3D9_REG_CONST3)
              actualReg += 4096;
            else if(regType == D3D9_REG_CONST4)
              actualReg += 6144;
            if(actualReg + 1 > maxFloatConstUsed)
              maxFloatConstUsed = actualReg + 1;
            hasFloatConsts = true;
            break;
          }
          case D3D9_REG_CONSTINT:
            if(regNum + 1 > maxIntConstUsed)
              maxIntConstUsed = regNum + 1;
            hasIntConsts = true;
            break;
          case D3D9_REG_CONSTBOOL:
            if(regNum + 1 > maxBoolConstUsed)
              maxBoolConstUsed = regNum + 1;
            hasBoolConsts = true;
            break;
          case D3D9_REG_SAMPLER:
            if(regNum < 16)
              samplerUsed[regNum] = true;
            break;
          default: break;
        }
      }

      i += 1 + instrLen;
      continue;
    }

    // SM1 instructions or unrecognised - skip based on opcode heuristic
    // SM1 has fixed instruction lengths per opcode. For safety, advance by 1
    // and rely on end token detection.
    if(shaderMajor < 2)
    {
      // For SM1, scan operand tokens for constant references too
      // SM1 instruction layout: dest param tokens have bit 31 set to 1,
      // source param tokens also have bit 31 set to 1, instruction token has bit 31 = 0.
      // We scan forward until we hit the next instruction token (bit 31 == 0) or END.
      size_t j = i + 1;
      while(j < numTokens && (tokens[j] & 0x80000000))
      {
        DWORD paramToken = tokens[j];
        uint32_t regType = D3D9_GetRegType(paramToken);
        uint32_t regNum = D3D9_GetRegNum(paramToken);

        switch(regType)
        {
          case D3D9_REG_CONST:
            if(regNum + 1 > maxFloatConstUsed)
              maxFloatConstUsed = regNum + 1;
            hasFloatConsts = true;
            break;
          case D3D9_REG_CONSTINT:
            if(regNum + 1 > maxIntConstUsed)
              maxIntConstUsed = regNum + 1;
            hasIntConsts = true;
            break;
          case D3D9_REG_CONSTBOOL:
            if(regNum + 1 > maxBoolConstUsed)
              maxBoolConstUsed = regNum + 1;
            hasBoolConsts = true;
            break;
          case D3D9_REG_SAMPLER:
            if(regNum < 16)
              samplerUsed[regNum] = true;
            break;
          default: break;
        }
        j++;
      }
      i = j;
      continue;
    }

    // Fallback: advance past instruction
    i += 1 + instrLen;
  }

  // Build input signature from DCL'd input registers
  for(size_t d = 0; d < inputDecls.size(); d++)
  {
    const DeclInfo &dcl = inputDecls[d];
    SigParameter sig;

    sig.semanticName = D3D9_DeclUsageName(dcl.usage);
    sig.semanticIndex = (uint16_t)dcl.usageIndex;
    if(dcl.usageIndex > 0)
      sig.semanticIdxName = StringFormat::Fmt("%s%u", sig.semanticName.c_str(), dcl.usageIndex);
    else
      sig.semanticIdxName = sig.semanticName;
    sig.needSemanticIndex = (dcl.usageIndex > 0);
    sig.varName = sig.semanticIdxName;

    sig.regIndex = dcl.regNum;
    sig.varType = VarType::Float;
    sig.regChannelMask = (uint8_t)(dcl.writeMask & 0xF);
    sig.channelUsedMask = sig.regChannelMask;
    sig.compCount = D3D9_WriteMaskCompCount(dcl.writeMask);

    // Map semantics to system values
    if(dcl.usage == D3D9_DECL_POSITION)
      sig.systemValue = ShaderBuiltin::Position;
    else if(dcl.usage == D3D9_DECL_PSIZE)
      sig.systemValue = ShaderBuiltin::PointSize;
    else
      sig.systemValue = ShaderBuiltin::Undefined;

    refl->inputSignature.push_back(sig);
  }

  // Build output signature
  // For SM3 VS, output registers have DCL. For PS, outputs are color/depth registers.
  for(size_t d = 0; d < outputDecls.size(); d++)
  {
    const DeclInfo &dcl = outputDecls[d];
    SigParameter sig;

    if(dcl.regType == D3D9_REG_COLOROUT)
    {
      sig.semanticName = "SV_Target";
      sig.semanticIndex = (uint16_t)dcl.regNum;
      sig.systemValue = ShaderBuiltin::ColorOutput;
    }
    else if(dcl.regType == D3D9_REG_DEPTHOUT)
    {
      sig.semanticName = "SV_Depth";
      sig.semanticIndex = 0;
      sig.systemValue = ShaderBuiltin::DepthOutput;
    }
    else if(dcl.regType == D3D9_REG_RASTOUT)
    {
      // rastout #0 = position, #1 = fog, #2 = point size
      if(dcl.regNum == 0)
      {
        sig.semanticName = "SV_Position";
        sig.systemValue = ShaderBuiltin::Position;
      }
      else if(dcl.regNum == 1)
      {
        sig.semanticName = "FOG";
        sig.systemValue = ShaderBuiltin::Undefined;
      }
      else
      {
        sig.semanticName = "PSIZE";
        sig.systemValue = ShaderBuiltin::PointSize;
      }
      sig.semanticIndex = 0;
    }
    else
    {
      sig.semanticName = D3D9_DeclUsageName(dcl.usage);
      sig.semanticIndex = (uint16_t)dcl.usageIndex;
      if(dcl.usage == D3D9_DECL_POSITION)
        sig.systemValue = ShaderBuiltin::Position;
      else
        sig.systemValue = ShaderBuiltin::Undefined;
    }

    if(sig.semanticIndex > 0)
      sig.semanticIdxName =
          StringFormat::Fmt("%s%u", sig.semanticName.c_str(), sig.semanticIndex);
    else
      sig.semanticIdxName = sig.semanticName;
    sig.needSemanticIndex = (sig.semanticIndex > 0);
    sig.varName = sig.semanticIdxName;

    sig.regIndex = dcl.regNum;
    sig.varType = VarType::Float;
    sig.regChannelMask = (uint8_t)(dcl.writeMask & 0xF);
    sig.channelUsedMask = sig.regChannelMask;
    sig.compCount = D3D9_WriteMaskCompCount(dcl.writeMask);

    refl->outputSignature.push_back(sig);
  }

  // For PS with no explicit output DCLs (SM < 3), synthesize color output
  if(isPS && refl->outputSignature.empty())
  {
    SigParameter sig;
    sig.semanticName = "SV_Target";
    sig.semanticIdxName = "SV_Target";
    sig.semanticIndex = 0;
    sig.regIndex = 0;
    sig.systemValue = ShaderBuiltin::ColorOutput;
    sig.varType = VarType::Float;
    sig.regChannelMask = 0xF;
    sig.channelUsedMask = 0xF;
    sig.compCount = 4;
    refl->outputSignature.push_back(sig);
  }

  // For VS with no explicit output DCLs (SM < 3), synthesize position output
  if(isVS && refl->outputSignature.empty())
  {
    SigParameter sig;
    sig.semanticName = "SV_Position";
    sig.semanticIdxName = "SV_Position";
    sig.semanticIndex = 0;
    sig.regIndex = 0;
    sig.systemValue = ShaderBuiltin::Position;
    sig.varType = VarType::Float;
    sig.regChannelMask = 0xF;
    sig.channelUsedMask = 0xF;
    sig.compCount = 4;
    refl->outputSignature.push_back(sig);
  }

  // Build constant blocks
  // D3D9 has three types of constants: float (c registers), int (i registers), bool (b registers)
  uint32_t cbufSlot = 0;

  // Clamp to hardware limits
  uint32_t maxF = isVS ? D3D9_MAX_VS_CONSTANTS_F : D3D9_MAX_PS_CONSTANTS_F;
  uint32_t maxI = isVS ? D3D9_MAX_VS_CONSTANTS_I : D3D9_MAX_PS_CONSTANTS_I;
  uint32_t maxB = isVS ? D3D9_MAX_VS_CONSTANTS_B : D3D9_MAX_PS_CONSTANTS_B;

  if(maxFloatConstUsed > maxF)
    maxFloatConstUsed = maxF;
  if(maxIntConstUsed > maxI)
    maxIntConstUsed = maxI;
  if(maxBoolConstUsed > maxB)
    maxBoolConstUsed = maxB;

  // Always expose float constants if the shader exists - even if we did not detect
  // explicit constant references the application may still set them via SetXxxShaderConstantF.
  // Use the full hardware limit so the UI can display all possible registers.
  {
    ConstantBlock cb;
    cb.name = "Float Constants";
    cb.bufferBacked = false;
    cb.fixedBindNumber = cbufSlot++;
    cb.byteSize = maxF * 4 * sizeof(float);

    for(uint32_t c = 0; c < maxF; c++)
    {
      ShaderConstant var;
      var.name = StringFormat::Fmt("c%u", c);
      var.byteOffset = c * 4 * sizeof(float);
      var.type.baseType = VarType::Float;
      var.type.rows = 1;
      var.type.columns = 4;
      var.type.elements = 1;
      var.type.flags = ShaderVariableFlags::RowMajorMatrix;
      cb.variables.push_back(var);
    }

    refl->constantBlocks.push_back(cb);
  }

  // Int constants
  {
    ConstantBlock cb;
    cb.name = "Int Constants";
    cb.bufferBacked = false;
    cb.fixedBindNumber = cbufSlot++;
    cb.byteSize = maxI * 4 * sizeof(int32_t);

    for(uint32_t c = 0; c < maxI; c++)
    {
      ShaderConstant var;
      var.name = StringFormat::Fmt("i%u", c);
      var.byteOffset = c * 4 * sizeof(int32_t);
      var.type.baseType = VarType::SInt;
      var.type.rows = 1;
      var.type.columns = 4;
      var.type.elements = 1;
      var.type.flags = ShaderVariableFlags::RowMajorMatrix;
      cb.variables.push_back(var);
    }

    refl->constantBlocks.push_back(cb);
  }

  // Bool constants
  {
    ConstantBlock cb;
    cb.name = "Bool Constants";
    cb.bufferBacked = false;
    cb.fixedBindNumber = cbufSlot++;
    cb.byteSize = maxB * sizeof(uint32_t);

    for(uint32_t c = 0; c < maxB; c++)
    {
      ShaderConstant var;
      var.name = StringFormat::Fmt("b%u", c);
      var.byteOffset = c * sizeof(uint32_t);
      var.type.baseType = VarType::Bool;
      var.type.rows = 1;
      var.type.columns = 1;
      var.type.elements = 1;
      var.type.flags = ShaderVariableFlags::RowMajorMatrix;
      cb.variables.push_back(var);
    }

    refl->constantBlocks.push_back(cb);
  }

  // Build sampler/texture resources from DCL'd samplers and detected sampler usage
  for(size_t d = 0; d < samplerDecls.size(); d++)
  {
    const DeclInfo &dcl = samplerDecls[d];

    ShaderResource res;
    res.name = StringFormat::Fmt("s%u", dcl.regNum);
    res.fixedBindNumber = dcl.regNum;
    res.isTexture = true;
    res.hasSampler = true;
    res.isReadOnly = true;
    res.isInputAttachment = false;
    res.descriptorType = DescriptorType::ImageSampler;

    switch(dcl.samplerType)
    {
      case D3D9_SAMPLER_2D: res.textureType = TextureType::Texture2D; break;
      case D3D9_SAMPLER_CUBE: res.textureType = TextureType::TextureCube; break;
      case D3D9_SAMPLER_VOLUME: res.textureType = TextureType::Texture3D; break;
      default: res.textureType = TextureType::Texture2D; break;
    }

    refl->readOnlyResources.push_back(res);

    // Also add a corresponding sampler entry
    ShaderSampler sam;
    sam.name = res.name;
    sam.fixedBindNumber = dcl.regNum;
    refl->samplers.push_back(sam);
  }

  // If no DCL'd samplers but tex* instructions reference samplers, create entries for them
  for(uint32_t s = 0; s < 16; s++)
  {
    if(!samplerUsed[s])
      continue;

    // Check if this sampler was already declared
    bool alreadyDeclared = false;
    for(size_t d = 0; d < samplerDecls.size(); d++)
    {
      if(samplerDecls[d].regNum == s)
      {
        alreadyDeclared = true;
        break;
      }
    }
    if(alreadyDeclared)
      continue;

    ShaderResource res;
    res.name = StringFormat::Fmt("s%u", s);
    res.fixedBindNumber = s;
    res.isTexture = true;
    res.hasSampler = true;
    res.isReadOnly = true;
    res.isInputAttachment = false;
    res.descriptorType = DescriptorType::ImageSampler;
    res.textureType = TextureType::Texture2D;    // default assumption
    refl->readOnlyResources.push_back(res);

    ShaderSampler sam;
    sam.name = res.name;
    sam.fixedBindNumber = s;
    refl->samplers.push_back(sam);
  }

  return refl;
}

ShaderReflection *D3D9Replay::GetShaderReflection(ResourceId shaderId)
{
  if(shaderId == ResourceId())
    return NULL;

  auto it = m_ShaderReflectionCache.find(shaderId);
  if(it != m_ShaderReflectionCache.end())
    return it->second;

  // Look up the shader resource and get its bytecode
  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  if(!rm->HasResource(shaderId))
  {
    m_ShaderReflectionCache[shaderId] = NULL;
    return NULL;
  }

  IUnknown *res = rm->GetResource(shaderId);
  if(!res)
  {
    m_ShaderReflectionCache[shaderId] = NULL;
    return NULL;
  }

  // Use QI to determine if this is a vertex or pixel shader
  D3D9WrappedInfo *info = NULL;
  HRESULT hr = res->QueryInterface(IID_ID3D9WrappedResource, (void **)&info);
  if(FAILED(hr) || !info)
  {
    m_ShaderReflectionCache[shaderId] = NULL;
    return NULL;
  }

  ShaderReflection *refl = NULL;

  if(info->type == D3D9WrappedType::VertexShader)
  {
    WrappedIDirect3DVertexShader9 *vs = (WrappedIDirect3DVertexShader9 *)res;
    const rdcarray<DWORD> &bytecode = vs->GetBytecode();
    refl = BuildShaderReflection(shaderId, bytecode, ShaderStage::Vertex);
  }
  else if(info->type == D3D9WrappedType::PixelShader)
  {
    WrappedIDirect3DPixelShader9 *ps = (WrappedIDirect3DPixelShader9 *)res;
    const rdcarray<DWORD> &bytecode = ps->GetBytecode();
    refl = BuildShaderReflection(shaderId, bytecode, ShaderStage::Pixel);
  }

  m_ShaderReflectionCache[shaderId] = refl;
  return refl;
}

ShaderReflection *D3D9Replay::GetFFPPixelReflection()
{
  const D3D9RenderState &rs = m_pDevice->GetRenderState();

  // Rebuild each time since bound textures can change per draw call.
  // Free the previous one if it exists.
  delete m_FFPPixelReflection;
  m_FFPPixelReflection = new ShaderReflection;

  ShaderReflection *refl = m_FFPPixelReflection;
  refl->stage = ShaderStage::Pixel;
  refl->entryPoint = "FFP";
  refl->encoding = ShaderEncoding::Unknown;
  refl->debugInfo.encoding = ShaderEncoding::Unknown;

  // Create readOnlyResources and samplers entries for each bound texture stage
  for(UINT i = 0; i < D3D9_MAX_TEXTURE_STAGES; i++)
  {
    if(rs.textures[i] != ResourceId())
    {
      ShaderResource res;
      res.name = StringFormat::Fmt("TexStage%u", i);
      res.fixedBindNumber = i;
      res.isTexture = true;
      res.hasSampler = true;
      res.isReadOnly = true;
      res.isInputAttachment = false;
      res.descriptorType = DescriptorType::ImageSampler;
      res.textureType = TextureType::Texture2D;    // default assumption for FFP
      refl->readOnlyResources.push_back(res);

      ShaderSampler sam;
      sam.name = res.name;
      sam.fixedBindNumber = i;
      refl->samplers.push_back(sam);
    }
  }

  return refl;
}

rdcarray<ShaderEntryPoint> D3D9Replay::GetShaderEntryPoints(ResourceId shader)
{
  rdcarray<ShaderEntryPoint> ret;

  ShaderStage stage = ShaderStage::Vertex;

  // Try to determine the actual shader stage from the resource
  ShaderReflection *refl = GetShaderReflection(shader);
  if(refl)
    stage = refl->stage;

  ShaderEntryPoint entry;
  entry.name = "main";
  entry.stage = stage;
  ret.push_back(entry);
  return ret;
}

const ShaderReflection *D3D9Replay::GetShader(ResourceId pipeline, ResourceId shader,
                                              ShaderEntryPoint entry)
{
  ShaderReflection *refl = GetShaderReflection(shader);
  if(!refl && shader == ResourceId() && entry.stage == ShaderStage::Pixel)
    return GetFFPPixelReflection();
  return refl;
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
  if(!m_D3D9PipelineState)
    return;

  D3D9Pipe::State &m_PipeState = *m_D3D9PipelineState;

  const D3D9RenderState &rs = m_pDevice->GetRenderState();

  // Input Assembly
  m_PipeState.inputAssembly.FVF = rs.FVF;
  m_PipeState.inputAssembly.vertexElements.clear();
  m_PipeState.inputAssembly.vertexBuffers.clear();

  // Populate vertex elements from vertex declaration
  if(rs.vertexDecl != ResourceId())
  {
    IUnknown *res = m_pDevice->GetResourceManager()->GetResource(rs.vertexDecl);
    if(res)
    {
      WrappedIDirect3DVertexDeclaration9 *decl =
          static_cast<WrappedIDirect3DVertexDeclaration9 *>((IDirect3DVertexDeclaration9 *)res);
      const rdcarray<D3DVERTEXELEMENT9> &elems = decl->GetElements();
      for(int i = 0; i < elems.count(); i++)
      {
        // Skip the end sentinel element (stream=0xFF, type=D3DDECLTYPE_UNUSED=17)
        if(elems[i].Stream == 0xFF || elems[i].Type == 17)
          break;
        D3D9Pipe::VertexElement ve;
        ve.stream = elems[i].Stream;
        ve.offset = elems[i].Offset;
        ve.type = elems[i].Type;
        ve.method = elems[i].Method;
        ve.usage = elems[i].Usage;
        ve.usageIndex = elems[i].UsageIndex;
        m_PipeState.inputAssembly.vertexElements.push_back(ve);
      }
    }
  }

  // Populate vertex buffers from stream sources - include ALL streams so indices match
  for(UINT i = 0; i < D3D9_MAX_STREAMS; i++)
  {
    D3D9Pipe::VertexBuffer vb;
    vb.resourceId = rs.streamSources[i].buffer;
    vb.byteOffset = rs.streamSources[i].offsetInBytes;
    vb.byteStride = rs.streamSources[i].stride;
    vb.frequency = rs.streamSources[i].freq;
    m_PipeState.inputAssembly.vertexBuffers.push_back(vb);
  }

  // Index buffer
  m_PipeState.inputAssembly.indexBuffer.resourceId = rs.indices;

  // Determine index buffer stride from the index buffer's D3DFORMAT
  m_PipeState.inputAssembly.indexBuffer.byteStride = 0;
  if(rs.indices != ResourceId())
  {
    IUnknown *ibRes = m_pDevice->GetResourceManager()->GetResource(rs.indices);
    if(ibRes)
    {
      D3D9WrappedInfo *info = GetD3D9WrappedInfo(ibRes);
      if(info && info->type == D3D9WrappedType::IndexBuffer)
      {
        WrappedIDirect3DIndexBuffer9 *ib = (WrappedIDirect3DIndexBuffer9 *)ibRes;
        D3DFORMAT fmt = ib->GetFormat();
        if(fmt == D3DFMT_INDEX16)
          m_PipeState.inputAssembly.indexBuffer.byteStride = 2;
        else if(fmt == D3DFMT_INDEX32)
          m_PipeState.inputAssembly.indexBuffer.byteStride = 4;
      }
    }
  }

  // Vertex shader
  m_PipeState.vertexShader.resourceId = rs.vertexShader;
  m_PipeState.vertexShader.reflection = GetShaderReflection(rs.vertexShader);

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
  if(rs.pixelShader != ResourceId())
    m_PipeState.pixelShader.reflection = GetShaderReflection(rs.pixelShader);
  else
    m_PipeState.pixelShader.reflection = GetFFPPixelReflection();

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
// Descriptors
// D3D9 doesn't have a descriptor system but we synthesize descriptor accesses
// so that GetReadOnlyResources() works for texture bindings.
////////////////////////////////////////////////////////////////

// D3D9 uses a fixed descriptor size of 1 byte per slot for addressing purposes.
static const uint32_t D3D9_DESCRIPTOR_SIZE = 1;

rdcarray<Descriptor> D3D9Replay::GetDescriptors(ResourceId descriptorStore,
                                                const rdcarray<DescriptorRange> &ranges)
{
  const D3D9RenderState &rs = m_pDevice->GetRenderState();
  rdcarray<Descriptor> ret;

  for(const DescriptorRange &range : ranges)
  {
    for(uint32_t i = 0; i < range.count; i++)
    {
      uint32_t slot = range.offset + i * range.descriptorSize;
      Descriptor desc;
      desc.type = DescriptorType::ImageSampler;

      if(slot < D3D9_TOTAL_SAMPLERS)
      {
        desc.resource = rs.textures[slot];
      }

      ret.push_back(desc);
    }
  }

  return ret;
}

rdcarray<SamplerDescriptor> D3D9Replay::GetSamplerDescriptors(
    ResourceId descriptorStore, const rdcarray<DescriptorRange> &ranges)
{
  rdcarray<SamplerDescriptor> ret;

  for(const DescriptorRange &range : ranges)
  {
    for(uint32_t i = 0; i < range.count; i++)
    {
      SamplerDescriptor samp;
      ret.push_back(samp);
    }
  }

  return ret;
}

rdcarray<DescriptorAccess> D3D9Replay::GetDescriptorAccess(uint32_t eventId)
{
  const D3D9RenderState &rs = m_pDevice->GetRenderState();
  rdcarray<DescriptorAccess> ret;

  // Use the device ResourceId as the virtual descriptor store
  ResourceId descStore = m_pDevice->GetResourceID();

  // For each shader stage (VS and PS), look at the reflection's readOnlyResources
  // to find which sampler registers are used, and create descriptor accesses for them.
  struct StageInfo
  {
    ShaderStage stage;
    ResourceId shaderId;
  };

  StageInfo stages[] = {
      {ShaderStage::Vertex, rs.vertexShader},
      {ShaderStage::Pixel, rs.pixelShader},
  };

  for(const StageInfo &si : stages)
  {
    ShaderReflection *refl = GetShaderReflection(si.shaderId);
    if(!refl)
      continue;

    for(int i = 0; i < refl->readOnlyResources.count(); i++)
    {
      const ShaderResource &res = refl->readOnlyResources[i];

      uint32_t samplerSlot = res.fixedBindNumber;

      // For vertex shader samplers, D3D9 uses slots D3DVERTEXTEXTURESAMPLER0..3 = 257..260
      // which map to our internal sampler indices 16..19
      if(si.stage == ShaderStage::Vertex && samplerSlot < 4)
        samplerSlot += D3D9_MAX_SAMPLERS;    // offset to VS sampler range

      DescriptorAccess acc;
      acc.stage = si.stage;
      acc.type = DescriptorType::ImageSampler;
      acc.index = (uint16_t)i;
      acc.arrayElement = 0;
      acc.descriptorStore = descStore;
      acc.byteOffset = samplerSlot;    // slot index as byte offset
      acc.byteSize = D3D9_DESCRIPTOR_SIZE;
      acc.staticallyUnused = false;
      ret.push_back(acc);
    }
  }

  // FFP texture stage bindings: emit descriptor accesses for texture stages
  // that have a bound texture but weren't already covered by shader reflection.
  // This is essential for FFP draw calls which have no pixel shader reflection.
  {
    // Track which sampler slots we've already emitted from shader reflection
    rdcarray<bool> coveredSlots;
    coveredSlots.resize(D3D9_TOTAL_SAMPLERS);
    for(int i = 0; i < coveredSlots.count(); i++)
      coveredSlots[i] = false;
    for(int i = 0; i < ret.count(); i++)
      if(ret[i].byteOffset < D3D9_TOTAL_SAMPLERS)
        coveredSlots[ret[i].byteOffset] = true;

    // D3D9 has up to 8 texture stages (D3D9_MAX_TEXTURE_STAGES = 8)
    // which map to sampler slots 0..7
    for(UINT i = 0; i < D3D9_MAX_TEXTURE_STAGES; i++)
    {
      if(rs.textures[i] != ResourceId() && !coveredSlots[i])
      {
        DescriptorAccess acc;
        acc.stage = ShaderStage::Pixel;    // FFP textures bind at the pixel stage
        acc.type = DescriptorType::ImageSampler;
        acc.index = (uint16_t)ret.count();    // unique index
        acc.arrayElement = 0;
        acc.descriptorStore = descStore;
        acc.byteOffset = i;    // sampler slot index
        acc.byteSize = D3D9_DESCRIPTOR_SIZE;
        acc.staticallyUnused = false;
        ret.push_back(acc);
      }
    }
  }

  return ret;
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

    IDirect3DSurface9 *srcSurf = NULL;
    tex2d->GetSurfaceLevel(sub.mip, &srcSurf);

    if(srcSurf)
    {
      bool gotData = false;

      // Strategy 1: For render targets, use GetRenderTargetData via staging surface
      IDirect3DSurface9 *realSrcSurf = (IDirect3DSurface9 *)UnwrapD3D9Resource(srcSurf);
      IDirect3DSurface9 *staging = NULL;
      HRESULT hr = m_pDevice->GetReal()->CreateOffscreenPlainSurface(
          desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL);

      if(SUCCEEDED(hr) && staging)
      {
        hr = m_pDevice->GetReal()->GetRenderTargetData(realSrcSurf, staging);

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
            gotData = true;
          }
        }

        SAFE_RELEASE(staging);
      }

      // Strategy 2: Direct LockRect (works for MANAGED pool textures and when
      // CreateOffscreenPlainSurface fails for compressed formats)
      if(!gotData)
      {
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
        }
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
      bool gotData = false;

      // Strategy 1: For render targets, use GetRenderTargetData via staging surface
      IDirect3DSurface9 *realSrcSurf = (IDirect3DSurface9 *)UnwrapD3D9Resource(srcSurf);
      IDirect3DSurface9 *staging = NULL;
      HRESULT hr = m_pDevice->GetReal()->CreateOffscreenPlainSurface(
          desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL);

      if(SUCCEEDED(hr) && staging)
      {
        hr = m_pDevice->GetReal()->GetRenderTargetData(realSrcSurf, staging);
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
            gotData = true;
          }
        }
        SAFE_RELEASE(staging);
      }

      // Strategy 2: Direct LockRect (works for MANAGED pool textures)
      if(!gotData)
      {
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
          gotData = true;
        }
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

    bool gotData = false;

    // Strategy 1: For render targets, use GetRenderTargetData via staging surface
    IDirect3DSurface9 *realSurf = (IDirect3DSurface9 *)UnwrapD3D9Resource(surf);
    IDirect3DSurface9 *staging = NULL;
    HRESULT hr = m_pDevice->GetReal()->CreateOffscreenPlainSurface(
        desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, NULL);

    if(SUCCEEDED(hr) && staging)
    {
      hr = m_pDevice->GetReal()->GetRenderTargetData(realSurf, staging);
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
          gotData = true;
        }
      }
      SAFE_RELEASE(staging);
    }

    // Strategy 2: Direct LockRect fallback
    if(!gotData)
    {
      D3DLOCKED_RECT locked;
      hr = surf->LockRect(&locked, NULL, D3DLOCK_READONLY);
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
        surf->UnlockRect();
        gotData = true;
      }
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
  m_pDevice->GetResourceManager()->ReplaceResource(from, to);
}

void D3D9Replay::RemoveReplacement(ResourceId id)
{
  m_pDevice->GetResourceManager()->RemoveReplacement(id);
}

void D3D9Replay::FreeTargetResource(ResourceId id)
{
  if(m_pDevice->GetResourceManager()->HasResource(id))
  {
    IUnknown *resource = m_pDevice->GetResourceManager()->GetResource(id);
    SAFE_RELEASE(resource);
  }
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
  TextureDescription texDesc = GetTexture(texid);
  if(texDesc.format.type == ResourceFormatType::Undefined)
    return false;

  // Get raw pixel data for the requested subresource
  bytebuf data;
  GetTextureDataParams params = {};
  GetTextureData(texid, sub, params, data);
  if(data.empty())
    return false;

  ResourceFormat fmt = texDesc.format;
  // If typeCast is specified, override the component type
  if(typeCast != CompType::Typeless)
    fmt.compType = typeCast;

  // For compressed formats (BC1/2/3), we can't easily iterate individual pixels
  // on the CPU without decompression. Return false for now.
  if(fmt.type != ResourceFormatType::Regular && fmt.type != ResourceFormatType::R5G6B5 &&
     fmt.type != ResourceFormatType::D24S8)
    return false;

  uint32_t pixelStride = fmt.ElementSize();
  if(pixelStride == 0)
    return false;

  uint32_t mipWidth = RDCMAX(1U, texDesc.width >> sub.mip);
  uint32_t mipHeight = RDCMAX(1U, texDesc.height >> sub.mip);
  uint32_t pixelCount = mipWidth * mipHeight;

  if(data.size() < pixelCount * pixelStride)
    pixelCount = (uint32_t)(data.size() / pixelStride);

  if(pixelCount == 0)
    return false;

  // Initialize min/max to extreme values
  float curMin[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
  float curMax[4] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};

  for(uint32_t i = 0; i < pixelCount; i++)
  {
    FloatVector v = DecodeFormattedComponents(fmt, data.data() + i * pixelStride);

    curMin[0] = RDCMIN(curMin[0], v.x);
    curMin[1] = RDCMIN(curMin[1], v.y);
    curMin[2] = RDCMIN(curMin[2], v.z);
    curMin[3] = RDCMIN(curMin[3], v.w);

    curMax[0] = RDCMAX(curMax[0], v.x);
    curMax[1] = RDCMAX(curMax[1], v.y);
    curMax[2] = RDCMAX(curMax[2], v.z);
    curMax[3] = RDCMAX(curMax[3], v.w);
  }

  memcpy(minval, curMin, sizeof(curMin));
  memcpy(maxval, curMax, sizeof(curMax));

  return true;
}

bool D3D9Replay::GetHistogram(ResourceId texid, const Subresource &sub, CompType typeCast,
                              float minval, float maxval, const rdcfixedarray<bool, 4> &channels,
                              rdcarray<uint32_t> &histogram)
{
  if(minval >= maxval)
    return false;

  TextureDescription texDesc = GetTexture(texid);
  if(texDesc.format.type == ResourceFormatType::Undefined)
    return false;

  bytebuf data;
  GetTextureDataParams params = {};
  GetTextureData(texid, sub, params, data);
  if(data.empty())
    return false;

  ResourceFormat fmt = texDesc.format;
  if(typeCast != CompType::Typeless)
    fmt.compType = typeCast;

  // Can't do compressed formats without decompression
  if(fmt.type != ResourceFormatType::Regular && fmt.type != ResourceFormatType::R5G6B5 &&
     fmt.type != ResourceFormatType::D24S8)
    return false;

  uint32_t pixelStride = fmt.ElementSize();
  if(pixelStride == 0)
    return false;

  uint32_t mipWidth = RDCMAX(1U, texDesc.width >> sub.mip);
  uint32_t mipHeight = RDCMAX(1U, texDesc.height >> sub.mip);
  uint32_t pixelCount = mipWidth * mipHeight;

  if(data.size() < pixelCount * pixelStride)
    pixelCount = (uint32_t)(data.size() / pixelStride);

  if(pixelCount == 0)
    return false;

  const int NUM_BUCKETS = 256;
  histogram.resize(NUM_BUCKETS);
  memset(histogram.data(), 0, sizeof(uint32_t) * NUM_BUCKETS);

  // Add a small delta to maxval so that exactly-maxval values go in the last bucket
  float maxWithDelta = maxval + maxval * 1e-6f;

  for(uint32_t i = 0; i < pixelCount; i++)
  {
    FloatVector v = DecodeFormattedComponents(fmt, data.data() + i * pixelStride);

    float vals[4] = {v.x, v.y, v.z, v.w};

    for(int c = 0; c < 4; c++)
    {
      if(!channels[c])
        continue;

      float normalized = (vals[c] - minval) / (maxWithDelta - minval);
      int bucket = (int)(normalized * NUM_BUCKETS);
      bucket = RDCCLAMP(bucket, 0, NUM_BUCKETS - 1);
      histogram[bucket]++;
    }
  }

  return true;
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
  if(cfg.position.vertexResourceId == ResourceId() || cfg.position.numIndices == 0)
    return;

  IDirect3DDevice9 *dev = m_pDevice->GetReal();

  // Save all device state
  IDirect3DStateBlock9 *savedState = NULL;
  dev->CreateStateBlock(D3DSBT_ALL, &savedState);

  // Get view/projection matrices from the camera
  float nearPlane = cfg.cam ? ((Camera *)cfg.cam)->GetNear() : 0.1f;
  float farPlane = cfg.cam ? ((Camera *)cfg.cam)->GetFar() : 100000.0f;

  // Get output window dimensions for aspect ratio from the currently bound render target
  IDirect3DSurface9 *curRT = NULL;
  dev->GetRenderTarget(0, &curRT);
  float outputW = 1.0f, outputH = 1.0f;
  if(curRT)
  {
    D3DSURFACE_DESC rtDesc;
    curRT->GetDesc(&rtDesc);
    outputW = (float)rtDesc.Width;
    outputH = (float)rtDesc.Height;
    curRT->Release();
  }

  Matrix4f projMat = Matrix4f::Perspective(90.0f, nearPlane, farPlane, outputW / outputH);
  Matrix4f camMat = cfg.cam ? ((Camera *)cfg.cam)->GetMatrix() : Matrix4f::Identity();
  Matrix4f axisMapMat = Matrix4f(cfg.axisMapping);

  Matrix4f mvp = projMat.Mul(camMat.Mul(axisMapMat));

  if(cfg.position.unproject)
  {
    Matrix4f guessProj =
        cfg.position.farPlane != FLT_MAX
            ? Matrix4f::Perspective(cfg.fov, cfg.position.nearPlane, cfg.position.farPlane,
                                    cfg.aspect)
            : Matrix4f::ReversePerspective(cfg.fov, cfg.position.nearPlane, cfg.aspect);

    if(cfg.ortho)
      guessProj = Matrix4f::Orthographic(cfg.position.nearPlane, cfg.position.farPlane);

    if(cfg.position.flipY)
      guessProj[5] *= -1.0f;

    Matrix4f guessProjInv = guessProj.Inverse();
    mvp = projMat.Mul(camMat.Mul(guessProjInv));
  }

  // Fetch vertex data
  bytebuf vbData;
  GetBufferData(cfg.position.vertexResourceId, 0, 0, vbData);
  if(vbData.empty())
  {
    if(savedState)
    {
      savedState->Apply();
      savedState->Release();
    }
    return;
  }

  // Fetch index data if indexed
  bytebuf ibData;
  bool indexed = (cfg.position.indexResourceId != ResourceId() && cfg.position.indexByteStride > 0);
  if(indexed)
    GetBufferData(cfg.position.indexResourceId, 0, 0, ibData);

  // Build a vertex array with transformed positions.
  // Read raw position data and transform through MVP on CPU,
  // then emit pre-transformed (XYZRHW) vertices for D3D9 FFP drawing.
  struct MeshVertex
  {
    float x, y, z, w;
    DWORD color;
  };

  uint32_t numVerts = cfg.position.numIndices;
  rdcarray<MeshVertex> verts;
  verts.resize(numVerts);

  const ResourceFormat &posFmt = cfg.position.format;
  uint32_t posStride = cfg.position.vertexByteStride;
  uint64_t posOffset = cfg.position.vertexByteOffset;

  for(uint32_t i = 0; i < numVerts; i++)
  {
    uint32_t vertIdx = i;

    // Look up actual vertex index if indexed
    if(indexed && !ibData.empty())
    {
      uint64_t ibOff = cfg.position.indexByteOffset + (uint64_t)i * cfg.position.indexByteStride;
      if(ibOff + cfg.position.indexByteStride <= ibData.size())
      {
        if(cfg.position.indexByteStride == 2)
          vertIdx = *(uint16_t *)(ibData.data() + ibOff);
        else
          vertIdx = *(uint32_t *)(ibData.data() + ibOff);

        vertIdx += cfg.position.baseVertex;
      }
    }

    // Read position from vertex buffer
    uint64_t vbOff = posOffset + (uint64_t)vertIdx * posStride;
    float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if(vbOff + posFmt.ElementSize() <= vbData.size())
    {
      FloatVector decoded = DecodeFormattedComponents(posFmt, vbData.data() + vbOff);
      pos[0] = decoded.x;
      pos[1] = decoded.y;
      pos[2] = decoded.z;
      if(posFmt.compCount >= 4)
        pos[3] = decoded.w;
    }

    // Transform through MVP
    float out[4];
    out[0] = mvp[0] * pos[0] + mvp[4] * pos[1] + mvp[8] * pos[2] + mvp[12] * pos[3];
    out[1] = mvp[1] * pos[0] + mvp[5] * pos[1] + mvp[9] * pos[2] + mvp[13] * pos[3];
    out[2] = mvp[2] * pos[0] + mvp[6] * pos[1] + mvp[10] * pos[2] + mvp[14] * pos[3];
    out[3] = mvp[3] * pos[0] + mvp[7] * pos[1] + mvp[11] * pos[2] + mvp[15] * pos[3];

    // Convert from clip space to D3D9 screen space (pre-transformed)
    float invW = (out[3] != 0.0f) ? (1.0f / out[3]) : 1.0f;
    float sx = (out[0] * invW * 0.5f + 0.5f) * outputW;
    float sy = (1.0f - (out[1] * invW * 0.5f + 0.5f)) * outputH;
    float sz = out[2] * invW;

    verts[i].x = sx - 0.5f;
    verts[i].y = sy - 0.5f;
    verts[i].z = sz;
    verts[i].w = 1.0f;
    verts[i].color = 0xFFE0E000;    // yellowish for solid
  }

  // Set up FFP render state
  dev->SetVertexShader(NULL);
  dev->SetPixelShader(NULL);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  dev->SetTexture(0, NULL);

  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
  dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
  dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
  dev->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);
  dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                      D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                          D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
  dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

  // Map RenderDoc topology to D3D9 primitive type
  D3DPRIMITIVETYPE primType = D3DPT_TRIANGLELIST;
  uint32_t primCount = 0;
  switch(cfg.position.topology)
  {
    case Topology::PointList:
      primType = D3DPT_POINTLIST;
      primCount = numVerts;
      break;
    case Topology::LineList:
      primType = D3DPT_LINELIST;
      primCount = numVerts / 2;
      break;
    case Topology::LineStrip:
      primType = D3DPT_LINESTRIP;
      primCount = numVerts > 1 ? numVerts - 1 : 0;
      break;
    case Topology::TriangleList:
      primType = D3DPT_TRIANGLELIST;
      primCount = numVerts / 3;
      break;
    case Topology::TriangleStrip:
      primType = D3DPT_TRIANGLESTRIP;
      primCount = numVerts > 2 ? numVerts - 2 : 0;
      break;
    case Topology::TriangleFan:
      primType = D3DPT_TRIANGLEFAN;
      primCount = numVerts > 2 ? numVerts - 2 : 0;
      break;
    default:
      primType = D3DPT_TRIANGLELIST;
      primCount = numVerts / 3;
      break;
  }

  if(primCount == 0 || verts.empty())
  {
    if(savedState)
    {
      savedState->Apply();
      savedState->Release();
    }
    return;
  }

  // Draw solid if requested
  if(cfg.visualisationMode != Visualisation::NoSolid &&
     (primType == D3DPT_TRIANGLELIST || primType == D3DPT_TRIANGLESTRIP ||
      primType == D3DPT_TRIANGLEFAN))
  {
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->DrawPrimitiveUP(primType, primCount, verts.data(), sizeof(MeshVertex));
  }

  // Draw wireframe overlay
  if(cfg.wireframeDraw && primCount > 0 &&
     (primType == D3DPT_TRIANGLELIST || primType == D3DPT_TRIANGLESTRIP ||
      primType == D3DPT_TRIANGLEFAN))
  {
    // Set wireframe color (green)
    for(uint32_t i = 0; i < numVerts; i++)
      verts[i].color = 0xFF00FF00;

    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    // Bias depth slightly so wireframe draws on top of solid
    const float depthBias = -0.00001f;
    dev->SetRenderState(D3DRS_DEPTHBIAS, *(DWORD *)&depthBias);
    dev->DrawPrimitiveUP(primType, primCount, verts.data(), sizeof(MeshVertex));
    const float zero = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS, *(DWORD *)&zero);
  }
  else if(primType == D3DPT_LINELIST || primType == D3DPT_LINESTRIP ||
          primType == D3DPT_POINTLIST)
  {
    // For line/point primitives, just draw them directly
    for(uint32_t i = 0; i < numVerts; i++)
      verts[i].color = 0xFF00FF00;

    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    dev->DrawPrimitiveUP(primType, primCount, verts.data(), sizeof(MeshVertex));
  }

  // Restore state
  if(savedState)
  {
    savedState->Apply();
    savedState->Release();
  }
}

bool D3D9Replay::RenderTexture(TextureDisplay cfg)
{
  if(cfg.resourceId == ResourceId())
    return false;

  D3D9ResourceManager *rm = m_pDevice->GetResourceManager();
  IUnknown *res = rm->GetResource(cfg.resourceId, true);
  if(!res)
    return false;

  // Unwrap the resource to get the real D3D9 object for rendering
  IUnknown *realRes = UnwrapD3D9Resource(res);
  if(!realRes)
    return false;

  // Try to get a base texture from the real resource
  IDirect3DBaseTexture9 *realTex = NULL;
  IDirect3DTexture9 *tex2d = NULL;
  IDirect3DCubeTexture9 *texCube = NULL;

  float texW = 1.0f, texH = 1.0f;

  if(SUCCEEDED(realRes->QueryInterface(__uuidof(IDirect3DTexture9), (void **)&tex2d)) && tex2d)
  {
    D3DSURFACE_DESC desc;
    tex2d->GetLevelDesc(cfg.subresource.mip, &desc);
    texW = (float)desc.Width;
    texH = (float)desc.Height;
    realTex = tex2d;
  }
  else if(SUCCEEDED(realRes->QueryInterface(__uuidof(IDirect3DCubeTexture9), (void **)&texCube)) &&
          texCube)
  {
    D3DSURFACE_DESC desc;
    texCube->GetLevelDesc(cfg.subresource.mip, &desc);
    texW = (float)desc.Width;
    texH = (float)desc.Height;
    realTex = texCube;
  }

  if(!realTex)
    return false;

  IDirect3DDevice9 *dev = m_pDevice->GetReal();

  // Save the current state so we can restore it later
  IDirect3DStateBlock9 *savedState = NULL;
  dev->CreateStateBlock(D3DSBT_ALL, &savedState);

  // Set up a simple fullscreen quad using transformed vertices (pretransformed, no vertex shader)
  struct Vertex
  {
    float x, y, z, w;
    float u, v;
  };

  float left = cfg.xOffset;
  float top = cfg.yOffset;
  float right = left + texW * cfg.scale;
  float bottom = top + texH * cfg.scale;

  // Pre-transformed vertices (RHW = 1.0, already in screen space)
  Vertex quad[4] = {
      {left - 0.5f, top - 0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
      {right - 0.5f, top - 0.5f, 0.0f, 1.0f, 1.0f, 0.0f},
      {left - 0.5f, bottom - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f},
      {right - 0.5f, bottom - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f},
  };

  // Set up render state for a simple textured quad
  dev->SetTexture(0, realTex);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
  dev->SetVertexShader(NULL);
  dev->SetPixelShader(NULL);

  // Disable lighting, alpha blending, depth test, etc.
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
  dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  dev->SetRenderState(D3DRS_CLIPPING, FALSE);
  dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
  dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                      D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                          D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

  // Simple texture sampling
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

  dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

  // Draw the quad
  dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));

  // Clean up
  dev->SetTexture(0, NULL);
  SAFE_RELEASE(realTex);

  // Restore state
  if(savedState)
  {
    savedState->Apply();
    savedState->Release();
  }

  return true;
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
  IDirect3DDevice9 *dev = m_pDevice->GetReal();

  // Just clear to the dark color as a simple background.
  // A proper checkerboard would require creating a small checkerboard texture.
  D3DCOLOR col =
      D3DCOLOR_COLORVALUE(dark.x * 0.5f + light.x * 0.5f, dark.y * 0.5f + light.y * 0.5f,
                          dark.z * 0.5f + light.z * 0.5f, 1.0f);
  dev->Clear(0, NULL, D3DCLEAR_TARGET, col, 1.0f, 0);
}

void D3D9Replay::RenderHighlightBox(float w, float h, float scale)
{
  IDirect3DDevice9 *dev = m_pDevice->GetReal();

  // Save state block
  IDirect3DStateBlock9 *savedState = NULL;
  dev->CreateStateBlock(D3DSBT_ALL, &savedState);

  // Set up fixed-function render state for drawing colored quads
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
  dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                      D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                          D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
  dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  dev->SetTexture(0, NULL);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  dev->SetVertexShader(NULL);
  dev->SetPixelShader(NULL);

  // Pre-transformed vertex: covers the entire viewport as a fullscreen quad
  // Using a triangle strip with 4 vertices
  // x, y, z, rhw — z=0, rhw=1 for pre-transformed
  struct HighlightVtx
  {
    float x, y, z, rhw;
    DWORD color;
  };

  // Full-viewport quad vertices (will be scissor-clipped to the border rects)
  auto DrawFullscreenQuad = [&](DWORD color) {
    HighlightVtx verts[4] = {
        {0.0f, 0.0f, 0.0f, 1.0f, color},
        {w, 0.0f, 0.0f, 1.0f, color},
        {0.0f, h, 0.0f, 1.0f, color},
        {w, h, 0.0f, 1.0f, color},
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(HighlightVtx));
  };

  // The highlight box occupies the center of the viewport.
  // Same math as D3D11: top-left at (w/2, h/2), size = scale pixels.
  LONG sz = LONG(scale);
  LONG tlx = LONG(w / 2.0f + 0.5f);
  LONG tly = LONG(h / 2.0f + 0.5f);

  // 4 border rects: left, right, top, bottom (same order as D3D11)
  RECT rect[4] = {
      {tlx, tly, tlx + 1, tly + sz},          // left border
      {tlx + sz, tly, tlx + sz + 1, tly + sz + 1},  // right border
      {tlx, tly, tlx + sz, tly + 1},           // top border
      {tlx, tly + sz, tlx + sz, tly + sz + 1},  // bottom border
  };

  // Draw white inner border
  for(int i = 0; i < 4; i++)
  {
    dev->SetScissorRect(&rect[i]);
    DrawFullscreenQuad(0xFFFFFFFF);
  }

  // Expand rects outward by 1px for the black outer border (matching D3D11 logic)
  rect[0].left--;
  rect[0].right--;
  rect[1].left++;
  rect[1].right++;
  rect[2].left--;
  rect[2].right--;
  rect[3].left--;
  rect[3].right--;

  rect[0].top--;
  rect[0].bottom--;
  rect[1].top--;
  rect[1].bottom--;
  rect[2].top--;
  rect[2].bottom--;
  rect[3].top++;
  rect[3].bottom++;

  rect[0].bottom += 2;
  rect[1].bottom += 2;
  rect[2].right += 2;
  rect[3].right += 2;

  // Draw black outer border
  for(int i = 0; i < 4; i++)
  {
    dev->SetScissorRect(&rect[i]);
    DrawFullscreenQuad(0xFF000000);
  }

  // Restore render state
  if(savedState)
  {
    savedState->Apply();
    savedState->Release();
  }
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
