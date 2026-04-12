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

#include "d3d9_common.h"

uint32_t GetD3D9FormatByteSize(D3DFORMAT fmt)
{
  switch(fmt)
  {
    case D3DFMT_R8G8B8: return 3;
    case D3DFMT_A8R8G8B8: return 4;
    case D3DFMT_X8R8G8B8: return 4;
    case D3DFMT_R5G6B5: return 2;
    case D3DFMT_X1R5G5B5: return 2;
    case D3DFMT_A1R5G5B5: return 2;
    case D3DFMT_A4R4G4B4: return 2;
    case D3DFMT_R3G3B2: return 1;
    case D3DFMT_A8: return 1;
    case D3DFMT_A8R3G3B2: return 2;
    case D3DFMT_X4R4G4B4: return 2;
    case D3DFMT_A2B10G10R10: return 4;
    case D3DFMT_A8B8G8R8: return 4;
    case D3DFMT_X8B8G8R8: return 4;
    case D3DFMT_G16R16: return 4;
    case D3DFMT_A2R10G10B10: return 4;
    case D3DFMT_A16B16G16R16: return 8;
    case D3DFMT_A8P8: return 2;
    case D3DFMT_P8: return 1;
    case D3DFMT_L8: return 1;
    case D3DFMT_A8L8: return 2;
    case D3DFMT_A4L4: return 1;
    case D3DFMT_V8U8: return 2;
    case D3DFMT_L6V5U5: return 2;
    case D3DFMT_X8L8V8U8: return 4;
    case D3DFMT_Q8W8V8U8: return 4;
    case D3DFMT_V16U16: return 4;
    case D3DFMT_A2W10V10U10: return 4;
    case D3DFMT_D16_LOCKABLE: return 2;
    case D3DFMT_D32: return 4;
    case D3DFMT_D15S1: return 2;
    case D3DFMT_D24S8: return 4;
    case D3DFMT_D24X8: return 4;
    case D3DFMT_D24X4S4: return 4;
    case D3DFMT_D16: return 2;
    case D3DFMT_D32F_LOCKABLE: return 4;
    case D3DFMT_D24FS8: return 4;
    case D3DFMT_L16: return 2;
    case D3DFMT_INDEX16: return 2;
    case D3DFMT_INDEX32: return 4;
    case D3DFMT_Q16W16V16U16: return 8;
    case D3DFMT_R16F: return 2;
    case D3DFMT_G16R16F: return 4;
    case D3DFMT_A16B16G16R16F: return 8;
    case D3DFMT_R32F: return 4;
    case D3DFMT_G32R32F: return 8;
    case D3DFMT_A32B32G32R32F: return 16;
    case D3DFMT_CxV8U8: return 2;
    // DXT compressed: return bytes per block (4x4 pixels)
    case D3DFMT_DXT1: return 8;
    case D3DFMT_DXT2: return 16;
    case D3DFMT_DXT3: return 16;
    case D3DFMT_DXT4: return 16;
    case D3DFMT_DXT5: return 16;
    default:
      RDCWARN("Unrecognised D3D9 format %u", (uint32_t)fmt);
      return 4;
  }
}

bool IsD3D9FormatCompressed(D3DFORMAT fmt)
{
  switch(fmt)
  {
    case D3DFMT_DXT1:
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: return true;
    default: return false;
  }
}

uint32_t GetD3D9FormatBlockSize(D3DFORMAT fmt)
{
  if(IsD3D9FormatCompressed(fmt))
    return 4;
  return 1;
}

uint32_t GetD3D9SurfaceByteSize(D3DFORMAT fmt, UINT width, UINT height)
{
  if(IsD3D9FormatCompressed(fmt))
  {
    UINT blockW = (width + 3) / 4;
    UINT blockH = (height + 3) / 4;
    return blockW * blockH * GetD3D9FormatByteSize(fmt);
  }
  return width * height * GetD3D9FormatByteSize(fmt);
}

