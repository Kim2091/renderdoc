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

#include "common/common.h"
#include "serialise/serialiser.h"
#include "d3d9_buffers.h"
#include "d3d9_device.h"
#include "d3d9_manager.h"
#include "d3d9_query.h"
#include "d3d9_resources.h"
#include "d3d9_shaders.h"
#include "d3d9_stateblock.h"
#include "d3d9_swapchain.h"

// serialisation of object handles via IDs.
template <class SerialiserType, class Interface>
void DoSerialiseViaResourceId(SerialiserType &ser, Interface *&el)
{
  D3D9ResourceManager *rm = (D3D9ResourceManager *)ser.GetUserData();

  ResourceId id;

  if(ser.IsWriting() && rm)
    id = GetIDForD3D9Resource(el);

  DoSerialise(ser, id);

  if(ser.IsReading())
  {
    if(id != ResourceId() && rm && rm->HasResource(id))
      el = (Interface *)rm->GetResource(id);
    else
      el = NULL;
  }
}

#define SERIALISE_D3D9_INTERFACE(iface)                \
  template <class SerialiserType>                      \
  void DoSerialise(SerialiserType &ser, iface *&el)    \
  {                                                    \
    DoSerialiseViaResourceId(ser, el);                 \
  }                                                    \
  INSTANTIATE_SERIALISE_TYPE(iface *);

SERIALISE_D3D9_INTERFACE(IDirect3DTexture9);
SERIALISE_D3D9_INTERFACE(IDirect3DCubeTexture9);
SERIALISE_D3D9_INTERFACE(IDirect3DVolumeTexture9);
SERIALISE_D3D9_INTERFACE(IDirect3DSurface9);
SERIALISE_D3D9_INTERFACE(IDirect3DVolume9);
SERIALISE_D3D9_INTERFACE(IDirect3DVertexBuffer9);
SERIALISE_D3D9_INTERFACE(IDirect3DIndexBuffer9);
SERIALISE_D3D9_INTERFACE(IDirect3DVertexShader9);
SERIALISE_D3D9_INTERFACE(IDirect3DPixelShader9);
SERIALISE_D3D9_INTERFACE(IDirect3DVertexDeclaration9);
SERIALISE_D3D9_INTERFACE(IDirect3DStateBlock9);
SERIALISE_D3D9_INTERFACE(IDirect3DSwapChain9);
SERIALISE_D3D9_INTERFACE(IDirect3DQuery9);
SERIALISE_D3D9_INTERFACE(IDirect3DBaseTexture9);

/////////////////////////////////////////////////////////////////////////////
// Struct serialisation
/////////////////////////////////////////////////////////////////////////////

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DCOLORVALUE &el)
{
  SERIALISE_MEMBER(r);
  SERIALISE_MEMBER(g);
  SERIALISE_MEMBER(b);
  SERIALISE_MEMBER(a);
}

INSTANTIATE_SERIALISE_TYPE(D3DCOLORVALUE);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DVECTOR &el)
{
  SERIALISE_MEMBER(x);
  SERIALISE_MEMBER(y);
  SERIALISE_MEMBER(z);
}

INSTANTIATE_SERIALISE_TYPE(D3DVECTOR);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DRECT &el)
{
  SERIALISE_MEMBER(x1);
  SERIALISE_MEMBER(y1);
  SERIALISE_MEMBER(x2);
  SERIALISE_MEMBER(y2);
}

INSTANTIATE_SERIALISE_TYPE(D3DRECT);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DMATRIX &el)
{
  SERIALISE_MEMBER(_11);
  SERIALISE_MEMBER(_12);
  SERIALISE_MEMBER(_13);
  SERIALISE_MEMBER(_14);
  SERIALISE_MEMBER(_21);
  SERIALISE_MEMBER(_22);
  SERIALISE_MEMBER(_23);
  SERIALISE_MEMBER(_24);
  SERIALISE_MEMBER(_31);
  SERIALISE_MEMBER(_32);
  SERIALISE_MEMBER(_33);
  SERIALISE_MEMBER(_34);
  SERIALISE_MEMBER(_41);
  SERIALISE_MEMBER(_42);
  SERIALISE_MEMBER(_43);
  SERIALISE_MEMBER(_44);
}

INSTANTIATE_SERIALISE_TYPE(D3DMATRIX);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DVIEWPORT9 &el)
{
  SERIALISE_MEMBER(X);
  SERIALISE_MEMBER(Y);
  SERIALISE_MEMBER(Width);
  SERIALISE_MEMBER(Height);
  SERIALISE_MEMBER(MinZ);
  SERIALISE_MEMBER(MaxZ);
}

