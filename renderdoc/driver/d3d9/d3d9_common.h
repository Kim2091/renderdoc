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

#include "api/replay/rdcstr.h"
#include "api/replay/resourceid.h"
#include "common/common.h"
#include "core/core.h"
#include "driver/dx/official/d3d9.h"
#include "serialise/serialiser.h"

#include "d3d9_chunks.h"

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
