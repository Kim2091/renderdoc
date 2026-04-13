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

#include "serialise/serialiser.h"

enum class D3D9Chunk : uint32_t
{
  DeviceInitialisation = (uint32_t)SystemChunk::FirstDriverChunk,
  SetResourceName,

  // Device creation / lifecycle
  CreateDevice,
  Reset,

  // Resource creation
  CreateTexture,
  CreateVolumeTexture,
  CreateCubeTexture,
  CreateVertexBuffer,
  CreateIndexBuffer,
  CreateRenderTarget,
  CreateDepthStencilSurface,
  CreateOffscreenPlainSurface,
  CreateVertexShader,
  CreatePixelShader,
  CreateVertexDeclaration,
  CreateStateBlock,
  CreateQuery,
  CreateAdditionalSwapChain,

  // Draw calls
  DrawPrimitive,
  DrawIndexedPrimitive,
  DrawPrimitiveUP,
  DrawIndexedPrimitiveUP,

  // Frame / scene
  Present,
  SwapChainPresent,
  BeginScene,
  EndScene,
  Clear,

  // Render state
  SetRenderState,
  SetSamplerState,
  SetTextureStageState,
  SetTransform,
  SetViewport,
  SetScissorRect,
  SetClipPlane,
  SetMaterial,
  SetLight,
  LightEnable,
  SetNPatchMode,

  // Shader state
  SetVertexShader,
  SetPixelShader,
  SetVertexDeclaration,
  SetFVF,
  SetVertexShaderConstantF,
  SetVertexShaderConstantI,
  SetVertexShaderConstantB,
  SetPixelShaderConstantF,
  SetPixelShaderConstantI,
  SetPixelShaderConstantB,

  // Resource binding
  SetTexture,
  SetStreamSource,
  SetStreamSourceFreq,
  SetIndices,
  SetRenderTarget,
  SetDepthStencilSurface,

  // Resource data
  LockRect,
  UnlockRect,
  LockBox,
  UnlockBox,
  LockVertexBuffer,
  UnlockVertexBuffer,
  LockIndexBuffer,
  UnlockIndexBuffer,
  UpdateSurface,
  UpdateTexture,
  StretchRect,
  ColorFill,
  GetRenderTargetData,
  GetFrontBufferData,

  // State blocks
  BeginStateBlock,
  EndStateBlock,
  StateBlockCapture,
  StateBlockApply,

  // Queries
  QueryIssue,
  QueryGetData,

  // Misc
  SetSoftwareVertexProcessing,
  SetDialogBoxMode,
  ValidateDevice,

  // Cursor (passthrough stubs but serialized for completeness)
  SetCursorProperties,
  SetCursorPosition,
  ShowCursor,

  // N-patch (passthrough stub)
  DrawRectPatch,
  DrawTriPatch,

  // Annotations / markers (D3DPERF_*)
  SetMarker,
  PushMarker,
  PopMarker,

  Max,
};

DECLARE_REFLECTION_ENUM(D3D9Chunk);
