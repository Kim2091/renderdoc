/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2025-2026 Baldur Karlsson
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

#include "common_pipestate.h"

// NOTE: Remember that python sees namespaces flattened to a prefix - i.e. D3D9Pipe::State is
// renamed to D3D9State, so these types must be referenced in the documentation

namespace D3D9Pipe
{
DOCUMENT("Describes a single D3D9 vertex declaration element.");
struct VertexElement
{
  DOCUMENT("");
  VertexElement() = default;
  VertexElement(const VertexElement &) = default;
  VertexElement &operator=(const VertexElement &) = default;

  bool operator==(const VertexElement &o) const
  {
    return stream == o.stream && offset == o.offset && type == o.type && method == o.method &&
           usage == o.usage && usageIndex == o.usageIndex;
  }
  bool operator<(const VertexElement &o) const
  {
    if(!(stream == o.stream))
      return stream < o.stream;
    if(!(offset == o.offset))
      return offset < o.offset;
    if(!(type == o.type))
      return type < o.type;
    if(!(method == o.method))
      return method < o.method;
    if(!(usage == o.usage))
      return usage < o.usage;
    if(!(usageIndex == o.usageIndex))
      return usageIndex < o.usageIndex;
    return false;
  }
  DOCUMENT(R"(The stream index for this element.

:type: int
)");
  uint16_t stream = 0;

  DOCUMENT(R"(The byte offset from the start of the vertex data for this element.

:type: int
)");
  uint16_t offset = 0;

  DOCUMENT(R"(The D3DDECLTYPE value describing the data type.

:type: int
)");
  uint32_t type = 0;

  DOCUMENT(R"(The D3DDECLMETHOD value describing the tessellator processing method.

:type: int
)");
  uint32_t method = 0;

  DOCUMENT(R"(The D3DDECLUSAGE value describing the intended use of the data.

:type: int
)");
  uint32_t usage = 0;

  DOCUMENT(R"(The usage index that distinguishes multiple elements with the same usage semantic.

:type: int
)");
  uint32_t usageIndex = 0;
};

DOCUMENT("Describes a single D3D9 vertex buffer binding.");
struct VertexBuffer
{
  DOCUMENT("");
  VertexBuffer() = default;
  VertexBuffer(const VertexBuffer &) = default;
  VertexBuffer &operator=(const VertexBuffer &) = default;

  bool operator==(const VertexBuffer &o) const
  {
    return resourceId == o.resourceId && byteOffset == o.byteOffset &&
           byteStride == o.byteStride && frequency == o.frequency;
  }
  bool operator<(const VertexBuffer &o) const
  {
    if(!(resourceId == o.resourceId))
      return resourceId < o.resourceId;
    if(!(byteOffset == o.byteOffset))
      return byteOffset < o.byteOffset;
    if(!(byteStride == o.byteStride))
      return byteStride < o.byteStride;
    if(!(frequency == o.frequency))
      return frequency < o.frequency;
    return false;
  }
  DOCUMENT(R"(The :class:`ResourceId` of the buffer bound to this slot.

:type: ResourceId
)");
  ResourceId resourceId;

  DOCUMENT(R"(The byte offset from the start of the buffer to the beginning of the vertex data.

:type: int
)");
  uint32_t byteOffset = 0;

  DOCUMENT(R"(The byte stride between the start of one set of vertex data and the next.

:type: int
)");
  uint32_t byteStride = 0;

  DOCUMENT(R"(The stream source frequency divider for instancing.

:type: int
)");
  uint32_t frequency = 0;
};

DOCUMENT("Describes the D3D9 index buffer binding.");
struct IndexBuffer
{
  DOCUMENT("");
  IndexBuffer() = default;
  IndexBuffer(const IndexBuffer &) = default;
  IndexBuffer &operator=(const IndexBuffer &) = default;

  DOCUMENT(R"(The :class:`ResourceId` of the index buffer.

:type: ResourceId
)");
  ResourceId resourceId;

