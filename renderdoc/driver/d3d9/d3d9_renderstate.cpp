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

#include "d3d9_renderstate.h"

D3D9RenderState::D3D9RenderState(EmptyInit)
{
  Clear();
}

D3D9RenderState::D3D9RenderState(const D3D9RenderState &other)
{
  CopyState(other);
}

D3D9RenderState &D3D9RenderState::operator=(const D3D9RenderState &other)
{
  CopyState(other);
  return *this;
}

void D3D9RenderState::Clear()
{
  // Identity matrix
  D3DMATRIX identity = {};
  identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;

  for(UINT i = 0; i < D3D9_MAX_TRANSFORMS; i++)
    transforms[i] = identity;

  lights.clear();
  memset(&material, 0, sizeof(material));

  memset(renderStates, 0, sizeof(renderStates));

  // Set important render state defaults (D3D9 defaults)
  renderStates[D3DRS_ZENABLE] = D3DZB_TRUE;
  renderStates[D3DRS_FILLMODE] = D3DFILL_SOLID;
  renderStates[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
  renderStates[D3DRS_ZWRITEENABLE] = TRUE;
  renderStates[D3DRS_ALPHATESTENABLE] = FALSE;
  renderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
  renderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
  renderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
  renderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
  renderStates[D3DRS_ALPHAREF] = 0;
  renderStates[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
  renderStates[D3DRS_DITHERENABLE] = FALSE;
  renderStates[D3DRS_ALPHABLENDENABLE] = FALSE;
  renderStates[D3DRS_FOGENABLE] = FALSE;
  renderStates[D3DRS_SPECULARENABLE] = FALSE;
  renderStates[D3DRS_LIGHTING] = TRUE;
  renderStates[D3DRS_COLORVERTEX] = TRUE;
  renderStates[D3DRS_STENCILENABLE] = FALSE;
  renderStates[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
  renderStates[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
  renderStates[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
  renderStates[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
  renderStates[D3DRS_STENCILREF] = 0;
  renderStates[D3DRS_STENCILMASK] = 0xFFFFFFFF;
  renderStates[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
  renderStates[D3DRS_WRAP0] = 0;
  renderStates[D3DRS_CLIPPING] = TRUE;
  renderStates[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
  renderStates[D3DRS_COLORWRITEENABLE] = 0x0000000F;
  renderStates[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
  renderStates[D3DRS_SCISSORTESTENABLE] = FALSE;
  renderStates[D3DRS_SLOPESCALEDEPTHBIAS] = 0;
  renderStates[D3DRS_ANTIALIASEDLINEENABLE] = FALSE;
  renderStates[D3DRS_TWOSIDEDSTENCILMODE] = FALSE;
  renderStates[D3DRS_SEPARATEALPHABLENDENABLE] = FALSE;
  renderStates[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
  renderStates[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
  renderStates[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;

  memset(textureStageStates, 0, sizeof(textureStageStates));
  memset(samplerStates, 0, sizeof(samplerStates));

  // Default sampler states
  for(UINT i = 0; i < D3D9_TOTAL_SAMPLERS; i++)
  {
    samplerStates[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
    samplerStates[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
    samplerStates[i][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
    samplerStates[i][D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
    samplerStates[i][D3DSAMP_MINFILTER] = D3DTEXF_POINT;
    samplerStates[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
    samplerStates[i][D3DSAMP_MAXANISOTROPY] = 1;
    samplerStates[i][D3DSAMP_MAXMIPLEVEL] = 0;
  }

  // Default texture stage state (stage 0)
  textureStageStates[0][D3DTSS_COLOROP] = D3DTOP_MODULATE;
  textureStageStates[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
  textureStageStates[0][D3DTSS_COLORARG2] = D3DTA_CURRENT;
  textureStageStates[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
  textureStageStates[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
  textureStageStates[0][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
  for(UINT i = 1; i < D3D9_MAX_TEXTURE_STAGES; i++)
  {
    textureStageStates[i][D3DTSS_COLOROP] = D3DTOP_DISABLE;
    textureStageStates[i][D3DTSS_ALPHAOP] = D3DTOP_DISABLE;
    textureStageStates[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    textureStageStates[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
    textureStageStates[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
    textureStageStates[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
  }
  for(UINT i = 0; i < D3D9_MAX_TEXTURE_STAGES; i++)
    textureStageStates[i][D3DTSS_TEXCOORDINDEX] = i;

  for(UINT i = 0; i < D3D9_TOTAL_SAMPLERS; i++)
    textures[i] = ResourceId();

  for(UINT i = 0; i < D3D9_MAX_RENDER_TARGETS; i++)
    renderTargets[i] = ResourceId();
  depthStencil = ResourceId();

  vertexShader = ResourceId();
  pixelShader = ResourceId();
  vertexDecl = ResourceId();
  FVF = 0;

  for(UINT i = 0; i < D3D9_MAX_STREAMS; i++)
  {
    streamSources[i].buffer = ResourceId();
    streamSources[i].offsetInBytes = 0;
    streamSources[i].stride = 0;
    streamSources[i].freq = 1;
  }

  indices = ResourceId();

  memset(vsConstantsF, 0, sizeof(vsConstantsF));
  memset(vsConstantsI, 0, sizeof(vsConstantsI));
  memset(vsConstantsB, 0, sizeof(vsConstantsB));
  memset(psConstantsF, 0, sizeof(psConstantsF));
  memset(psConstantsI, 0, sizeof(psConstantsI));
  memset(psConstantsB, 0, sizeof(psConstantsB));

  memset(&viewport, 0, sizeof(viewport));
  memset(&scissor, 0, sizeof(scissor));
  memset(clipPlanes, 0, sizeof(clipPlanes));

  nPatchMode = 0.0f;
  softwareVertexProcessing = FALSE;
}

void D3D9RenderState::CopyState(const D3D9RenderState &other)
{
  memcpy(transforms, other.transforms, sizeof(transforms));
  lights = other.lights;
  material = other.material;
  memcpy(renderStates, other.renderStates, sizeof(renderStates));
  memcpy(textureStageStates, other.textureStageStates, sizeof(textureStageStates));
  memcpy(samplerStates, other.samplerStates, sizeof(samplerStates));
  memcpy(textures, other.textures, sizeof(textures));
  memcpy(renderTargets, other.renderTargets, sizeof(renderTargets));
  depthStencil = other.depthStencil;
  vertexShader = other.vertexShader;
  pixelShader = other.pixelShader;
  vertexDecl = other.vertexDecl;
  FVF = other.FVF;
  memcpy(streamSources, other.streamSources, sizeof(streamSources));
  indices = other.indices;
  memcpy(vsConstantsF, other.vsConstantsF, sizeof(vsConstantsF));
  memcpy(vsConstantsI, other.vsConstantsI, sizeof(vsConstantsI));
  memcpy(vsConstantsB, other.vsConstantsB, sizeof(vsConstantsB));
  memcpy(psConstantsF, other.psConstantsF, sizeof(psConstantsF));
  memcpy(psConstantsI, other.psConstantsI, sizeof(psConstantsI));
  memcpy(psConstantsB, other.psConstantsB, sizeof(psConstantsB));
  viewport = other.viewport;
  scissor = other.scissor;
  memcpy(clipPlanes, other.clipPlanes, sizeof(clipPlanes));
  nPatchMode = other.nPatchMode;
  softwareVertexProcessing = other.softwareVertexProcessing;
}
