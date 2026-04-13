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

#include "common/common.h"
#include "d3d9_common.h"

class D3D9TextRenderer
{
public:
  D3D9TextRenderer(IDirect3DDevice9 *device);
  ~D3D9TextRenderer();

  void SetOutputDimensions(int w, int h)
  {
    m_width = w;
    m_height = h;
  }

  void RenderText(float x, float y, const rdcstr &text);

private:
  void RenderTextInternal(float x, float y, const rdcstr &text);

  IDirect3DDevice9 *m_pDevice = NULL;

  int m_width = 1, m_height = 1;

  static const int FONT_TEX_WIDTH = 256;
  static const int FONT_TEX_HEIGHT = 128;
  static const int FONT_MAX_CHARS = 256;

  IDirect3DTexture9 *m_FontTexture = NULL;

  float m_CharAspect = 1.0f;
  float m_CharSize = 1.0f;

  // stb_truetype baked char data for UV lookups
  struct BakedChar
  {
    float x0, y0, x1, y1;    // UV coords in atlas (pixel coords)
    float xoff, yoff;         // glyph offset
    float xadvance;           // horizontal advance
  };

  static const int FIRST_CHAR = ' ' + 1;
  static const int LAST_CHAR = 127;
  static const int NUM_CHARS = LAST_CHAR - FIRST_CHAR;

  BakedChar m_CharData[NUM_CHARS];
  float m_MaxHeight = 0.0f;
};