  DOCUMENT(R"(The number of bytes for each index in the index buffer. Typically 2 or 4 bytes but
it can be 0 if no index buffer is bound.

:type: int
)");
  uint32_t byteStride = 0;
};

DOCUMENT("Describes the D3D9 input assembler data.");
struct InputAssembly
{
  DOCUMENT("");
  InputAssembly() = default;
  InputAssembly(const InputAssembly &) = default;
  InputAssembly &operator=(const InputAssembly &) = default;

  DOCUMENT(R"(The vertex declaration elements.

:type: List[D3D9VertexElement]
)");
  rdcarray<VertexElement> vertexElements;

  DOCUMENT(R"(The flexible vertex format (FVF) code, if used instead of a vertex declaration.

:type: int
)");
  uint32_t FVF = 0;

  DOCUMENT(R"(The bound vertex buffers.

:type: List[D3D9VertexBuffer]
)");
  rdcarray<VertexBuffer> vertexBuffers;

  DOCUMENT(R"(The bound index buffer.

:type: D3D9IndexBuffer
)");
  IndexBuffer indexBuffer;
};

DOCUMENT("Describes the D3D9 shader constants for a shader stage.");
struct ShaderConstant
{
  DOCUMENT("");
  ShaderConstant() = default;
  ShaderConstant(const ShaderConstant &) = default;
  ShaderConstant &operator=(const ShaderConstant &) = default;

  DOCUMENT(R"(The float constant registers (vector of float4 values, 4 floats per register).

:type: List[float]
)");
  rdcarray<float> floatConstants;

  DOCUMENT(R"(The integer constant registers (vector of int4 values, 4 ints per register).

:type: List[int]
)");
  rdcarray<int32_t> intConstants;

  DOCUMENT(R"(The boolean constant registers.

:type: List[int]
)");
  rdcarray<uint32_t> boolConstants;
};

DOCUMENT("Describes a D3D9 shader stage.");
struct Shader
{
  DOCUMENT("");
  Shader() = default;
  Shader(const Shader &) = default;
  Shader &operator=(const Shader &) = default;

  DOCUMENT(R"(The :class:`ResourceId` of the shader itself.

:type: ResourceId
)");
  ResourceId resourceId;

  DOCUMENT(R"(The reflection data for this shader.

:type: ShaderReflection
)");
  const ShaderReflection *reflection = NULL;

  DOCUMENT(R"(The shader constants bound to this stage.

:type: D3D9ShaderConstant
)");
  ShaderConstant constants;
};

DOCUMENT("Describes the D3D9 fixed-function transform state.");
struct Transform
{
  DOCUMENT("");
  Transform() = default;
  Transform(const Transform &) = default;
  Transform &operator=(const Transform &) = default;

  DOCUMENT(R"(The world transform matrix (4x4, stored row-major).

:type: List[float]
)");
  rdcfixedarray<float, 16> world = {};

  DOCUMENT(R"(The view transform matrix (4x4, stored row-major).

:type: List[float]
)");
  rdcfixedarray<float, 16> view = {};

  DOCUMENT(R"(The projection transform matrix (4x4, stored row-major).

:type: List[float]
)");
  rdcfixedarray<float, 16> projection = {};

  DOCUMENT(R"(The texture transform matrices for each of the 8 texture stages (each 4x4, stored row-major).

:type: List[List[float]]
)");
  rdcfixedarray<rdcfixedarray<float, 16>, 8> texture = {};
};

DOCUMENT("Describes a D3D9 light.");
struct Light
{
  DOCUMENT("");
  Light() = default;
  Light(const Light &) = default;
  Light &operator=(const Light &) = default;

  DOCUMENT(R"(The D3DLIGHTTYPE value.

:type: int
)");
  uint32_t type = 0;

  DOCUMENT(R"(The diffuse color of the light.

:type: FloatVector
)");
  FloatVector diffuse;

