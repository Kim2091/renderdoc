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

#define INITGUID

#include "api/replay/rdcstr.h"
#include "api/replay/resourceid.h"
#include "common/common.h"
#include "core/core.h"
#include "driver/dx/official/d3d9.h"
#include "serialise/serialiser.h"

#include "d3d9_chunks.h"

///////////////////////////////////////////////////////////////////////////
// QI-based type identification for wrapped D3D9 resources (replaces RTTI)
///////////////////////////////////////////////////////////////////////////

// Custom GUID used to identify our wrapped objects via QueryInterface.
// {D3D9EEEE-0000-0000-0000-52454E440001}
static const GUID IID_ID3D9WrappedResource = {
    0xd3d9eeee, 0x0, 0x0, {0x0, 0x0, 0x52, 0x45, 0x4e, 0x44, 0x0, 0x01}};

enum class D3D9WrappedType : uint32_t
{
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
  SwapChain,
};

// Info struct returned by custom QI on our wrappers.
// Callers get a pointer to this struct (no AddRef — it is just info).
struct D3D9WrappedInfo
{
  D3D9WrappedType type;
  ResourceId id;
  IUnknown *realObject;
};

// Forward declarations
class WrappedIDirect3DDevice9;
class WrappedIDirect3D9;
class D3D9ResourceManager;
struct D3D9ResourceRecord;

// Macros mirroring the D3D11 driver's serialization helpers.
// These are defined on WrappedIDirect3DDevice9 which holds m_ScratchSerialiser and m_State.
#define USE_SCRATCH_SERIALISER() WriteSerialiser &ser = m_ScratchSerialiser;

#define SERIALISE_TIME_CALL(...)                                          \
  m_ScratchSerialiser.ChunkMetadata().timestampMicro = Timing::GetTick(); \
  __VA_ARGS__;                                                            \
  m_ScratchSerialiser.ChunkMetadata().durationMicro =                     \
      Timing::GetTick() - m_ScratchSerialiser.ChunkMetadata().timestampMicro;

#define IMPLEMENT_FUNCTION_SERIALISED(ret, func, ...) \
  ret func(__VA_ARGS__);                              \
  template <typename SerialiserType>                  \
  bool CONCAT(Serialise_, func(SerialiserType &ser, __VA_ARGS__));

#define INSTANTIATE_FUNCTION_SERIALISED(ret, parent, func, ...)                     \
  template bool parent::CONCAT(Serialise_, func(ReadSerialiser &ser, __VA_ARGS__)); \
  template bool parent::CONCAT(Serialise_, func(WriteSerialiser &ser, __VA_ARGS__));

// A handy macro to say "is the serialiser reading and we're doing replay-mode stuff?"
// The reason we check both is that checking the first allows the compiler to eliminate the other
// path at compile-time, and the second because we might be just struct-serialising in which case we
// should be doing no work to restore states.
// Writing is unambiguously during capture mode, so we don't have to check both in that case.
#define IsReplayingAndReading() (ser.IsReading() && IsReplayMode(m_State))

// Helper to compute vertex count from primitive type and primitive count
inline UINT D3D9_VertexCount(D3DPRIMITIVETYPE type, UINT primitiveCount)
{
  switch(type)
  {
    case D3DPT_POINTLIST: return primitiveCount;
    case D3DPT_LINELIST: return primitiveCount * 2;
    case D3DPT_LINESTRIP: return primitiveCount + 1;
    case D3DPT_TRIANGLELIST: return primitiveCount * 3;
    case D3DPT_TRIANGLESTRIP: return primitiveCount + 2;
    case D3DPT_TRIANGLEFAN: return primitiveCount + 2;
    default: return 0;
  }
}

// Convert D3DFORMAT to RenderDoc ResourceFormat
ResourceFormat MakeResourceFormat(D3DFORMAT fmt);

// Convert RenderDoc ResourceFormat back to D3DFORMAT
D3DFORMAT MakeD3DFormat(ResourceFormat fmt);

// Get byte size of a D3DFORMAT per pixel (or per block for compressed)
uint32_t GetD3D9FormatByteSize(D3DFORMAT fmt);

// Get byte size for a surface given format, width, height
uint32_t GetD3D9SurfaceByteSize(D3DFORMAT fmt, UINT width, UINT height);

