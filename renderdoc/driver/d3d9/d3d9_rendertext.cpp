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

#include "d3d9_rendertext.h"
#include "stb/stb_truetype.h"
#include "strings/string_utils.h"

struct D3D9FontVertex
{
  float x, y, z, rhw;
  DWORD color;
  float u, v;
};

#define D3D9_FONT_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

D3D9TextRenderer::D3D9TextRenderer(IDirect3DDevice9 *device) : m_pDevice(device)
{
  // Bake font atlas using stb_truetype
  rdcstr font = GetEmbeddedResource(sourcecodepro_ttf);
  byte *ttfdata = (byte *)font.c_str();

  const float pixelHeight = 20.0f;

  byte *buf = new byte[FONT_TEX_WIDTH * FONT_TEX_HEIGHT];

  stbtt_bakedchar chardata[NUM_CHARS];
  stbtt_BakeFontBitmap(ttfdata, 0, pixelHeight, buf, FONT_TEX_WIDTH, FONT_TEX_HEIGHT, FIRST_CHAR,
                        NUM_CHARS, chardata);

  m_CharSize = pixelHeight;
  m_CharAspect = chardata[0].xadvance / pixelHeight;

  stbtt_fontinfo f = {0};
  stbtt_InitFont(&f, ttfdata, 0);

  int ascent = 0;
  stbtt_GetFontVMetrics(&f, &ascent, NULL, NULL);
  m_MaxHeight = float(ascent) * stbtt_ScaleForPixelHeight(&f, pixelHeight);

  // Copy baked char data into our struct
  for(int i = 0; i < NUM_CHARS; i++)
  {
    m_CharData[i].x0 = (float)chardata[i].x0;
    m_CharData[i].y0 = (float)chardata[i].y0;
    m_CharData[i].x1 = (float)chardata[i].x1;
    m_CharData[i].y1 = (float)chardata[i].y1;
    m_CharData[i].xoff = chardata[i].xoff;
    m_CharData[i].yoff = chardata[i].yoff;
    m_CharData[i].xadvance = chardata[i].xadvance;
  }

  // Create D3D9 font texture (A8R8G8B8 — white text with alpha from font bitmap)
  HRESULT hr = m_pDevice->CreateTexture(FONT_TEX_WIDTH, FONT_TEX_HEIGHT, 1, 0, D3DFMT_A8R8G8B8,
                                         D3DPOOL_MANAGED, &m_FontTexture, NULL);

  if(FAILED(hr))
  {
    RDCERR("Failed to create D3D9 font texture HRESULT: %s", ToStr(hr).c_str());
    delete[] buf;
    return;
  }

  D3DLOCKED_RECT locked;
  hr = m_FontTexture->LockRect(0, &locked, NULL, 0);

  if(SUCCEEDED(hr))
  {
    for(int y = 0; y < FONT_TEX_HEIGHT; y++)
    {
      DWORD *dst = (DWORD *)((byte *)locked.pBits + y * locked.Pitch);
      byte *src = buf + y * FONT_TEX_WIDTH;

      for(int x = 0; x < FONT_TEX_WIDTH; x++)
        dst[x] = (src[x] << 24) | 0x00FFFFFF;    // alpha from bitmap, RGB white
    }

    m_FontTexture->UnlockRect(0);
  }
  else
  {
    RDCERR("Failed to lock font texture HRESULT: %s", ToStr(hr).c_str());
  }

  delete[] buf;
}

D3D9TextRenderer::~D3D9TextRenderer()
{
  SAFE_RELEASE(m_FontTexture);
}