  DOCUMENT(R"(The specular color of the light.

:type: FloatVector
)");
  FloatVector specular;

  DOCUMENT(R"(The ambient color of the light.

:type: FloatVector
)");
  FloatVector ambient;

  DOCUMENT(R"(The position of the light in world space.

:type: FloatVector
)");
  FloatVector position;

  DOCUMENT(R"(The direction of the light.

:type: FloatVector
)");
  FloatVector direction;

  DOCUMENT(R"(The distance beyond which the light has no effect.

:type: float
)");
  float range = 0.0f;

  DOCUMENT(R"(The decrease in illumination between a spotlight's inner cone and outer cone.

:type: float
)");
  float falloff = 0.0f;

  DOCUMENT(R"(The constant attenuation factor.

:type: float
)");
  float attenuation0 = 0.0f;

  DOCUMENT(R"(The linear attenuation factor.

:type: float
)");
  float attenuation1 = 0.0f;

  DOCUMENT(R"(The quadratic attenuation factor.

:type: float
)");
  float attenuation2 = 0.0f;

  DOCUMENT(R"(The angle in radians of the spotlight's inner cone.

:type: float
)");
  float theta = 0.0f;

  DOCUMENT(R"(The angle in radians of the spotlight's outer cone.

:type: float
)");
  float phi = 0.0f;

  DOCUMENT(R"(Whether this light is enabled.

:type: bool
)");
  bool enabled = false;
};

DOCUMENT("Describes the D3D9 material properties.");
struct Material
{
  DOCUMENT("");
  Material() = default;
  Material(const Material &) = default;
  Material &operator=(const Material &) = default;

  DOCUMENT(R"(The diffuse color of the material.

:type: FloatVector
)");
  FloatVector diffuse;

  DOCUMENT(R"(The specular color of the material.

:type: FloatVector
)");
  FloatVector specular;

  DOCUMENT(R"(The ambient color of the material.

:type: FloatVector
)");
  FloatVector ambient;

  DOCUMENT(R"(The emissive color of the material.

:type: FloatVector
)");
  FloatVector emissive;

  DOCUMENT(R"(The sharpness of specular highlights. Higher values produce sharper highlights.

:type: float
)");
  float power = 0.0f;
};

DOCUMENT("Describes a D3D9 texture stage state configuration.");
struct TextureStageState
{
  DOCUMENT("");
  TextureStageState() = default;
  TextureStageState(const TextureStageState &) = default;
  TextureStageState &operator=(const TextureStageState &) = default;

  DOCUMENT(R"(The D3DTEXTUREOP for the color channel operation.

:type: int
)");
  uint32_t colorOp = 0;

  DOCUMENT(R"(The first color argument (D3DTA value).

:type: int
)");
  uint32_t colorArg1 = 0;

  DOCUMENT(R"(The second color argument (D3DTA value).

:type: int
)");
  uint32_t colorArg2 = 0;

  DOCUMENT(R"(The D3DTEXTUREOP for the alpha channel operation.

:type: int
)");
  uint32_t alphaOp = 0;

  DOCUMENT(R"(The first alpha argument (D3DTA value).

:type: int
)");
  uint32_t alphaArg1 = 0;

  DOCUMENT(R"(The second alpha argument (D3DTA value).

:type: int
)");
  uint32_t alphaArg2 = 0;

  DOCUMENT(R"(The texture coordinate index and optional generation flags.

:type: int
)");
  uint32_t texCoordIndex = 0;

  DOCUMENT(R"(The D3DTEXTURETRANSFORMFLAGS value controlling texture coordinate transform.

:type: int
)");
  uint32_t textureTransformFlags = 0;
};

DOCUMENT("Describes a D3D9 sampler state.");
struct SamplerState
{
  DOCUMENT("");
  SamplerState() = default;
  SamplerState(const SamplerState &) = default;
  SamplerState &operator=(const SamplerState &) = default;