INSTANTIATE_SERIALISE_TYPE(D3DVIEWPORT9);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DLIGHT9 &el)
{
  SERIALISE_MEMBER(Type);
  SERIALISE_MEMBER(Diffuse);
  SERIALISE_MEMBER(Specular);
  SERIALISE_MEMBER(Ambient);
  SERIALISE_MEMBER(Position);
  SERIALISE_MEMBER(Direction);
  SERIALISE_MEMBER(Range);
  SERIALISE_MEMBER(Falloff);
  SERIALISE_MEMBER(Attenuation0);
  SERIALISE_MEMBER(Attenuation1);
  SERIALISE_MEMBER(Attenuation2);
  SERIALISE_MEMBER(Theta);
  SERIALISE_MEMBER(Phi);
}

INSTANTIATE_SERIALISE_TYPE(D3DLIGHT9);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DMATERIAL9 &el)
{
  SERIALISE_MEMBER(Diffuse);
  SERIALISE_MEMBER(Ambient);
  SERIALISE_MEMBER(Specular);
  SERIALISE_MEMBER(Emissive);
  SERIALISE_MEMBER(Power);
}

INSTANTIATE_SERIALISE_TYPE(D3DMATERIAL9);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DVERTEXELEMENT9 &el)
{
  SERIALISE_MEMBER(Stream);
  SERIALISE_MEMBER(Offset);
  SERIALISE_MEMBER(Type);
  SERIALISE_MEMBER(Method);
  SERIALISE_MEMBER(Usage);
  SERIALISE_MEMBER(UsageIndex);
}

INSTANTIATE_SERIALISE_TYPE(D3DVERTEXELEMENT9);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DPRESENT_PARAMETERS &el)
{
  SERIALISE_MEMBER(BackBufferWidth);
  SERIALISE_MEMBER(BackBufferHeight);
  SERIALISE_MEMBER(BackBufferFormat);
  SERIALISE_MEMBER(BackBufferCount);
  SERIALISE_MEMBER(MultiSampleType);
  SERIALISE_MEMBER(MultiSampleQuality);
  SERIALISE_MEMBER(SwapEffect);
  // Skip hDeviceWindow - it's a window handle, not meaningful for replay
  SERIALISE_MEMBER(Windowed);
  SERIALISE_MEMBER(EnableAutoDepthStencil);
  SERIALISE_MEMBER(AutoDepthStencilFormat);
  SERIALISE_MEMBER(Flags);
  SERIALISE_MEMBER(FullScreen_RefreshRateInHz);
  SERIALISE_MEMBER(PresentationInterval);
}

INSTANTIATE_SERIALISE_TYPE(D3DPRESENT_PARAMETERS);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DSURFACE_DESC &el)
{
  SERIALISE_MEMBER(Format);
  SERIALISE_MEMBER(Type);
  SERIALISE_MEMBER(Usage);
  SERIALISE_MEMBER(Pool);
  SERIALISE_MEMBER(MultiSampleType);
  SERIALISE_MEMBER(MultiSampleQuality);
  SERIALISE_MEMBER(Width);
  SERIALISE_MEMBER(Height);
}

INSTANTIATE_SERIALISE_TYPE(D3DSURFACE_DESC);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DVERTEXBUFFER_DESC &el)
{
  SERIALISE_MEMBER(Format);
  SERIALISE_MEMBER(Type);
  SERIALISE_MEMBER(Usage);
  SERIALISE_MEMBER(Pool);
  SERIALISE_MEMBER(Size);
  SERIALISE_MEMBER(FVF);
}

INSTANTIATE_SERIALISE_TYPE(D3DVERTEXBUFFER_DESC);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DINDEXBUFFER_DESC &el)
{
  SERIALISE_MEMBER(Format);
  SERIALISE_MEMBER(Type);
  SERIALISE_MEMBER(Usage);
  SERIALISE_MEMBER(Pool);
  SERIALISE_MEMBER(Size);
}

INSTANTIATE_SERIALISE_TYPE(D3DINDEXBUFFER_DESC);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DVSHADERCAPS2_0 &el)
{
  SERIALISE_MEMBER(Caps);
  SERIALISE_MEMBER(DynamicFlowControlDepth);
  SERIALISE_MEMBER(NumTemps);
  SERIALISE_MEMBER(StaticFlowControlDepth);
}

INSTANTIATE_SERIALISE_TYPE(D3DVSHADERCAPS2_0);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DPSHADERCAPS2_0 &el)
{
  SERIALISE_MEMBER(Caps);
  SERIALISE_MEMBER(DynamicFlowControlDepth);
  SERIALISE_MEMBER(NumTemps);
  SERIALISE_MEMBER(StaticFlowControlDepth);
  SERIALISE_MEMBER(NumInstructionSlots);
}

INSTANTIATE_SERIALISE_TYPE(D3DPSHADERCAPS2_0);