void D3D9TextRenderer::RenderText(float x, float y, const rdcstr &text)
{
  if(!m_FontTexture)
    return;

  // Save all device state via state block
  IDirect3DStateBlock9 *stateBlock = NULL;
  HRESULT hr = m_pDevice->CreateStateBlock(D3DSBT_ALL, &stateBlock);
  if(FAILED(hr))
  {
    RDCERR("Failed to create state block for overlay HRESULT: %s", ToStr(hr).c_str());
    return;
  }

  // Set up render state for overlay
  m_pDevice->SetTexture(0, m_FontTexture);
  m_pDevice->SetVertexShader(NULL);
  m_pDevice->SetPixelShader(NULL);
  m_pDevice->SetFVF(D3D9_FONT_FVF);

  // Alpha blending
  m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  m_pDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

  // Disable depth/stencil/fog/culling/lighting
  m_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  m_pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  m_pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
  m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
  m_pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
  m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  m_pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
  m_pDevice->SetRenderState(D3DRS_CLIPPING, FALSE);
  m_pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);

  // Texture stage: modulate texture alpha with diffuse, use texture color
  m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

  // Sampler: bilinear filtering
  m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  m_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  m_pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

  // Render each line
  rdcarray<rdcstr> lines;
  split(text, lines, '\n');

  for(const rdcstr &line : lines)
  {
    RenderTextInternal(x, y, line);
    y += 1.0f;
  }

  // Restore all device state
  stateBlock->Apply();
  stateBlock->Release();
}

void D3D9TextRenderer::RenderTextInternal(float x, float y, const rdcstr &text)
{
  if(text.empty())
    return;

  size_t numChars = RDCMIN(text.size(), (size_t)FONT_MAX_CHARS);

  // Build vertex array: 6 vertices per character (2 triangles)
  D3D9FontVertex *verts = new D3D9FontVertex[numChars * 6];

  float startX = x * m_CharSize * m_CharAspect;
  float startY = y * m_CharSize;
  float curX = startX;

  const DWORD textColor = 0xFFFFFFFF;    // white, fully opaque

  float invTexW = 1.0f / (float)FONT_TEX_WIDTH;
  float invTexH = 1.0f / (float)FONT_TEX_HEIGHT;

  size_t vertIdx = 0;

  for(size_t i = 0; i < numChars; i++)
  {
    int ch = text[i] - FIRST_CHAR;

    if(ch < 0 || ch >= NUM_CHARS)
    {
      // Space or unprintable — advance cursor, emit degenerate quad
      curX += m_CharSize * m_CharAspect;

      for(int v = 0; v < 6; v++)
      {
        verts[vertIdx + v].x = 0.0f;
        verts[vertIdx + v].y = 0.0f;
        verts[vertIdx + v].z = 0.0f;
        verts[vertIdx + v].rhw = 1.0f;
        verts[vertIdx + v].color = 0;
        verts[vertIdx + v].u = 0.0f;
        verts[vertIdx + v].v = 0.0f;
      }
      vertIdx += 6;
      continue;
    }

    BakedChar &bc = m_CharData[ch];

    float glyphX = curX + bc.xoff;
    float glyphY = startY + bc.yoff + m_MaxHeight;
    float glyphW = bc.x1 - bc.x0;
    float glyphH = bc.y1 - bc.y0;

    float u0 = bc.x0 * invTexW;
    float v0 = bc.y0 * invTexH;
    float u1 = bc.x1 * invTexW;
    float v1 = bc.y1 * invTexH;

    // D3D9 half-pixel offset for correct texel-to-pixel mapping with XYZRHW
    float x0 = glyphX - 0.5f;
    float y0 = glyphY - 0.5f;
    float x1 = glyphX + glyphW - 0.5f;
    float y1 = glyphY + glyphH - 0.5f;

    // Triangle 1: top-left, top-right, bottom-left
    verts[vertIdx + 0] = {x0, y0, 0.0f, 1.0f, textColor, u0, v0};
    verts[vertIdx + 1] = {x1, y0, 0.0f, 1.0f, textColor, u1, v0};
    verts[vertIdx + 2] = {x0, y1, 0.0f, 1.0f, textColor, u0, v1};

    // Triangle 2: top-right, bottom-right, bottom-left
    verts[vertIdx + 3] = {x1, y0, 0.0f, 1.0f, textColor, u1, v0};
    verts[vertIdx + 4] = {x1, y1, 0.0f, 1.0f, textColor, u1, v1};
    verts[vertIdx + 5] = {x0, y1, 0.0f, 1.0f, textColor, u0, v1};

    curX += bc.xadvance;
    vertIdx += 6;
  }

  m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(vertIdx / 3), verts,
                              sizeof(D3D9FontVertex));

  delete[] verts;
}