  DOCUMENT(R"(The D3DTEXTUREADDRESS value for the U (horizontal) texture coordinate.

:type: int
)");
  uint32_t addressU = 0;

  DOCUMENT(R"(The D3DTEXTUREADDRESS value for the V (vertical) texture coordinate.

:type: int
)");
  uint32_t addressV = 0;

  DOCUMENT(R"(The D3DTEXTUREADDRESS value for the W (depth) texture coordinate.

:type: int
)");
  uint32_t addressW = 0;

  DOCUMENT(R"(The D3DTEXTUREFILTERTYPE for magnification filtering.

:type: int
)");
  uint32_t magFilter = 0;

  DOCUMENT(R"(The D3DTEXTUREFILTERTYPE for minification filtering.

:type: int
)");
  uint32_t minFilter = 0;

  DOCUMENT(R"(The D3DTEXTUREFILTERTYPE for mipmap filtering.

:type: int
)");
  uint32_t mipFilter = 0;

  DOCUMENT(R"(The maximum anisotropy level for anisotropic filtering.

:type: int
)");
  uint32_t maxAnisotropy = 0;

  DOCUMENT(R"(The most detailed mipmap level to use.

:type: int
)");
  uint32_t maxMipLevel = 0;

  DOCUMENT(R"(The mipmap level of detail bias.

:type: float
)");
  float mipLODBias = 0.0f;

  DOCUMENT(R"(``True`` if sRGB texture reads are enabled for this sampler.

:type: bool
)");
  bool sRGB = false;
};

DOCUMENT("Describes a D3D9 texture stage including bound texture, sampler, and stage state.");
struct TextureStage
{
  DOCUMENT("");
  TextureStage() = default;
  TextureStage(const TextureStage &) = default;
  TextureStage &operator=(const TextureStage &) = default;

  DOCUMENT(R"(The :class:`ResourceId` of the texture bound to this stage.

:type: ResourceId
)");
  ResourceId texture;

  DOCUMENT(R"(The sampler state for this texture stage.

:type: D3D9SamplerState
)");
  SamplerState sampler;

  DOCUMENT(R"(The texture stage state configuration.

:type: D3D9TextureStageState
)");
  TextureStageState stageState;
};

DOCUMENT("Describes the D3D9 fixed-function pipeline state.");
struct FixedFunction
{
  DOCUMENT("");
  FixedFunction() = default;
  FixedFunction(const FixedFunction &) = default;
  FixedFunction &operator=(const FixedFunction &) = default;

  DOCUMENT(R"(The fixed-function transform state.

:type: D3D9Transform
)");
  Transform transforms;

  DOCUMENT(R"(The currently set lights.

:type: List[D3D9Light]
)");
  rdcarray<Light> lights;

  DOCUMENT(R"(The current material properties.

:type: D3D9Material
)");
  Material material;

  DOCUMENT(R"(``True`` if lighting calculations are enabled.

:type: bool
)");
  bool lightingEnabled = false;

  DOCUMENT(R"(``True`` if fog calculations are enabled.

:type: bool
)");
  bool fogEnabled = false;
};

DOCUMENT("Describes the D3D9 rasterizer render state.");
struct RenderState
{
  DOCUMENT("");
  RenderState() = default;
  RenderState(const RenderState &) = default;
  RenderState &operator=(const RenderState &) = default;

  DOCUMENT(R"(The D3DFILLMODE value for polygon fill mode.

:type: int
)");
  uint32_t fillMode = 0;

  DOCUMENT(R"(The D3DCULL value for face culling mode.

:type: int
)");
  uint32_t cullMode = 0;

  DOCUMENT(R"(The depth bias applied to z-values.

:type: float
)");
  float depthBias = 0.0f;

  DOCUMENT(R"(The slope-scaled depth bias.

:type: float
)");
  float slopeScaledDepthBias = 0.0f;

  DOCUMENT(R"(``True`` if the scissor test is enabled.

:type: bool
)");
  bool scissorEnable = false;