template <class SerialiserType>
void DoSerialise(SerialiserType &ser, D3DCAPS9 &el)
{
  // Device Info
  SERIALISE_MEMBER(DeviceType);
  SERIALISE_MEMBER(AdapterOrdinal);

  // Caps from DX7 Draw
  SERIALISE_MEMBER(Caps);
  SERIALISE_MEMBER(Caps2);
  SERIALISE_MEMBER(Caps3);
  SERIALISE_MEMBER(PresentationIntervals);

  // Cursor Caps
  SERIALISE_MEMBER(CursorCaps);

  // 3D Device Caps
  SERIALISE_MEMBER(DevCaps);
  SERIALISE_MEMBER(PrimitiveMiscCaps);
  SERIALISE_MEMBER(RasterCaps);
  SERIALISE_MEMBER(ZCmpCaps);
  SERIALISE_MEMBER(SrcBlendCaps);
  SERIALISE_MEMBER(DestBlendCaps);
  SERIALISE_MEMBER(AlphaCmpCaps);
  SERIALISE_MEMBER(ShadeCaps);
  SERIALISE_MEMBER(TextureCaps);
  SERIALISE_MEMBER(TextureFilterCaps);
  SERIALISE_MEMBER(CubeTextureFilterCaps);
  SERIALISE_MEMBER(VolumeTextureFilterCaps);
  SERIALISE_MEMBER(TextureAddressCaps);
  SERIALISE_MEMBER(VolumeTextureAddressCaps);

  SERIALISE_MEMBER(LineCaps);

  SERIALISE_MEMBER(MaxTextureWidth);
  SERIALISE_MEMBER(MaxTextureHeight);
  SERIALISE_MEMBER(MaxVolumeExtent);

  SERIALISE_MEMBER(MaxTextureRepeat);
  SERIALISE_MEMBER(MaxTextureAspectRatio);
  SERIALISE_MEMBER(MaxAnisotropy);
  SERIALISE_MEMBER(MaxVertexW);

  SERIALISE_MEMBER(GuardBandLeft);
  SERIALISE_MEMBER(GuardBandTop);
  SERIALISE_MEMBER(GuardBandRight);
  SERIALISE_MEMBER(GuardBandBottom);

  SERIALISE_MEMBER(ExtentsAdjust);
  SERIALISE_MEMBER(StencilCaps);

  SERIALISE_MEMBER(FVFCaps);
  SERIALISE_MEMBER(TextureOpCaps);
  SERIALISE_MEMBER(MaxTextureBlendStages);
  SERIALISE_MEMBER(MaxSimultaneousTextures);

  SERIALISE_MEMBER(VertexProcessingCaps);
  SERIALISE_MEMBER(MaxActiveLights);
  SERIALISE_MEMBER(MaxUserClipPlanes);
  SERIALISE_MEMBER(MaxVertexBlendMatrices);
  SERIALISE_MEMBER(MaxVertexBlendMatrixIndex);

  SERIALISE_MEMBER(MaxPointSize);

  SERIALISE_MEMBER(MaxPrimitiveCount);
  SERIALISE_MEMBER(MaxVertexIndex);
  SERIALISE_MEMBER(MaxStreams);
  SERIALISE_MEMBER(MaxStreamStride);

  SERIALISE_MEMBER(VertexShaderVersion);
  SERIALISE_MEMBER(MaxVertexShaderConst);

  SERIALISE_MEMBER(PixelShaderVersion);
  SERIALISE_MEMBER(PixelShader1xMaxValue);

  // DX9 specific
  SERIALISE_MEMBER(DevCaps2);

  SERIALISE_MEMBER(MaxNpatchTessellationLevel);
  SERIALISE_MEMBER(Reserved5);

  SERIALISE_MEMBER(MasterAdapterOrdinal);
  SERIALISE_MEMBER(AdapterOrdinalInGroup);
  SERIALISE_MEMBER(NumberOfAdaptersInGroup);
  SERIALISE_MEMBER(DeclTypes);
  SERIALISE_MEMBER(NumSimultaneousRTs);
  SERIALISE_MEMBER(StretchRectFilterCaps);
  SERIALISE_MEMBER(VS20Caps);
  SERIALISE_MEMBER(PS20Caps);
  SERIALISE_MEMBER(VertexTextureFilterCaps);
  SERIALISE_MEMBER(MaxVShaderInstructionsExecuted);
  SERIALISE_MEMBER(MaxPShaderInstructionsExecuted);
  SERIALISE_MEMBER(MaxVertexShader30InstructionSlots);
  SERIALISE_MEMBER(MaxPixelShader30InstructionSlots);
}

INSTANTIATE_SERIALISE_TYPE(D3DCAPS9);