ResourceFormat MakeResourceFormat(D3DFORMAT fmt)
{
  ResourceFormat ret;

  ret.type = ResourceFormatType::Regular;

  switch(fmt)
  {
    case D3DFMT_A8R8G8B8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 4;
      ret.SetBGRAOrder(true);
      break;
    case D3DFMT_X8R8G8B8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 4;
      ret.SetBGRAOrder(true);
      break;
    case D3DFMT_A8B8G8R8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 4;
      break;
    case D3DFMT_X8B8G8R8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 4;
      break;
    case D3DFMT_R5G6B5:
      ret.type = ResourceFormatType::R5G6B5;
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 0;
      ret.compCount = 3;
      break;
    case D3DFMT_A16B16G16R16:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 2;
      ret.compCount = 4;
      break;
    case D3DFMT_A16B16G16R16F:
      ret.compType = CompType::Float;
      ret.compByteWidth = 2;
      ret.compCount = 4;
      break;
    case D3DFMT_A32B32G32R32F:
      ret.compType = CompType::Float;
      ret.compByteWidth = 4;
      ret.compCount = 4;
      break;
    case D3DFMT_R32F:
      ret.compType = CompType::Float;
      ret.compByteWidth = 4;
      ret.compCount = 1;
      break;
    case D3DFMT_G32R32F:
      ret.compType = CompType::Float;
      ret.compByteWidth = 4;
      ret.compCount = 2;
      break;
    case D3DFMT_R16F:
      ret.compType = CompType::Float;
      ret.compByteWidth = 2;
      ret.compCount = 1;
      break;
    case D3DFMT_G16R16F:
      ret.compType = CompType::Float;
      ret.compByteWidth = 2;
      ret.compCount = 2;
      break;
    case D3DFMT_G16R16:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 2;
      ret.compCount = 2;
      break;
    case D3DFMT_A8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 1;
      break;
    case D3DFMT_L8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 1;
      break;
    case D3DFMT_A8L8:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 2;
      break;
    case D3DFMT_L16:
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 2;
      ret.compCount = 1;
      break;
    case D3DFMT_DXT1:
      ret.type = ResourceFormatType::BC1;
      ret.compType = CompType::UNorm;
      ret.compCount = 4;
      break;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
      ret.type = ResourceFormatType::BC2;
      ret.compType = CompType::UNorm;
      ret.compCount = 4;
      break;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
      ret.type = ResourceFormatType::BC3;
      ret.compType = CompType::UNorm;
      ret.compCount = 4;
      break;
    case D3DFMT_V8U8:
      ret.compType = CompType::SNorm;
      ret.compByteWidth = 1;
      ret.compCount = 2;
      break;
    case D3DFMT_Q8W8V8U8:
      ret.compType = CompType::SNorm;
      ret.compByteWidth = 1;
      ret.compCount = 4;
      break;
    case D3DFMT_V16U16:
      ret.compType = CompType::SNorm;
      ret.compByteWidth = 2;
      ret.compCount = 2;
      break;
    case D3DFMT_Q16W16V16U16:
      ret.compType = CompType::SNorm;
      ret.compByteWidth = 2;
      ret.compCount = 4;
      break;
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
      ret.compType = CompType::Depth;
      ret.compByteWidth = 2;
      ret.compCount = 1;
      break;
    case D3DFMT_D32:
    case D3DFMT_D32F_LOCKABLE:
      ret.compType = CompType::Depth;
      ret.compByteWidth = 4;
      ret.compCount = 1;
      break;
    case D3DFMT_D24S8:
    case D3DFMT_D24FS8:
      ret.type = ResourceFormatType::D24S8;
      ret.compType = CompType::Depth;
      ret.compCount = 2;
      break;
    case D3DFMT_D24X8:
      ret.compType = CompType::Depth;
      ret.compByteWidth = 4;
      ret.compCount = 1;
      break;
    default:
      RDCWARN("Unsupported D3D9 format %u for ResourceFormat conversion", (uint32_t)fmt);
      ret.compType = CompType::UNorm;
      ret.compByteWidth = 1;
      ret.compCount = 4;
      break;
  }

  return ret;
}

D3DFORMAT MakeD3DFormat(ResourceFormat fmt)
{
  if(fmt.type == ResourceFormatType::BC1)
    return D3DFMT_DXT1;
  if(fmt.type == ResourceFormatType::BC2)
    return D3DFMT_DXT3;
  if(fmt.type == ResourceFormatType::BC3)
    return D3DFMT_DXT5;
  if(fmt.type == ResourceFormatType::D24S8)
    return D3DFMT_D24S8;
  if(fmt.type == ResourceFormatType::R5G6B5)
    return D3DFMT_R5G6B5;

  if(fmt.compType == CompType::Float)
  {
    if(fmt.compByteWidth == 4 && fmt.compCount == 4)
      return D3DFMT_A32B32G32R32F;
    if(fmt.compByteWidth == 4 && fmt.compCount == 2)
      return D3DFMT_G32R32F;
    if(fmt.compByteWidth == 4 && fmt.compCount == 1)
      return D3DFMT_R32F;
    if(fmt.compByteWidth == 2 && fmt.compCount == 4)
      return D3DFMT_A16B16G16R16F;
    if(fmt.compByteWidth == 2 && fmt.compCount == 2)
      return D3DFMT_G16R16F;
    if(fmt.compByteWidth == 2 && fmt.compCount == 1)
      return D3DFMT_R16F;
  }
  if(fmt.compType == CompType::UNorm)
  {
    if(fmt.compByteWidth == 1 && fmt.compCount == 4)
      return fmt.BGRAOrder() ? D3DFMT_A8R8G8B8 : D3DFMT_A8B8G8R8;
    if(fmt.compByteWidth == 2 && fmt.compCount == 4)
      return D3DFMT_A16B16G16R16;
    if(fmt.compByteWidth == 2 && fmt.compCount == 2)
      return D3DFMT_G16R16;
    if(fmt.compByteWidth == 1 && fmt.compCount == 1)
      return D3DFMT_L8;
    if(fmt.compByteWidth == 1 && fmt.compCount == 2)
      return D3DFMT_A8L8;
  }
  if(fmt.compType == CompType::Depth)
  {
    if(fmt.compByteWidth == 2)
      return D3DFMT_D16;
    if(fmt.compByteWidth == 4)
      return D3DFMT_D32F_LOCKABLE;
  }

  return D3DFMT_A8R8G8B8;
}