  DOCUMENT(R"(``True`` if multisampling is enabled.

:type: bool
)");
  bool multisampleEnable = false;

  DOCUMENT(R"(``True`` if anti-aliased line drawing is enabled.

:type: bool
)");
  bool antialiasedLineEnable = false;

  DOCUMENT(R"(The user-defined clip planes. Up to 6 planes, each defined by 4 floats (a, b, c, d).

:type: List[List[float]]
)");
  rdcfixedarray<rdcfixedarray<float, 4>, 6> clipPlanes = {};

  DOCUMENT(R"(A bitmask indicating which clip planes are enabled.

:type: int
)");
  uint32_t clipPlaneEnable = 0;
};

DOCUMENT("Describes the D3D9 blend state.");
struct BlendState
{
  DOCUMENT("");
  BlendState() = default;
  BlendState(const BlendState &) = default;
  BlendState &operator=(const BlendState &) = default;

  DOCUMENT(R"(``True`` if alpha blending is enabled.

:type: bool
)");
  bool alphaBlendEnable = false;

  DOCUMENT(R"(The D3DBLEND value for the source blend factor.

:type: int
)");
  uint32_t srcBlend = 0;

  DOCUMENT(R"(The D3DBLEND value for the destination blend factor.

:type: int
)");
  uint32_t destBlend = 0;

  DOCUMENT(R"(The D3DBLENDOP value for the blend operation.

:type: int
)");
  uint32_t blendOp = 0;

  DOCUMENT(R"(``True`` if separate alpha blending is enabled.

:type: bool
)");
  bool separateAlphaBlendEnable = false;

  DOCUMENT(R"(The D3DBLEND value for the alpha source blend factor.

:type: int
)");
  uint32_t srcBlendAlpha = 0;

  DOCUMENT(R"(The D3DBLEND value for the alpha destination blend factor.

:type: int
)");
  uint32_t destBlendAlpha = 0;

  DOCUMENT(R"(The D3DBLENDOP value for the alpha blend operation.

:type: int
)");
  uint32_t blendOpAlpha = 0;

  DOCUMENT(R"(The color write mask. A combination of D3DCOLORWRITEENABLE flags.

:type: int
)");
  uint32_t writeMask = 0;

  DOCUMENT(R"(``True`` if the alpha test is enabled.

:type: bool
)");
  bool alphaTestEnable = false;

  DOCUMENT(R"(The D3DCMPFUNC value for the alpha test comparison function.

:type: int
)");
  uint32_t alphaFunc = 0;

  DOCUMENT(R"(The reference value for the alpha test.

:type: int
)");
  uint32_t alphaRef = 0;
};

DOCUMENT("Describes the D3D9 depth-stencil state.");
struct DepthStencilState
{
  DOCUMENT("");
  DepthStencilState() = default;
  DepthStencilState(const DepthStencilState &) = default;
  DepthStencilState &operator=(const DepthStencilState &) = default;

  DOCUMENT(R"(``True`` if depth testing is enabled.

:type: bool
)");
  bool depthEnable = false;

  DOCUMENT(R"(``True`` if depth writes are enabled.

:type: bool
)");
  bool depthWrite = false;

  DOCUMENT(R"(The D3DCMPFUNC value for the depth comparison function.

:type: int
)");
  uint32_t depthFunc = 0;

  DOCUMENT(R"(``True`` if stencil testing is enabled.

:type: bool
)");
  bool stencilEnable = false;

  DOCUMENT(R"(The stencil read mask.

:type: int
)");
  uint32_t stencilReadMask = 0;

  DOCUMENT(R"(The stencil write mask.

:type: int
)");
  uint32_t stencilWriteMask = 0;

  DOCUMENT(R"(The stencil reference value.

:type: int
)");
  uint32_t stencilRef = 0;

  DOCUMENT(R"(The D3DSTENCILOP value for stencil fail (front-facing).

:type: int
)");
  uint32_t stencilFail = 0;

