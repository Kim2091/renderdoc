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
// Render state
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetRenderState(SerialiserType &ser,
                                                        D3DRENDERSTATETYPE State, DWORD Value)
{
  SERIALISE_ELEMENT(State).Important();
  SERIALISE_ELEMENT(Value);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetRenderState(State, Value);
    if((UINT)State < D3D9_MAX_RENDER_STATES)
      m_RenderState.renderStates[(UINT)State] = Value;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Sampler / texture stage state
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetSamplerState(SerialiserType &ser, DWORD Sampler,
                                                         D3DSAMPLERSTATETYPE Type, DWORD Value)
{
  SERIALISE_ELEMENT(Sampler);
  SERIALISE_ELEMENT(Type).Important();
  SERIALISE_ELEMENT(Value);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetSamplerState(Sampler, Type, Value);
    if(Sampler < D3D9_TOTAL_SAMPLERS && (UINT)Type < D3D9_MAX_SAMPLER_STATES)
      m_RenderState.samplerStates[Sampler][(UINT)Type] = Value;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetTextureStageState(SerialiserType &ser, DWORD Stage,
                                                              D3DTEXTURESTAGESTATETYPE Type,
                                                              DWORD Value)
{
  SERIALISE_ELEMENT(Stage);
  SERIALISE_ELEMENT(Type).Important();
  SERIALISE_ELEMENT(Value);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetTextureStageState(Stage, Type, Value);
    if(Stage < D3D9_MAX_TEXTURE_STAGES && (UINT)Type < D3D9_MAX_TSS_STATES)
      m_RenderState.textureStageStates[Stage][(UINT)Type] = Value;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Transforms
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetTransform(SerialiserType &ser,
                                                      D3DTRANSFORMSTATETYPE State,
                                                      const D3DMATRIX *pMatrix)
{
  SERIALISE_ELEMENT(State).Important();
  SERIALISE_ELEMENT_LOCAL(Matrix, pMatrix ? *pMatrix : D3DMATRIX());

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetTransform(State, &Matrix);
    if((UINT)State < D3D9_MAX_TRANSFORMS)
      m_RenderState.transforms[(UINT)State] = Matrix;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Viewport / scissor / clip
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetViewport(SerialiserType &ser,
                                                     const D3DVIEWPORT9 *pViewport)
{
  SERIALISE_ELEMENT_LOCAL(Viewport, pViewport ? *pViewport : D3DVIEWPORT9()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetViewport(&Viewport);
    m_RenderState.viewport = Viewport;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetScissorRect(SerialiserType &ser, const RECT *pRect)
{
  SERIALISE_ELEMENT_LOCAL(Rect, pRect ? *pRect : RECT()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetScissorRect(&Rect);
    m_RenderState.scissor = Rect;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetClipPlane(SerialiserType &ser, DWORD Index,
                                                      const float *pPlane)
{
  SERIALISE_ELEMENT(Index);
  // Clip plane is always 4 floats
  SERIALISE_ELEMENT_ARRAY(pPlane, 4u);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetClipPlane(Index, pPlane);
    if(Index < D3D9_MAX_CLIP_PLANES)
      memcpy(m_RenderState.clipPlanes[Index], pPlane, sizeof(float) * 4);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Fixed-function: material, lights
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetMaterial(SerialiserType &ser,
                                                     const D3DMATERIAL9 *pMaterial)
{
  SERIALISE_ELEMENT_LOCAL(Material, pMaterial ? *pMaterial : D3DMATERIAL9());

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetMaterial(&Material);
    m_RenderState.material = Material;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetLight(SerialiserType &ser, DWORD Index,
                                                  const D3DLIGHT9 *pLight)
{
  SERIALISE_ELEMENT(Index);
  SERIALISE_ELEMENT_LOCAL(Light, pLight ? *pLight : D3DLIGHT9());

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetLight(Index, &Light);
    if(Index >= m_RenderState.lights.size())
      m_RenderState.lights.resize(Index + 1);
    m_RenderState.lights[Index].light = Light;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_LightEnable(SerialiserType &ser, DWORD Index, BOOL Enable)
{
  SERIALISE_ELEMENT(Index);
  SERIALISE_ELEMENT(Enable);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->LightEnable(Index, Enable);
    if(Index >= m_RenderState.lights.size())
      m_RenderState.lights.resize(Index + 1);
    m_RenderState.lights[Index].enabled = (Enable != FALSE);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Shader binding
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetVertexShader(SerialiserType &ser,
                                                         IDirect3DVertexShader9 *pShader)
{
  // Resource wrappers not yet available; serialize a placeholder ResourceId
  SERIALISE_ELEMENT_LOCAL(Shader, ResourceId()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    // TODO: look up wrapped shader from ResourceId once resource wrappers exist
    m_RenderState.vertexShader = Shader;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetPixelShader(SerialiserType &ser,
                                                        IDirect3DPixelShader9 *pShader)
{
  SERIALISE_ELEMENT_LOCAL(Shader, ResourceId()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    // TODO: look up wrapped shader from ResourceId once resource wrappers exist
    m_RenderState.pixelShader = Shader;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetVertexDeclaration(SerialiserType &ser,
                                                              IDirect3DVertexDeclaration9 *pDecl)
{
  SERIALISE_ELEMENT_LOCAL(Decl, ResourceId()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    // TODO: look up wrapped declaration from ResourceId once resource wrappers exist
    m_RenderState.vertexDecl = Decl;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetFVF(SerialiserType &ser, DWORD FVF)
{
  SERIALISE_ELEMENT(FVF).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetFVF(FVF);
    m_RenderState.FVF = FVF;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Shader constants
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetVertexShaderConstantF(SerialiserType &ser,
                                                                  UINT StartRegister,
                                                                  const float *pConstantData,
                                                                  UINT Vector4fCount)
{
  SERIALISE_ELEMENT(StartRegister);
  SERIALISE_ELEMENT(Vector4fCount);
  SERIALISE_ELEMENT_ARRAY(pConstantData, Vector4fCount * 4u);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    if(pConstantData)
    {
      for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_F; i++)
        memcpy(m_RenderState.vsConstantsF[StartRegister + i], &pConstantData[i * 4],
               sizeof(float) * 4);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetVertexShaderConstantI(SerialiserType &ser,
                                                                  UINT StartRegister,
                                                                  const int *pConstantData,
                                                                  UINT Vector4iCount)
{
  SERIALISE_ELEMENT(StartRegister);
  SERIALISE_ELEMENT(Vector4iCount);
  SERIALISE_ELEMENT_ARRAY(pConstantData, Vector4iCount * 4u);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    if(pConstantData)
    {
      for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_I; i++)
        memcpy(m_RenderState.vsConstantsI[StartRegister + i], &pConstantData[i * 4],
               sizeof(int) * 4);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetVertexShaderConstantB(SerialiserType &ser,
                                                                  UINT StartRegister,
                                                                  const BOOL *pConstantData,
                                                                  UINT BoolCount)
{
  SERIALISE_ELEMENT(StartRegister);
  SERIALISE_ELEMENT(BoolCount);
  SERIALISE_ELEMENT_ARRAY(pConstantData, BoolCount);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    if(pConstantData)
    {
      for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_VS_CONSTANTS_B; i++)
        m_RenderState.vsConstantsB[StartRegister + i] = pConstantData[i];
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetPixelShaderConstantF(SerialiserType &ser,
                                                                 UINT StartRegister,
                                                                 const float *pConstantData,
                                                                 UINT Vector4fCount)
{
  SERIALISE_ELEMENT(StartRegister);
  SERIALISE_ELEMENT(Vector4fCount);
  SERIALISE_ELEMENT_ARRAY(pConstantData, Vector4fCount * 4u);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    if(pConstantData)
    {
      for(UINT i = 0; i < Vector4fCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_F; i++)
        memcpy(m_RenderState.psConstantsF[StartRegister + i], &pConstantData[i * 4],
               sizeof(float) * 4);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetPixelShaderConstantI(SerialiserType &ser,
                                                                 UINT StartRegister,
                                                                 const int *pConstantData,
                                                                 UINT Vector4iCount)
{
  SERIALISE_ELEMENT(StartRegister);
  SERIALISE_ELEMENT(Vector4iCount);
  SERIALISE_ELEMENT_ARRAY(pConstantData, Vector4iCount * 4u);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    if(pConstantData)
    {
      for(UINT i = 0; i < Vector4iCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_I; i++)
        memcpy(m_RenderState.psConstantsI[StartRegister + i], &pConstantData[i * 4],
               sizeof(int) * 4);
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetPixelShaderConstantB(SerialiserType &ser,
                                                                 UINT StartRegister,
                                                                 const BOOL *pConstantData,
                                                                 UINT BoolCount)
{
  SERIALISE_ELEMENT(StartRegister);
  SERIALISE_ELEMENT(BoolCount);
  SERIALISE_ELEMENT_ARRAY(pConstantData, BoolCount);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    if(pConstantData)
    {
      for(UINT i = 0; i < BoolCount && (StartRegister + i) < D3D9_MAX_PS_CONSTANTS_B; i++)
        m_RenderState.psConstantsB[StartRegister + i] = pConstantData[i];
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Resource binding
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetTexture(SerialiserType &ser, DWORD Stage,
                                                    IDirect3DBaseTexture9 *pTexture)
{
  SERIALISE_ELEMENT(Stage);
  // Resource wrappers not yet available; serialize a placeholder ResourceId
  SERIALISE_ELEMENT_LOCAL(Texture, ResourceId()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    // TODO: look up wrapped texture from ResourceId once resource wrappers exist
    if(Stage < D3D9_TOTAL_SAMPLERS)
      m_RenderState.textures[Stage] = Texture;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetStreamSource(SerialiserType &ser, UINT StreamNumber,
                                                         IDirect3DVertexBuffer9 *pStreamData,
                                                         UINT OffsetInBytes, UINT Stride)
{
  SERIALISE_ELEMENT(StreamNumber);

  ResourceId Buffer;
  if(ser.IsWriting())
    Buffer = GetIDForD3D9Resource(pStreamData);
  SERIALISE_ELEMENT(Buffer).Important();
  SERIALISE_ELEMENT(OffsetInBytes);
  SERIALISE_ELEMENT(Stride);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(StreamNumber < D3D9_MAX_STREAMS)
    {
      m_RenderState.streamSources[StreamNumber].buffer = Buffer;
      m_RenderState.streamSources[StreamNumber].offsetInBytes = OffsetInBytes;
      m_RenderState.streamSources[StreamNumber].stride = Stride;
    }
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetStreamSourceFreq(SerialiserType &ser, UINT StreamNumber,
                                                              UINT Setting)
{
  SERIALISE_ELEMENT(StreamNumber);
  SERIALISE_ELEMENT(Setting);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetStreamSourceFreq(StreamNumber, Setting);
    if(StreamNumber < D3D9_MAX_STREAMS)
      m_RenderState.streamSources[StreamNumber].freq = Setting;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetIndices(SerialiserType &ser,
                                                    IDirect3DIndexBuffer9 *pIndexData)
{
  ResourceId IndexBuffer;
  if(ser.IsWriting())
    IndexBuffer = GetIDForD3D9Resource(pIndexData);
  SERIALISE_ELEMENT(IndexBuffer).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_RenderState.indices = IndexBuffer;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetRenderTarget(SerialiserType &ser,
                                                         DWORD RenderTargetIndex,
                                                         IDirect3DSurface9 *pRenderTarget)
{
  SERIALISE_ELEMENT(RenderTargetIndex);
  // Resource wrappers not yet available; serialize a placeholder ResourceId
  SERIALISE_ELEMENT_LOCAL(RenderTarget, ResourceId()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    // TODO: look up wrapped surface from ResourceId once resource wrappers exist
    if(RenderTargetIndex < D3D9_MAX_RENDER_TARGETS)
      m_RenderState.renderTargets[RenderTargetIndex] = RenderTarget;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetDepthStencilSurface(SerialiserType &ser,
                                                                IDirect3DSurface9 *pNewZStencil)
{
  // Resource wrappers not yet available; serialize a placeholder ResourceId
  SERIALISE_ELEMENT_LOCAL(DepthSurface, ResourceId()).Important();

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    // TODO: look up wrapped surface from ResourceId once resource wrappers exist
    m_RenderState.depthStencil = DepthSurface;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Scene / frame
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_BeginScene(SerialiserType &ser)
{
  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->BeginScene();
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_EndScene(SerialiserType &ser)
{
  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->EndScene();
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_Clear(SerialiserType &ser, DWORD Count,
                                               const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color,
                                               float Z, DWORD Stencil)
{
  SERIALISE_ELEMENT(Count);
  SERIALISE_ELEMENT_ARRAY(pRects, Count);
  SERIALISE_ELEMENT(Flags).Important();
  SERIALISE_ELEMENT(Color);
  SERIALISE_ELEMENT(Z);
  SERIALISE_ELEMENT(Stencil);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->Clear(Count, pRects, Flags, Color, Z, Stencil);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Misc state: N-patch, software vertex processing
///////////////////////////////////////////////////////////////////////////

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetNPatchMode(SerialiserType &ser, float nSegments)
{
  SERIALISE_ELEMENT(nSegments);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetNPatchMode(nSegments);
    m_RenderState.nPatchMode = nSegments;
  }

  return true;
}

template <typename SerialiserType>
bool WrappedIDirect3DDevice9::Serialise_SetSoftwareVertexProcessing(SerialiserType &ser,
                                                                     BOOL bSoftware)
{
  SERIALISE_ELEMENT(bSoftware);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    m_pDevice->SetSoftwareVertexProcessing(bSoftware);
    m_RenderState.softwareVertexProcessing = bSoftware;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Template instantiations
///////////////////////////////////////////////////////////////////////////

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetRenderState,
                                D3DRENDERSTATETYPE State, DWORD Value);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetSamplerState, DWORD Sampler,
                                D3DSAMPLERSTATETYPE Type, DWORD Value);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetTextureStageState, DWORD Stage,
                                D3DTEXTURESTAGESTATETYPE Type, DWORD Value);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetTransform,
                                D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetViewport,
                                CONST D3DVIEWPORT9 *pViewport);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetScissorRect,
                                CONST RECT *pRect);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetClipPlane, DWORD Index,
                                CONST float *pPlane);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetMaterial,
                                CONST D3DMATERIAL9 *pMaterial);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetLight, DWORD Index,
                                CONST D3DLIGHT9 *pLight);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, LightEnable, DWORD Index,
                                BOOL Enable);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetVertexShader,
                                IDirect3DVertexShader9 *pShader);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetPixelShader,
                                IDirect3DPixelShader9 *pShader);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetVertexDeclaration,
                                IDirect3DVertexDeclaration9 *pDecl);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetFVF, DWORD FVF);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetVertexShaderConstantF,
                                UINT StartRegister, CONST float *pConstantData,
                                UINT Vector4fCount);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetVertexShaderConstantI,
                                UINT StartRegister, CONST int *pConstantData, UINT Vector4iCount);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetVertexShaderConstantB,
                                UINT StartRegister, CONST BOOL *pConstantData, UINT BoolCount);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetPixelShaderConstantF,
                                UINT StartRegister, CONST float *pConstantData,
                                UINT Vector4fCount);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetPixelShaderConstantI,
                                UINT StartRegister, CONST int *pConstantData, UINT Vector4iCount);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetPixelShaderConstantB,
                                UINT StartRegister, CONST BOOL *pConstantData, UINT BoolCount);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetTexture, DWORD Stage,
                                IDirect3DBaseTexture9 *pTexture);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetStreamSource,
                                UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData,
                                UINT OffsetInBytes, UINT Stride);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetStreamSourceFreq,
                                UINT StreamNumber, UINT Setting);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetIndices,
                                IDirect3DIndexBuffer9 *pIndexData);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetRenderTarget,
                                DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetDepthStencilSurface,
                                IDirect3DSurface9 *pNewZStencil);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, BeginScene);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, EndScene);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, Clear, DWORD Count,
                                CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z,
                                DWORD Stencil);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetNPatchMode, float nSegments);

INSTANTIATE_FUNCTION_SERIALISED(HRESULT, WrappedIDirect3DDevice9, SetSoftwareVertexProcessing,
                                BOOL bSoftware);