// Check if a D3DFORMAT is a compressed (DXT/BC) format
bool IsD3D9FormatCompressed(D3DFORMAT fmt);

// Get block size for compressed formats (4 for DXT)
uint32_t GetD3D9FormatBlockSize(D3DFORMAT fmt);

// Reflection declarations for D3D9 enums used in serialisation/stringise
DECLARE_REFLECTION_ENUM(D3DFORMAT);
DECLARE_REFLECTION_ENUM(D3DPOOL);
DECLARE_REFLECTION_ENUM(D3DPRIMITIVETYPE);
DECLARE_REFLECTION_ENUM(D3DRENDERSTATETYPE);
DECLARE_REFLECTION_ENUM(D3DTEXTURESTAGESTATETYPE);
DECLARE_REFLECTION_ENUM(D3DSAMPLERSTATETYPE);
DECLARE_REFLECTION_ENUM(D3DTRANSFORMSTATETYPE);
DECLARE_REFLECTION_ENUM(D3DSTATEBLOCKTYPE);
DECLARE_REFLECTION_ENUM(D3DQUERYTYPE);
DECLARE_REFLECTION_ENUM(D3DBLEND);
DECLARE_REFLECTION_ENUM(D3DCMPFUNC);
DECLARE_REFLECTION_ENUM(D3DSTENCILOP);
DECLARE_REFLECTION_ENUM(D3DFILLMODE);
DECLARE_REFLECTION_ENUM(D3DCULL);
DECLARE_REFLECTION_ENUM(D3DBLENDOP);
DECLARE_REFLECTION_ENUM(D3DSWAPEFFECT);
DECLARE_REFLECTION_ENUM(D3DMULTISAMPLE_TYPE);
DECLARE_REFLECTION_ENUM(D3DTEXTUREFILTERTYPE);
DECLARE_REFLECTION_ENUM(D3DDEVTYPE);
DECLARE_REFLECTION_ENUM(D3DLIGHTTYPE);
DECLARE_REFLECTION_ENUM(D3DTEXTUREOP);
DECLARE_REFLECTION_ENUM(D3DRESOURCETYPE);

// TypeName specialisations and DoSerialise forward declarations for DWORD
// (unsigned long) and LONG (long).  MSVC treats these as distinct types from
// uint32_t / int32_t.
template <>
inline rdcliteral TypeName<unsigned long>()
{
  return "DWORD"_lit;
}
template <class SerialiserType>
void DoSerialise(SerialiserType &ser, unsigned long &el);

template <>
inline rdcliteral TypeName<long>()
{
  return "LONG"_lit;
}
template <class SerialiserType>
void DoSerialise(SerialiserType &ser, long &el);

// Reflection declarations for Windows structs used by D3D9 serialisation
DECLARE_REFLECTION_STRUCT(RECT);
DECLARE_REFLECTION_STRUCT(POINT);

// Reflection declarations for D3D9 structs used in serialisation.
// These provide TypeName<> specialisations needed by the serialiser.
DECLARE_REFLECTION_STRUCT(D3DCOLORVALUE);
DECLARE_REFLECTION_STRUCT(D3DVECTOR);
DECLARE_REFLECTION_STRUCT(D3DRECT);
DECLARE_REFLECTION_STRUCT(D3DMATRIX);
DECLARE_REFLECTION_STRUCT(D3DVIEWPORT9);
DECLARE_REFLECTION_STRUCT(D3DLIGHT9);
DECLARE_REFLECTION_STRUCT(D3DMATERIAL9);
DECLARE_REFLECTION_STRUCT(D3DVERTEXELEMENT9);
DECLARE_REFLECTION_STRUCT(D3DPRESENT_PARAMETERS);
DECLARE_REFLECTION_STRUCT(D3DSURFACE_DESC);
DECLARE_REFLECTION_STRUCT(D3DVERTEXBUFFER_DESC);
DECLARE_REFLECTION_STRUCT(D3DINDEXBUFFER_DESC);
DECLARE_REFLECTION_STRUCT(D3DVSHADERCAPS2_0);
DECLARE_REFLECTION_STRUCT(D3DPSHADERCAPS2_0);
DECLARE_REFLECTION_STRUCT(D3DCAPS9);