  DOCUMENT(R"(The D3DSTENCILOP value for stencil pass with depth fail (front-facing).

:type: int
)");
  uint32_t stencilZFail = 0;

  DOCUMENT(R"(The D3DSTENCILOP value for stencil and depth pass (front-facing).

:type: int
)");
  uint32_t stencilPass = 0;

  DOCUMENT(R"(The D3DCMPFUNC value for the stencil comparison function (front-facing).

:type: int
)");
  uint32_t stencilFunc = 0;

  DOCUMENT(R"(``True`` if two-sided stencil testing is enabled.

:type: bool
)");
  bool twoSidedStencil = false;

  DOCUMENT(R"(The D3DSTENCILOP value for stencil fail (back-facing).

:type: int
)");
  uint32_t ccwStencilFail = 0;

  DOCUMENT(R"(The D3DSTENCILOP value for stencil pass with depth fail (back-facing).

:type: int
)");
  uint32_t ccwStencilZFail = 0;

  DOCUMENT(R"(The D3DSTENCILOP value for stencil and depth pass (back-facing).

:type: int
)");
  uint32_t ccwStencilPass = 0;

  DOCUMENT(R"(The D3DCMPFUNC value for the stencil comparison function (back-facing).

:type: int
)");
  uint32_t ccwStencilFunc = 0;
};

DOCUMENT("Describes the current state of the D3D9 output merger.");
struct OutputMerger
{
  DOCUMENT("");
  OutputMerger() = default;
  OutputMerger(const OutputMerger &) = default;
  OutputMerger &operator=(const OutputMerger &) = default;

  DOCUMENT(R"(The current blend state.

:type: D3D9BlendState
)");
  BlendState blendState;

  DOCUMENT(R"(The current depth-stencil state.

:type: D3D9DepthStencilState
)");
  DepthStencilState depthStencilState;

  DOCUMENT(R"(The bound render targets.

:type: List[ResourceId]
)");
  rdcarray<ResourceId> renderTargets;

  DOCUMENT(R"(The :class:`ResourceId` of the bound depth-stencil surface.

:type: ResourceId
)");
  ResourceId depthStencil;
};

DOCUMENT("The full current D3D9 pipeline state.");
struct State
{
#if !defined(RENDERDOC_EXPORTS)
  // disallow creation/copy of this object externally
  State() = delete;
  State(const State &) = delete;
#endif

  DOCUMENT(R"(The input assembly pipeline stage.

:type: D3D9InputAssembly
)");
  InputAssembly inputAssembly;

  DOCUMENT(R"(The vertex shader stage.

:type: D3D9Shader
)");
  Shader vertexShader;

  DOCUMENT(R"(The pixel shader stage.

:type: D3D9Shader
)");
  Shader pixelShader;

  DOCUMENT(R"(The fixed-function pipeline state.

:type: D3D9FixedFunction
)");
  FixedFunction fixedFunction;

  DOCUMENT(R"(The texture stage bindings.

:type: List[D3D9TextureStage]
)");
  rdcarray<TextureStage> textureStages;

  DOCUMENT(R"(The rasterizer render state.

:type: D3D9RenderState
)");
  RenderState rasterizer;

  DOCUMENT(R"(The output merger pipeline stage.

:type: D3D9OutputMerger
)");
  OutputMerger outputMerger;
};

};    // namespace D3D9Pipe

DECLARE_REFLECTION_STRUCT(D3D9Pipe::VertexElement);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::VertexBuffer);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::IndexBuffer);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::InputAssembly);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::ShaderConstant);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::Shader);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::Transform);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::Light);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::Material);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::TextureStageState);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::SamplerState);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::TextureStage);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::FixedFunction);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::RenderState);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::BlendState);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::DepthStencilState);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::OutputMerger);
DECLARE_REFLECTION_STRUCT(D3D9Pipe::State);
