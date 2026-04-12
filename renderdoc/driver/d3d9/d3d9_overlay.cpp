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
#include "d3d9_device.h"

///////////////////////////////////////////////////////////////////////////
// RenderOverlay (replay-side)
//
// Renders a debug overlay on top of a given texture. This is used by the
// replay UI to show wireframe, depth, stencil, and other debug overlays.
//
// For now this is a minimal stub that returns an empty ResourceId, meaning
// no overlay is rendered. The full implementation will use D3D9DebugManager
// to draw colored quads, wireframe meshes, etc.
///////////////////////////////////////////////////////////////////////////

ResourceId D3D9Replay::RenderOverlay(ResourceId texid, FloatVector clearCol, DebugOverlay overlay,
                                     uint32_t eventId, const rdcarray<uint32_t> &passEvents)
{
  // TODO: implement debug overlays (wireframe, depth test, stencil, etc.)
  // For the RTX Remix analysis use case, overlays are nice-to-have but not critical.
  return ResourceId();
}
