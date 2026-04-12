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

#include "d3d9_common.h"

// Maximum counts for D3D9 state arrays
static const UINT D3D9_MAX_TEXTURE_STAGES = 8;
static const UINT D3D9_MAX_SAMPLERS = 16;
static const UINT D3D9_MAX_VS_SAMPLERS = 4;
static const UINT D3D9_TOTAL_SAMPLERS = D3D9_MAX_SAMPLERS + D3D9_MAX_VS_SAMPLERS;
static const UINT D3D9_MAX_STREAMS = 16;
static const UINT D3D9_MAX_RENDER_TARGETS = 4;
static const UINT D3D9_MAX_CLIP_PLANES = 6;
static const UINT D3D9_MAX_LIGHTS = 8;    // common practical max
static const UINT D3D9_MAX_TRANSFORMS = 256;
static const UINT D3D9_MAX_RENDER_STATES = 210;
static const UINT D3D9_MAX_TSS_STATES = 33;
static const UINT D3D9_MAX_SAMPLER_STATES = 14;
static const UINT D3D9_MAX_VS_CONSTANTS_F = 256;
static const UINT D3D9_MAX_VS_CONSTANTS_I = 16;
static const UINT D3D9_MAX_VS_CONSTANTS_B = 16;
static const UINT D3D9_MAX_PS_CONSTANTS_F = 224;
static const UINT D3D9_MAX_PS_CONSTANTS_I = 16;
static const UINT D3D9_MAX_PS_CONSTANTS_B = 16;

struct D3D9StreamSource
{
  ResourceId buffer;
  UINT offsetInBytes;
  UINT stride;
  UINT freq;
};

struct D3D9LightData
{
  D3DLIGHT9 light;
  bool enabled;
};

struct D3D9RenderState
{
  enum EmptyInit
  {
    Empty
  };
  D3D9RenderState(EmptyInit);
  D3D9RenderState(const D3D9RenderState &other);
  D3D9RenderState &operator=(const D3D9RenderState &other);

  void Clear();
  void CopyState(const D3D9RenderState &other);

  // Transforms (indexed by D3DTRANSFORMSTATETYPE: VIEW=2, PROJECTION=3, WORLD=256, TEXTURE0=16, etc.)
  D3DMATRIX transforms[D3D9_MAX_TRANSFORMS];

  // Fixed-function lighting
  rdcarray<D3D9LightData> lights;
  D3DMATERIAL9 material;

  // Render state (indexed by D3DRENDERSTATETYPE)
  DWORD renderStates[D3D9_MAX_RENDER_STATES];

  // Texture stage state [stage][D3DTEXTURESTAGESTATETYPE]
  DWORD textureStageStates[D3D9_MAX_TEXTURE_STAGES][D3D9_MAX_TSS_STATES];

  // Sampler state [sampler][D3DSAMPLERSTATETYPE]
  // Samplers 0-15 are pixel shader, 16-19 are vertex shader (D3DVERTEXTEXTURESAMPLER0-3)
  DWORD samplerStates[D3D9_TOTAL_SAMPLERS][D3D9_MAX_SAMPLER_STATES];

  // Bound textures (by ResourceId)
  ResourceId textures[D3D9_TOTAL_SAMPLERS];

  // Render targets and depth
  ResourceId renderTargets[D3D9_MAX_RENDER_TARGETS];
  ResourceId depthStencil;

  // Shaders
  ResourceId vertexShader;
  ResourceId pixelShader;

  // Vertex declaration / FVF
  ResourceId vertexDecl;
  DWORD FVF;

  // Stream sources
  D3D9StreamSource streamSources[D3D9_MAX_STREAMS];

  // Index buffer
  ResourceId indices;

  // Shader constants
  float vsConstantsF[D3D9_MAX_VS_CONSTANTS_F][4];
  int vsConstantsI[D3D9_MAX_VS_CONSTANTS_I][4];
  BOOL vsConstantsB[D3D9_MAX_VS_CONSTANTS_B];

  float psConstantsF[D3D9_MAX_PS_CONSTANTS_F][4];
  int psConstantsI[D3D9_MAX_PS_CONSTANTS_I][4];
  BOOL psConstantsB[D3D9_MAX_PS_CONSTANTS_B];

  // Viewport, scissor, clip planes
  D3DVIEWPORT9 viewport;
  RECT scissor;
  float clipPlanes[D3D9_MAX_CLIP_PLANES][4];

  // N-patch mode
  float nPatchMode;

  // Software vertex processing
  BOOL softwareVertexProcessing;
};
