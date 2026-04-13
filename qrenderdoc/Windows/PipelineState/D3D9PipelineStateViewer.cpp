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

#include "D3D9PipelineStateViewer.h"
#include <float.h>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QXmlStreamWriter>
#include "Code/Resources.h"
#include "Widgets/Extended/RDHeaderView.h"
#include "toolwindowmanager/ToolWindowManager.h"
#include "PipelineStateViewer.h"
#include "ui_D3D9PipelineStateViewer.h"

struct D3D9VBIBTag
{
  D3D9VBIBTag() { offset = 0; }
  D3D9VBIBTag(ResourceId i, uint64_t offs, QString f = QString())
  {
    id = i;
    offset = offs;
    format = f;
  }

  ResourceId id;
  uint64_t offset;
  QString format;
};

Q_DECLARE_METATYPE(D3D9VBIBTag);

// D3D9 enum-to-string helpers

static QString D3D9DeclTypeStr(uint32_t type)
{
  switch(type)
  {
    case 0: return lit("FLOAT1");
    case 1: return lit("FLOAT2");
    case 2: return lit("FLOAT3");
    case 3: return lit("FLOAT4");
    case 4: return lit("D3DCOLOR");
    case 5: return lit("UBYTE4");
    case 6: return lit("SHORT2");
    case 7: return lit("SHORT4");
    case 8: return lit("UBYTE4N");
    case 9: return lit("SHORT2N");
    case 10: return lit("SHORT4N");
    case 11: return lit("USHORT2N");
    case 12: return lit("USHORT4N");
    case 13: return lit("UDEC3");
    case 14: return lit("DEC3N");
    case 15: return lit("FLOAT16_2");
    case 16: return lit("FLOAT16_4");
    case 17: return lit("UNUSED");
    default: return QFormatStr("Unknown(%1)").arg(type);
  }
}

static QString D3D9DeclUsageStr(uint32_t usage)
{
  switch(usage)
  {
    case 0: return lit("POSITION");
    case 1: return lit("BLENDWEIGHT");
    case 2: return lit("BLENDINDICES");
    case 3: return lit("NORMAL");
    case 4: return lit("PSIZE");
    case 5: return lit("TEXCOORD");
    case 6: return lit("TANGENT");
    case 7: return lit("BINORMAL");
    case 8: return lit("TESSFACTOR");
    case 9: return lit("POSITIONT");
    case 10: return lit("COLOR");
    case 11: return lit("FOG");
    case 12: return lit("DEPTH");
    case 13: return lit("SAMPLE");
    default: return QFormatStr("Unknown(%1)").arg(usage);
  }
}

static QString D3D9DeclMethodStr(uint32_t method)
{
  switch(method)
  {
    case 0: return lit("DEFAULT");
    case 1: return lit("PARTIALU");
    case 2: return lit("PARTIALV");
    case 3: return lit("CROSSUV");
    case 4: return lit("UV");
    case 5: return lit("LOOKUP");
    case 6: return lit("LOOKUPPRESAMPLED");
    default: return QFormatStr("Unknown(%1)").arg(method);
  }
}

static QString D3D9TextureOpStr(uint32_t op)
{
  switch(op)
  {
    case 1: return lit("DISABLE");
    case 2: return lit("SELECTARG1");
    case 3: return lit("SELECTARG2");
    case 4: return lit("MODULATE");
    case 5: return lit("MODULATE2X");
    case 6: return lit("MODULATE4X");
    case 7: return lit("ADD");
    case 8: return lit("ADDSIGNED");
    case 9: return lit("ADDSIGNED2X");
    case 10: return lit("SUBTRACT");
    case 11: return lit("ADDSMOOTH");
    case 12: return lit("BLENDDIFFUSEALPHA");
    case 13: return lit("BLENDTEXTUREALPHA");
    case 14: return lit("BLENDFACTORALPHA");
    case 15: return lit("BLENDTEXTUREALPHAPM");
    case 16: return lit("BLENDCURRENTALPHA");
    case 17: return lit("PREMODULATE");
    case 18: return lit("MODULATEALPHA_ADDCOLOR");
    case 19: return lit("MODULATECOLOR_ADDALPHA");
    case 20: return lit("MODULATEINVALPHA_ADDCOLOR");
    case 21: return lit("MODULATEINVCOLOR_ADDALPHA");
    case 22: return lit("BUMPENVMAP");
    case 23: return lit("BUMPENVMAPLUMINANCE");
    case 24: return lit("DOTPRODUCT3");
    case 25: return lit("MULTIPLYADD");
    case 26: return lit("LERP");
    default: return QFormatStr("Unknown(%1)").arg(op);
  }
}

static QString D3D9TextureArgStr(uint32_t arg)
{
  QString modifier;
  uint32_t base = arg & 0x0F;

  if(arg & 0x10)
    modifier = lit("COMPLEMENT | ");
  if(arg & 0x20)
    modifier += lit("ALPHAREPLICATE | ");

  QString baseStr;
  switch(base)
  {
    case 0: baseStr = lit("DIFFUSE"); break;
    case 1: baseStr = lit("CURRENT"); break;
    case 2: baseStr = lit("TEXTURE"); break;
    case 3: baseStr = lit("TFACTOR"); break;
    case 4: baseStr = lit("SPECULAR"); break;
    case 5: baseStr = lit("TEMP"); break;
    case 6: baseStr = lit("CONSTANT"); break;
    default: baseStr = QFormatStr("Unknown(%1)").arg(base); break;
  }

  if(modifier.isEmpty())
    return baseStr;
  return modifier + baseStr;
}

static QString D3D9BlendStr(uint32_t blend)
{
  switch(blend)
  {
    case 1: return lit("ZERO");
    case 2: return lit("ONE");
    case 3: return lit("SRCCOLOR");
    case 4: return lit("INVSRCCOLOR");
    case 5: return lit("SRCALPHA");
    case 6: return lit("INVSRCALPHA");
    case 7: return lit("DESTALPHA");
    case 8: return lit("INVDESTALPHA");
    case 9: return lit("DESTCOLOR");
    case 10: return lit("INVDESTCOLOR");
    case 11: return lit("SRCALPHASAT");
    case 12: return lit("BOTHSRCALPHA");
    case 13: return lit("BOTHINVSRCALPHA");
    case 14: return lit("BLENDFACTOR");
    case 15: return lit("INVBLENDFACTOR");
    case 16: return lit("SRCCOLOR2");
    case 17: return lit("INVSRCCOLOR2");
    default: return QFormatStr("Unknown(%1)").arg(blend);
  }
}

static QString D3D9BlendOpStr(uint32_t op)
{
  switch(op)
  {
    case 1: return lit("ADD");
    case 2: return lit("SUBTRACT");
    case 3: return lit("REVSUBTRACT");
    case 4: return lit("MIN");
    case 5: return lit("MAX");
    default: return QFormatStr("Unknown(%1)").arg(op);
  }
}

static QString D3D9CmpFuncStr(uint32_t func)
{
  switch(func)
  {
    case 1: return lit("NEVER");
    case 2: return lit("LESS");
    case 3: return lit("EQUAL");
    case 4: return lit("LESSEQUAL");
    case 5: return lit("GREATER");
    case 6: return lit("NOTEQUAL");
    case 7: return lit("GREATEREQUAL");
    case 8: return lit("ALWAYS");
    default: return QFormatStr("Unknown(%1)").arg(func);
  }
}

static QString D3D9StencilOpStr(uint32_t op)
{
  switch(op)
  {
    case 1: return lit("KEEP");
    case 2: return lit("ZERO");
    case 3: return lit("REPLACE");
    case 4: return lit("INCRSAT");
    case 5: return lit("DECRSAT");
    case 6: return lit("INVERT");
    case 7: return lit("INCR");
    case 8: return lit("DECR");
    default: return QFormatStr("Unknown(%1)").arg(op);
  }
}

static QString D3D9FillModeStr(uint32_t mode)
{
  switch(mode)
  {
    case 1: return lit("POINT");
    case 2: return lit("WIREFRAME");
    case 3: return lit("SOLID");
    default: return QFormatStr("Unknown(%1)").arg(mode);
  }
}

static QString D3D9CullModeStr(uint32_t mode)
{
  switch(mode)
  {
    case 1: return lit("NONE");
    case 2: return lit("CW");
    case 3: return lit("CCW");
    default: return QFormatStr("Unknown(%1)").arg(mode);
  }
}

static QString D3D9FilterStr(uint32_t filter)
{
  switch(filter)
  {
    case 0: return lit("NONE");
    case 1: return lit("POINT");
    case 2: return lit("LINEAR");
    case 3: return lit("ANISOTROPIC");
    case 4: return lit("PYRAMIDALQUAD");
    case 5: return lit("GAUSSIANQUAD");
    case 6: return lit("CONVOLUTIONMONO");
    default: return QFormatStr("Unknown(%1)").arg(filter);
  }
}

static QString D3D9AddrStr(uint32_t addr)
{
  switch(addr)
  {
    case 1: return lit("WRAP");
    case 2: return lit("MIRROR");
    case 3: return lit("CLAMP");
    case 4: return lit("BORDER");
    case 5: return lit("MIRRORONCE");
    default: return QFormatStr("Unknown(%1)").arg(addr);
  }
}

static QString D3D9LightTypeStr(uint32_t type)
{
  switch(type)
  {
    case 1: return lit("POINT");
    case 2: return lit("SPOT");
    case 3: return lit("DIRECTIONAL");
    default: return QFormatStr("Unknown(%1)").arg(type);
  }
}

static QString D3D9TexTransformFlagsStr(uint32_t flags)
{
  uint32_t count = flags & 0xFF;
  bool projected = (flags & 256) != 0;

  QString str;
  switch(count)
  {
    case 0: str = lit("DISABLE"); break;
    case 1: str = lit("COUNT1"); break;
    case 2: str = lit("COUNT2"); break;
    case 3: str = lit("COUNT3"); break;
    case 4: str = lit("COUNT4"); break;
    default: str = QFormatStr("COUNT(%1)").arg(count); break;
  }

  if(projected)
    str += lit(" | PROJECTED");

  return str;
}

static QString FormatMatrix4x4(const rdcfixedarray<float, 16> &m)
{
  return QFormatStr("[ %1, %2, %3, %4 ]\n[ %5, %6, %7, %8 ]\n[ %9, %10, %11, %12 ]\n[ %13, %14, %15, %16 ]")
      .arg(m[0], 0, 'f', 4)
      .arg(m[1], 0, 'f', 4)
      .arg(m[2], 0, 'f', 4)
      .arg(m[3], 0, 'f', 4)
      .arg(m[4], 0, 'f', 4)
      .arg(m[5], 0, 'f', 4)
      .arg(m[6], 0, 'f', 4)
      .arg(m[7], 0, 'f', 4)
      .arg(m[8], 0, 'f', 4)
      .arg(m[9], 0, 'f', 4)
      .arg(m[10], 0, 'f', 4)
      .arg(m[11], 0, 'f', 4)
      .arg(m[12], 0, 'f', 4)
      .arg(m[13], 0, 'f', 4)
      .arg(m[14], 0, 'f', 4)
      .arg(m[15], 0, 'f', 4);
}

static QString FormatColor(const FloatVector &v)
{
  return QFormatStr("(%1, %2, %3, %4)")
      .arg(v.x, 0, 'f', 3)
      .arg(v.y, 0, 'f', 3)
      .arg(v.z, 0, 'f', 3)
      .arg(v.w, 0, 'f', 3);
}

D3D9PipelineStateViewer::D3D9PipelineStateViewer(ICaptureContext &ctx,
                                                 PipelineStateViewer &common, QWidget *parent)
    : QFrame(parent), ui(new Ui::D3D9PipelineStateViewer), m_Ctx(ctx), m_Common(common)
{
  ui->setupUi(this);

  const QIcon &action = Icons::action();
  const QIcon &action_hover = Icons::action_hover();

  RDLabel *shaderLabels[] = {ui->vsShader, ui->psShader};

  for(RDLabel *b : shaderLabels)
  {
    b->setAutoFillBackground(true);
    b->setBackgroundRole(QPalette::ToolTipBase);
    b->setForegroundRole(QPalette::ToolTipText);
    b->setMinimumSizeHint(QSize(250, 0));
  }

  // Configure PipelineFlowChart stages for D3D9
  ui->pipeFlow->setStages(
      {
          lit("IA"),
          lit("VS"),
          lit("RS"),
          lit("PS"),
          lit("OM"),
      },
      {
          tr("Input Assembler"),
          tr("Vertex Shader"),
          tr("Rasterizer"),
          tr("Pixel Shader"),
          tr("Output Merger"),
      });

  ui->pipeFlow->setStagesEnabled({true, true, true, true, true});

  m_Common.setMeshViewPixmap(ui->meshView);

  {
    QMenu *extensionsMenu = new QMenu(this);

    ui->extensions->setMenu(extensionsMenu);
    ui->extensions->setPopupMode(QToolButton::InstantPopup);

    QObject::connect(extensionsMenu, &QMenu::aboutToShow, [this, extensionsMenu]() {
      extensionsMenu->clear();
      m_Ctx.Extensions().MenuDisplaying(PanelMenu::PipelineStateViewer, extensionsMenu,
                                        ui->extensions, {});
    });
  }

  // IA Layouts tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->iaLayouts->setHeader(header);

    ui->iaLayouts->setColumns(
        {tr("Index"), tr("Semantic"), tr("Format"), tr("Stream"), tr("Offset"), tr("Method")});
    header->setColumnStretchHints({1, 3, 3, 1, 2, 2});

    ui->iaLayouts->setClearSelectionOnFocusLoss(true);
    ui->iaLayouts->setInstantTooltips(true);
  }

  // IA Buffers tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->iaBuffers->setHeader(header);

    ui->iaBuffers->setColumns(
        {tr("Slot"), tr("Buffer"), tr("Stride"), tr("Offset"), tr("Byte Length"), tr("Go")});
    header->setColumnStretchHints({1, 4, 2, 2, 3, -1});

    ui->iaBuffers->setClearSelectionOnFocusLoss(true);
    ui->iaBuffers->setInstantTooltips(true);
    ui->iaBuffers->setHoverIconColumn(5, action, action_hover);

    m_Common.SetupResourceView(ui->iaBuffers);
  }

  // VS/PS Resources trees
  RDTreeWidget *resources[] = {ui->vsResources, ui->psResources};
  for(RDTreeWidget *res : resources)
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    res->setHeader(header);

    res->setColumns({tr("Slot"), tr("Resource"), tr("Type"), tr("Width"), tr("Height"),
                     tr("Depth"), tr("Array Size"), tr("Format"), tr("Go")});
    header->setColumnStretchHints({2, 4, 2, 1, 1, 1, 1, 3, -1});

    res->setHoverIconColumn(8, action, action_hover);
    res->setClearSelectionOnFocusLoss(true);
    res->setInstantTooltips(true);

    m_Common.SetupResourceView(res);
  }

  // VS/PS Samplers trees
  RDTreeWidget *samplers[] = {ui->vsSamplers, ui->psSamplers};
  for(RDTreeWidget *samp : samplers)
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    samp->setHeader(header);

    samp->setColumns(
        {tr("Slot"), tr("Addressing"), tr("Filter"), tr("LOD Bias"), tr("Max Aniso")});
    header->setColumnStretchHints({1, 4, 4, 2, 2});

    samp->setClearSelectionOnFocusLoss(true);
    samp->setInstantTooltips(true);
  }

  // VS/PS Constant Buffers trees
  RDTreeWidget *cbuffers[] = {ui->vsCBuffers, ui->psCBuffers};
  for(RDTreeWidget *cb : cbuffers)
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    cb->setHeader(header);

    cb->setColumns({tr("Reg"), tr("X"), tr("Y"), tr("Z"), tr("W")});
    header->setColumnStretchHints({1, 2, 2, 2, 2});

    cb->setClearSelectionOnFocusLoss(true);
    cb->setInstantTooltips(true);
  }

  // FFP Texture Transforms tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->ffpTexTransforms->setHeader(header);

    ui->ffpTexTransforms->setColumns(
        {tr("Stage"), tr("Row 0"), tr("Row 1"), tr("Row 2"), tr("Row 3")});
    header->setColumnStretchHints({1, 4, 4, 4, 4});

    ui->ffpTexTransforms->setClearSelectionOnFocusLoss(true);
    ui->ffpTexTransforms->setInstantTooltips(true);
  }

  // FFP Lights tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->ffpLights->setHeader(header);

    ui->ffpLights->setColumns({tr("Index"), tr("Type"), tr("Enabled"), tr("Diffuse"),
                               tr("Specular"), tr("Ambient"), tr("Position"), tr("Direction"),
                               tr("Range"), tr("Atten0"), tr("Atten1"), tr("Atten2")});
    header->setColumnStretchHints({1, 2, 1, 3, 3, 3, 3, 3, 2, 2, 2, 2});

    ui->ffpLights->setClearSelectionOnFocusLoss(true);
    ui->ffpLights->setInstantTooltips(true);
  }

  // Texture Stages tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->texStages->setHeader(header);

    ui->texStages->setColumns({tr("Stage"), tr("Texture"), tr("Color Op"), tr("Color Arg1"),
                               tr("Color Arg2"), tr("Alpha Op"), tr("Alpha Arg1"),
                               tr("Alpha Arg2"), tr("TexCoord Index"), tr("Transform Flags"),
                               tr("Go")});
    header->setColumnStretchHints({1, 3, 2, 2, 2, 2, 2, 2, 1, 2, -1});

    ui->texStages->setHoverIconColumn(10, action, action_hover);
    ui->texStages->setClearSelectionOnFocusLoss(true);
    ui->texStages->setInstantTooltips(true);

    m_Common.SetupResourceView(ui->texStages);
  }

  // Texture Samplers tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->texSamplers->setHeader(header);

    ui->texSamplers->setColumns({tr("Stage"), tr("Address U"), tr("Address V"), tr("Address W"),
                                 tr("Mag Filter"), tr("Min Filter"), tr("Mip Filter"),
                                 tr("Max Aniso"), tr("LOD Bias"), tr("Max Mip"), tr("sRGB")});
    header->setColumnStretchHints({1, 2, 2, 2, 2, 2, 2, 1, 2, 1, 1});

    ui->texSamplers->setClearSelectionOnFocusLoss(true);
    ui->texSamplers->setInstantTooltips(true);
  }

  // Rasterizer Clip Planes tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->rsClipPlanes->setHeader(header);

    ui->rsClipPlanes->setColumns(
        {tr("Plane"), tr("Enabled"), tr("A"), tr("B"), tr("C"), tr("D")});
    header->setColumnStretchHints({1, 1, 2, 2, 2, 2});

    ui->rsClipPlanes->setClearSelectionOnFocusLoss(true);
    ui->rsClipPlanes->setInstantTooltips(true);
  }

  // OM Render Targets tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->omTargets->setHeader(header);

    ui->omTargets->setColumns({tr("Slot"), tr("Resource"), tr("Go")});
    header->setColumnStretchHints({1, 6, -1});

    ui->omTargets->setHoverIconColumn(2, action, action_hover);
    ui->omTargets->setClearSelectionOnFocusLoss(true);
    ui->omTargets->setInstantTooltips(true);

    m_Common.SetupResourceView(ui->omTargets);
  }

  // OM Blends tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->blends->setHeader(header);

    ui->blends->setColumns({tr("Enabled"), tr("Src Blend"), tr("Dst Blend"), tr("Blend Op"),
                            tr("Alpha Src"), tr("Alpha Dst"), tr("Alpha Op"), tr("Write Mask")});
    header->setColumnStretchHints({1, 2, 2, 2, 2, 2, 2, 2});

    ui->blends->setClearSelectionOnFocusLoss(true);
    ui->blends->setInstantTooltips(true);
  }

  // OM Stencils tree
  {
    RDHeaderView *header = new RDHeaderView(Qt::Horizontal, this);
    ui->stencils->setHeader(header);

    ui->stencils->setColumns(
        {tr("Face"), tr("Func"), tr("Fail Op"), tr("Depth Fail Op"), tr("Pass Op")});
    header->setColumnStretchHints({1, 2, 2, 2, 2});

    ui->stencils->setClearSelectionOnFocusLoss(true);
    ui->stencils->setInstantTooltips(true);
  }

  // Set fonts for all tree widgets and shader labels
  ui->iaLayouts->setFont(Formatter::PreferredFont());
  ui->iaBuffers->setFont(Formatter::PreferredFont());

  ui->vsShader->setFont(Formatter::PreferredFont());
  ui->vsResources->setFont(Formatter::PreferredFont());
  ui->vsSamplers->setFont(Formatter::PreferredFont());
  ui->vsCBuffers->setFont(Formatter::PreferredFont());

  ui->psShader->setFont(Formatter::PreferredFont());
  ui->psResources->setFont(Formatter::PreferredFont());
  ui->psSamplers->setFont(Formatter::PreferredFont());
  ui->psCBuffers->setFont(Formatter::PreferredFont());

  ui->ffpTexTransforms->setFont(Formatter::PreferredFont());
  ui->ffpLights->setFont(Formatter::PreferredFont());

  ui->texStages->setFont(Formatter::PreferredFont());
  ui->texSamplers->setFont(Formatter::PreferredFont());

  ui->rsClipPlanes->setFont(Formatter::PreferredFont());

  ui->omTargets->setFont(Formatter::PreferredFont());
  ui->blends->setFont(Formatter::PreferredFont());
  ui->stencils->setFont(Formatter::PreferredFont());

  ui->omDepth->setFont(Formatter::PreferredFont());
  ui->omDepth->setAutoFillBackground(true);
  ui->omDepth->setBackgroundRole(QPalette::ToolTipBase);
  ui->omDepth->setForegroundRole(QPalette::ToolTipText);
  ui->omDepth->setMinimumSizeHint(QSize(100, 0));

  ui->blendFactor->setFont(Formatter::PreferredFont());
  ui->blendFactor->setAutoFillBackground(true);
  ui->blendFactor->setBackgroundRole(QPalette::ToolTipBase);
  ui->blendFactor->setForegroundRole(QPalette::ToolTipText);

  clearState();
}

D3D9PipelineStateViewer::~D3D9PipelineStateViewer()
{
  delete ui;
}

void D3D9PipelineStateViewer::OnCaptureLoaded()
{
}

void D3D9PipelineStateViewer::OnCaptureClosed()
{
  clearState();
}

void D3D9PipelineStateViewer::OnEventChanged(uint32_t eventId)
{
  m_Ctx.Replay().AsyncInvoke([this](IReplayController *r) {
    GUIInvoke::call(this, [this]() { setState(); });
  });
}

void D3D9PipelineStateViewer::SelectPipelineStage(PipelineStage stage)
{
  // Map PipelineStage enum to our 5-stage flow chart indices
  // D3D9 flow: 0=IA, 1=VS, 2=RS, 3=PS, 4=OM
  switch(stage)
  {
    case PipelineStage::VertexInput: ui->pipeFlow->setSelectedStage(0); break;
    case PipelineStage::VertexShader: ui->pipeFlow->setSelectedStage(1); break;
    case PipelineStage::Rasterizer: ui->pipeFlow->setSelectedStage(2); break;
    case PipelineStage::PixelShader: ui->pipeFlow->setSelectedStage(3); break;
    case PipelineStage::ColorDepthOutput:
    case PipelineStage::SampleMask: ui->pipeFlow->setSelectedStage(4); break;
    default: break;
  }
}

ResourceId D3D9PipelineStateViewer::GetResource(RDTreeWidgetItem *item)
{
  QVariant tag = item->tag();

  if(tag.canConvert<ResourceId>())
  {
    return tag.value<ResourceId>();
  }
  else if(tag.canConvert<D3D9VBIBTag>())
  {
    D3D9VBIBTag buf = tag.value<D3D9VBIBTag>();
    return buf.id;
  }

  return ResourceId();
}

void D3D9PipelineStateViewer::on_showUnused_toggled(bool checked)
{
  setState();
}

void D3D9PipelineStateViewer::on_showEmpty_toggled(bool checked)
{
  setState();
}

void D3D9PipelineStateViewer::on_exportHTML_clicked()
{
  if(!m_Ctx.IsCaptureLoaded())
    return;

  QXmlStreamWriter *xmlptr = m_Common.beginHTMLExport();

  if(xmlptr)
  {
    QXmlStreamWriter &xml = *xmlptr;

    const QStringList &stageNames = ui->pipeFlow->stageNames();
    const QStringList &stageAbbrevs = ui->pipeFlow->stageAbbreviations();

    int stage = 0;
    for(const QString &sn : stageNames)
    {
      xml.writeStartElement(lit("div"));
      xml.writeStartElement(lit("a"));
      xml.writeAttribute(lit("name"), stageAbbrevs[stage]);
      xml.writeEndElement();
      xml.writeEndElement();

      xml.writeStartElement(lit("div"));
      xml.writeAttribute(lit("class"), lit("stage"));

      xml.writeStartElement(lit("h1"));
      xml.writeCharacters(sn);
      xml.writeEndElement();

      // Simplified: export the full pipeline state in one block
      if(stage == 0 && m_Ctx.CurD3D9PipelineState())
        exportHTML(xml, *m_Ctx.CurD3D9PipelineState());

      xml.writeEndElement();
      stage++;
    }

    m_Common.endHTMLExport(xmlptr);
  }
}

void D3D9PipelineStateViewer::on_meshView_clicked()
{
  if(!m_Ctx.HasMeshPreview())
    m_Ctx.ShowMeshPreview();
  ToolWindowManager::raiseToolWindow(m_Ctx.GetMeshPreview()->Widget());
}

void D3D9PipelineStateViewer::on_pipeFlow_stageSelected(int index)
{
  // Map 5-stage flow chart indices to 7 tab indices
  // Flow: 0=IA, 1=VS, 2=RS, 3=PS, 4=OM
  // Tabs: 0=IA, 1=VS, 2=FFP, 3=TexStages, 4=RS, 5=PS, 6=OM
  switch(index)
  {
    case 0: ui->stagesTabs->setCurrentIndex(0); break;    // IA
    case 1: ui->stagesTabs->setCurrentIndex(1); break;    // VS
    case 2: ui->stagesTabs->setCurrentIndex(4); break;    // RS
    case 3: ui->stagesTabs->setCurrentIndex(5); break;    // PS
    case 4: ui->stagesTabs->setCurrentIndex(6); break;    // OM
    default: break;
  }
}

void D3D9PipelineStateViewer::setState()
{
  if(!m_Ctx.IsCaptureLoaded())
  {
    clearState();
    return;
  }

  const D3D9Pipe::State *statePtr = m_Ctx.CurD3D9PipelineState();
  if(!statePtr)
  {
    clearState();
    return;
  }

  const D3D9Pipe::State &state = *statePtr;

  const QPixmap &tick = Pixmaps::tick(this);
  const QPixmap &cross = Pixmaps::cross(this);

  ////////////////////////////////////////////////
  // Input Assembly

  int vs = 0;

  vs = ui->iaLayouts->verticalScrollBar()->value();
  ui->iaLayouts->beginUpdate();
  ui->iaLayouts->clear();
  {
    for(int i = 0; i < state.inputAssembly.vertexElements.count(); i++)
    {
      const D3D9Pipe::VertexElement &el = state.inputAssembly.vertexElements[i];

      QString semantic = QFormatStr("%1%2").arg(D3D9DeclUsageStr(el.usage)).arg(el.usageIndex);

      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {i, semantic, D3D9DeclTypeStr(el.type), el.stream,
           Formatter::HumanFormat(el.offset, Formatter::OffsetSize), D3D9DeclMethodStr(el.method)});

      node->setTag(i);

      ui->iaLayouts->addTopLevelItem(node);
    }
  }
  ui->iaLayouts->clearSelection();
  ui->iaLayouts->endUpdate();
  ui->iaLayouts->verticalScrollBar()->setValue(vs);

  // FVF
  if(state.inputAssembly.FVF != 0)
    ui->iaFVF->setText(QFormatStr("0x%1").arg(state.inputAssembly.FVF, 8, 16, QLatin1Char('0')));
  else
    ui->iaFVF->setText(tr("Not set (using vertex declaration)"));

  // Buffers
  m_VBNodes.clear();
  m_EmptyNodes.clear();

  vs = ui->iaBuffers->verticalScrollBar()->value();
  ui->iaBuffers->beginUpdate();
  ui->iaBuffers->clear();

  // Index buffer
  {
    const D3D9Pipe::IndexBuffer &ib = state.inputAssembly.indexBuffer;
    bool filledSlot = (ib.resourceId != ResourceId());

    if(filledSlot || ui->showEmpty->isChecked())
    {
      uint64_t length = 0;
      BufferDescription *buf = m_Ctx.GetBuffer(ib.resourceId);
      if(buf)
        length = buf->length;

      QString formatStr;
      if(ib.byteStride == 2)
        formatStr = lit("16-bit");
      else if(ib.byteStride == 4)
        formatStr = lit("32-bit");
      else
        formatStr = QFormatStr("%1-byte").arg(ib.byteStride);

      RDTreeWidgetItem *node = NULL;

      if(filledSlot)
      {
        node = new RDTreeWidgetItem(
            {tr("Index"), m_Ctx.GetResourceName(ib.resourceId),
             formatStr, lit("0"),
             Formatter::HumanFormat(length, Formatter::OffsetSize), QString()});
      }
      else
      {
        node = new RDTreeWidgetItem(
            {tr("Index"), tr("No Buffer Set"), lit("-"), lit("-"), lit("-"), QString()});
      }

      node->setTag(QVariant::fromValue(ib.resourceId));

      if(!filledSlot)
      {
        setEmptyRow(node);
        m_EmptyNodes.push_back(node);
      }

      ui->iaBuffers->addTopLevelItem(node);
    }
  }

  // Vertex buffers
  for(int i = 0; i < state.inputAssembly.vertexBuffers.count(); i++)
  {
    const D3D9Pipe::VertexBuffer &vb = state.inputAssembly.vertexBuffers[i];

    bool filledSlot = (vb.resourceId != ResourceId());
    bool usedSlot = filledSlot;    // For D3D9, consider all bound buffers as used

    if(showNode(usedSlot, filledSlot))
    {
      uint64_t length = 0;
      BufferDescription *buf = m_Ctx.GetBuffer(vb.resourceId);
      if(buf)
        length = buf->length;

      RDTreeWidgetItem *node = NULL;

      if(filledSlot)
      {
        node = new RDTreeWidgetItem(
            {i, m_Ctx.GetResourceName(vb.resourceId),
             Formatter::HumanFormat(vb.byteStride, Formatter::OffsetSize),
             Formatter::HumanFormat(vb.byteOffset, Formatter::OffsetSize),
             Formatter::HumanFormat(length, Formatter::OffsetSize), QString()});
      }
      else
      {
        node = new RDTreeWidgetItem(
            {i, tr("No Buffer Set"), lit("-"), lit("-"), lit("-"), QString()});
      }

      node->setTag(QVariant::fromValue(
          D3D9VBIBTag(vb.resourceId, vb.byteOffset, m_Common.GetVBufferFormatString(i))));

      if(!filledSlot)
      {
        setEmptyRow(node);
        m_EmptyNodes.push_back(node);
      }

      if(!usedSlot)
        setInactiveRow(node);

      m_VBNodes.push_back(node);

      ui->iaBuffers->addTopLevelItem(node);
    }
    else
    {
      m_VBNodes.push_back(NULL);
    }
  }
  ui->iaBuffers->clearSelection();
  ui->iaBuffers->endUpdate();
  ui->iaBuffers->verticalScrollBar()->setValue(vs);

  ////////////////////////////////////////////////
  // Vertex Shader

  setShaderState(state.vertexShader, ui->vsShader, ui->vsResources, ui->vsSamplers, ui->vsCBuffers,
                 state.textureStages);

  ////////////////////////////////////////////////
  // Pixel Shader

  setShaderState(state.pixelShader, ui->psShader, ui->psResources, ui->psSamplers, ui->psCBuffers,
                 state.textureStages);

  ////////////////////////////////////////////////
  // Fixed Function

  const D3D9Pipe::FixedFunction &ff = state.fixedFunction;

  ui->ffpWorldMatrix->setText(FormatMatrix4x4(ff.transforms.world));
  ui->ffpViewMatrix->setText(FormatMatrix4x4(ff.transforms.view));
  ui->ffpProjMatrix->setText(FormatMatrix4x4(ff.transforms.projection));

  // Texture transforms
  vs = ui->ffpTexTransforms->verticalScrollBar()->value();
  ui->ffpTexTransforms->beginUpdate();
  ui->ffpTexTransforms->clear();
  for(int i = 0; i < 8; i++)
  {
    const rdcfixedarray<float, 16> &m = ff.transforms.texture[i];

    RDTreeWidgetItem *node = new RDTreeWidgetItem(
        {i,
         QFormatStr("%1, %2, %3, %4").arg(m[0], 0, 'f', 4).arg(m[1], 0, 'f', 4).arg(m[2], 0, 'f', 4).arg(m[3], 0, 'f', 4),
         QFormatStr("%1, %2, %3, %4").arg(m[4], 0, 'f', 4).arg(m[5], 0, 'f', 4).arg(m[6], 0, 'f', 4).arg(m[7], 0, 'f', 4),
         QFormatStr("%1, %2, %3, %4").arg(m[8], 0, 'f', 4).arg(m[9], 0, 'f', 4).arg(m[10], 0, 'f', 4).arg(m[11], 0, 'f', 4),
         QFormatStr("%1, %2, %3, %4").arg(m[12], 0, 'f', 4).arg(m[13], 0, 'f', 4).arg(m[14], 0, 'f', 4).arg(m[15], 0, 'f', 4)});

    // Check if this is an identity matrix (unused)
    bool isIdentity = (m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f && m[15] == 1.0f &&
                       m[1] == 0.0f && m[2] == 0.0f && m[3] == 0.0f && m[4] == 0.0f &&
                       m[6] == 0.0f && m[7] == 0.0f && m[8] == 0.0f && m[9] == 0.0f &&
                       m[11] == 0.0f && m[12] == 0.0f && m[13] == 0.0f && m[14] == 0.0f);

    if(isIdentity)
      setInactiveRow(node);

    ui->ffpTexTransforms->addTopLevelItem(node);
  }
  ui->ffpTexTransforms->clearSelection();
  ui->ffpTexTransforms->endUpdate();
  ui->ffpTexTransforms->verticalScrollBar()->setValue(vs);

  // Lights
  vs = ui->ffpLights->verticalScrollBar()->value();
  ui->ffpLights->beginUpdate();
  ui->ffpLights->clear();
  for(int i = 0; i < ff.lights.count(); i++)
  {
    const D3D9Pipe::Light &l = ff.lights[i];

    RDTreeWidgetItem *node = new RDTreeWidgetItem(
        {i, D3D9LightTypeStr(l.type), l.enabled ? tr("True") : tr("False"),
         FormatColor(l.diffuse), FormatColor(l.specular), FormatColor(l.ambient),
         QFormatStr("(%1, %2, %3)")
             .arg(l.position.x, 0, 'f', 3)
             .arg(l.position.y, 0, 'f', 3)
             .arg(l.position.z, 0, 'f', 3),
         QFormatStr("(%1, %2, %3)")
             .arg(l.direction.x, 0, 'f', 3)
             .arg(l.direction.y, 0, 'f', 3)
             .arg(l.direction.z, 0, 'f', 3),
         Formatter::Format((double)l.range),
         Formatter::Format((double)l.attenuation0),
         Formatter::Format((double)l.attenuation1),
         Formatter::Format((double)l.attenuation2)});

    if(!l.enabled)
      setInactiveRow(node);

    ui->ffpLights->addTopLevelItem(node);
  }
  ui->ffpLights->clearSelection();
  ui->ffpLights->endUpdate();
  ui->ffpLights->verticalScrollBar()->setValue(vs);

  // Material
  ui->ffpDiffuse->setText(FormatColor(ff.material.diffuse));
  ui->ffpSpecular->setText(FormatColor(ff.material.specular));
  ui->ffpAmbient->setText(FormatColor(ff.material.ambient));
  ui->ffpEmissive->setText(FormatColor(ff.material.emissive));
  ui->ffpPower->setText(Formatter::Format((double)ff.material.power));

  // Misc FFP state
  ui->ffpLightingEnabled->setPixmap(ff.lightingEnabled ? tick : cross);
  ui->ffpFogEnabled->setPixmap(ff.fogEnabled ? tick : cross);

  ////////////////////////////////////////////////
  // Texture Stages

  vs = ui->texStages->verticalScrollBar()->value();
  ui->texStages->beginUpdate();
  ui->texStages->clear();
  for(int i = 0; i < state.textureStages.count(); i++)
  {
    const D3D9Pipe::TextureStage &ts = state.textureStages[i];

    bool filledSlot = (ts.texture != ResourceId());
    bool usedSlot = (ts.stageState.colorOp != 1);    // 1 = D3DTOP_DISABLE

    if(showNode(usedSlot, filledSlot))
    {
      QString texName = filledSlot ? m_Ctx.GetResourceName(ts.texture) : tr("Unbound");

      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {i, texName, D3D9TextureOpStr(ts.stageState.colorOp),
           D3D9TextureArgStr(ts.stageState.colorArg1), D3D9TextureArgStr(ts.stageState.colorArg2),
           D3D9TextureOpStr(ts.stageState.alphaOp), D3D9TextureArgStr(ts.stageState.alphaArg1),
           D3D9TextureArgStr(ts.stageState.alphaArg2), ts.stageState.texCoordIndex,
           D3D9TexTransformFlagsStr(ts.stageState.textureTransformFlags), QString()});

      node->setTag(QVariant::fromValue(ts.texture));

      if(!usedSlot)
        setInactiveRow(node);

      if(!filledSlot)
        setEmptyRow(node);

      ui->texStages->addTopLevelItem(node);
    }
  }
  ui->texStages->clearSelection();
  ui->texStages->endUpdate();
  ui->texStages->verticalScrollBar()->setValue(vs);

  // Texture Samplers
  vs = ui->texSamplers->verticalScrollBar()->value();
  ui->texSamplers->beginUpdate();
  ui->texSamplers->clear();
  for(int i = 0; i < state.textureStages.count(); i++)
  {
    const D3D9Pipe::SamplerState &samp = state.textureStages[i].sampler;

    // Show sampler row if the corresponding texture stage is active
    bool usedSlot = (i < state.textureStages.count() && state.textureStages[i].stageState.colorOp != 1);
    bool filledSlot = (samp.magFilter != 0 || samp.minFilter != 0 || samp.addressU != 0);

    if(showNode(usedSlot, filledSlot))
    {
      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {i, D3D9AddrStr(samp.addressU), D3D9AddrStr(samp.addressV), D3D9AddrStr(samp.addressW),
           D3D9FilterStr(samp.magFilter), D3D9FilterStr(samp.minFilter),
           D3D9FilterStr(samp.mipFilter), samp.maxAnisotropy,
           Formatter::Format((double)samp.mipLODBias), samp.maxMipLevel,
           samp.sRGB ? tr("True") : tr("False")});

      if(!usedSlot)
        setInactiveRow(node);

      ui->texSamplers->addTopLevelItem(node);
    }
  }
  ui->texSamplers->clearSelection();
  ui->texSamplers->endUpdate();
  ui->texSamplers->verticalScrollBar()->setValue(vs);

  ////////////////////////////////////////////////
  // Rasterizer

  const D3D9Pipe::RenderState &rs = state.rasterizer;

  ui->fillMode->setText(D3D9FillModeStr(rs.fillMode));
  ui->cullMode->setText(D3D9CullModeStr(rs.cullMode));
  ui->depthBias->setText(Formatter::Format((double)rs.depthBias));
  ui->slopeScaledDepthBias->setText(Formatter::Format((double)rs.slopeScaledDepthBias));
  ui->rsScissorEnabled->setPixmap(rs.scissorEnable ? tick : cross);
  ui->rsMultisampleEnabled->setPixmap(rs.multisampleEnable ? tick : cross);
  ui->rsAntialiasedLineEnabled->setPixmap(rs.antialiasedLineEnable ? tick : cross);

  // Clip planes
  vs = ui->rsClipPlanes->verticalScrollBar()->value();
  ui->rsClipPlanes->beginUpdate();
  ui->rsClipPlanes->clear();
  for(int i = 0; i < 6; i++)
  {
    bool enabled = (rs.clipPlaneEnable & (1 << i)) != 0;

    RDTreeWidgetItem *node = new RDTreeWidgetItem(
        {i, enabled ? tr("True") : tr("False"),
         Formatter::Format((double)rs.clipPlanes[i][0]),
         Formatter::Format((double)rs.clipPlanes[i][1]),
         Formatter::Format((double)rs.clipPlanes[i][2]),
         Formatter::Format((double)rs.clipPlanes[i][3])});

    if(!enabled)
      setInactiveRow(node);

    ui->rsClipPlanes->addTopLevelItem(node);
  }
  ui->rsClipPlanes->clearSelection();
  ui->rsClipPlanes->endUpdate();
  ui->rsClipPlanes->verticalScrollBar()->setValue(vs);

  ////////////////////////////////////////////////
  // Output Merger

  const D3D9Pipe::OutputMerger &om = state.outputMerger;

  // Render targets
  vs = ui->omTargets->verticalScrollBar()->value();
  ui->omTargets->beginUpdate();
  ui->omTargets->clear();
  for(int i = 0; i < om.renderTargets.count(); i++)
  {
    const ResourceId &rt = om.renderTargets[i];
    bool filledSlot = (rt != ResourceId());

    if(filledSlot || ui->showEmpty->isChecked())
    {
      RDTreeWidgetItem *node = NULL;

      if(filledSlot)
        node = new RDTreeWidgetItem({i, m_Ctx.GetResourceName(rt), QString()});
      else
        node = new RDTreeWidgetItem({i, tr("Unbound"), QString()});

      node->setTag(QVariant::fromValue(rt));

      if(!filledSlot)
        setEmptyRow(node);

      ui->omTargets->addTopLevelItem(node);
    }
  }
  ui->omTargets->clearSelection();
  ui->omTargets->endUpdate();
  ui->omTargets->verticalScrollBar()->setValue(vs);

  // Depth surface
  if(om.depthStencil != ResourceId())
    ui->omDepth->setText(m_Ctx.GetResourceName(om.depthStencil));
  else
    ui->omDepth->setText(tr("Unbound"));

  // Blend state
  const D3D9Pipe::BlendState &bs = om.blendState;

  vs = ui->blends->verticalScrollBar()->value();
  ui->blends->beginUpdate();
  ui->blends->clear();
  {
    QString writeMaskStr = QFormatStr("%1%2%3%4")
                               .arg((bs.writeMask & 0x1) == 0 ? lit("_") : lit("R"))
                               .arg((bs.writeMask & 0x2) == 0 ? lit("_") : lit("G"))
                               .arg((bs.writeMask & 0x4) == 0 ? lit("_") : lit("B"))
                               .arg((bs.writeMask & 0x8) == 0 ? lit("_") : lit("A"));

    RDTreeWidgetItem *node = new RDTreeWidgetItem(
        {bs.alphaBlendEnable ? tr("True") : tr("False"), D3D9BlendStr(bs.srcBlend),
         D3D9BlendStr(bs.destBlend), D3D9BlendOpStr(bs.blendOp),
         bs.separateAlphaBlendEnable ? D3D9BlendStr(bs.srcBlendAlpha) : lit("-"),
         bs.separateAlphaBlendEnable ? D3D9BlendStr(bs.destBlendAlpha) : lit("-"),
         bs.separateAlphaBlendEnable ? D3D9BlendOpStr(bs.blendOpAlpha) : lit("-"), writeMaskStr});

    if(!bs.alphaBlendEnable)
      setInactiveRow(node);

    ui->blends->addTopLevelItem(node);
  }
  ui->blends->clearSelection();
  ui->blends->endUpdate();
  ui->blends->verticalScrollBar()->setValue(vs);

  // Blend factor label - D3D9 doesn't have a direct blend factor, but we populate alpha test info
  if(bs.alphaTestEnable)
    ui->blendFactor->setText(QFormatStr("Alpha Test: %1 ref=%2")
                                 .arg(D3D9CmpFuncStr(bs.alphaFunc))
                                 .arg(bs.alphaRef));
  else
    ui->blendFactor->setText(tr("Alpha Test: Disabled"));

  ui->alphaToCoverage->setPixmap(bs.alphaTestEnable ? tick : cross);
  ui->separateAlpha->setPixmap(bs.separateAlphaBlendEnable ? tick : cross);

  // Depth-stencil state
  const D3D9Pipe::DepthStencilState &ds = om.depthStencilState;

  ui->depthEnabled->setPixmap(ds.depthEnable ? tick : cross);
  ui->depthFunc->setText(ds.depthEnable ? D3D9CmpFuncStr(ds.depthFunc) : tr("Disabled"));
  ui->depthWrite->setPixmap(ds.depthWrite ? tick : cross);
  ui->stencilEnabled->setPixmap(ds.stencilEnable ? tick : cross);

  // Stencils
  ui->stencils->beginUpdate();
  ui->stencils->clear();

  ui->stencils->addTopLevelItem(new RDTreeWidgetItem(
      {tr("Front"), D3D9CmpFuncStr(ds.stencilFunc), D3D9StencilOpStr(ds.stencilFail),
       D3D9StencilOpStr(ds.stencilZFail), D3D9StencilOpStr(ds.stencilPass)}));

  if(ds.twoSidedStencil)
  {
    ui->stencils->addTopLevelItem(new RDTreeWidgetItem(
        {tr("Back"), D3D9CmpFuncStr(ds.ccwStencilFunc), D3D9StencilOpStr(ds.ccwStencilFail),
         D3D9StencilOpStr(ds.ccwStencilZFail), D3D9StencilOpStr(ds.ccwStencilPass)}));
  }

  ui->stencils->clearSelection();
  ui->stencils->endUpdate();

  ////////////////////////////////////////////////
  // Pipeline flow chart enable/disable stages

  ui->pipeFlow->setStagesEnabled(
      {true, state.vertexShader.resourceId != ResourceId(), true,
       state.pixelShader.resourceId != ResourceId(), true});
}

void D3D9PipelineStateViewer::setShaderState(const D3D9Pipe::Shader &stage, RDLabel *shader,
                                             RDTreeWidget *resources, RDTreeWidget *samplers,
                                             RDTreeWidget *cbuffers,
                                             const rdcarray<D3D9Pipe::TextureStage> &texStages)
{
  if(stage.resourceId != ResourceId())
    shader->setText(m_Ctx.GetResourceName(stage.resourceId));
  else
    shader->setText(tr("No Shader (Fixed Function)"));

  // Resources - populate from texture stage bindings
  int vs = resources->verticalScrollBar()->value();
  resources->beginUpdate();
  resources->clear();
  for(int i = 0; i < texStages.count(); i++)
  {
    const D3D9Pipe::TextureStage &ts = texStages[i];
    bool filledSlot = (ts.texture != ResourceId());
    bool usedSlot = filledSlot;

    if(showNode(usedSlot, filledSlot))
    {
      RDTreeWidgetItem *node = NULL;

      if(filledSlot)
      {
        TextureDescription *tex = m_Ctx.GetTexture(ts.texture);

        QString typeName = lit("Unknown");
        uint32_t w = 0, h = 0, d = 0, arr = 0;
        QString fmtName;

        if(tex)
        {
          typeName = ToQStr(tex->type);
          w = tex->width;
          h = tex->height;
          d = tex->depth;
          arr = tex->arraysize;
          fmtName = tex->format.Name();
        }

        node = new RDTreeWidgetItem(
            {i, m_Ctx.GetResourceName(ts.texture), typeName, w, h, d, arr, fmtName, QString()});
      }
      else
      {
        node = new RDTreeWidgetItem(
            {i, tr("Unbound"), QString(), QString(), QString(), QString(), QString(), QString(),
             QString()});
      }

      node->setTag(QVariant::fromValue(ts.texture));

      if(!filledSlot)
        setEmptyRow(node);

      if(!usedSlot)
        setInactiveRow(node);

      resources->addTopLevelItem(node);
    }
  }
  resources->clearSelection();
  resources->endUpdate();
  resources->verticalScrollBar()->setValue(vs);

  // Samplers - populate from texture stage sampler state
  vs = samplers->verticalScrollBar()->value();
  samplers->beginUpdate();
  samplers->clear();
  for(int i = 0; i < texStages.count(); i++)
  {
    const D3D9Pipe::SamplerState &samp = texStages[i].sampler;
    bool usedSlot = (texStages[i].texture != ResourceId());
    bool filledSlot = (samp.addressU != 0 || samp.magFilter != 0);

    if(showNode(usedSlot, filledSlot))
    {
      QString addr = QFormatStr("%1, %2, %3")
                         .arg(D3D9AddrStr(samp.addressU))
                         .arg(D3D9AddrStr(samp.addressV))
                         .arg(D3D9AddrStr(samp.addressW));
      QString filter = QFormatStr("Mag: %1 Min: %2 Mip: %3")
                           .arg(D3D9FilterStr(samp.magFilter))
                           .arg(D3D9FilterStr(samp.minFilter))
                           .arg(D3D9FilterStr(samp.mipFilter));

      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {i, addr, filter, Formatter::Format((double)samp.mipLODBias), samp.maxAnisotropy});

      if(!usedSlot)
        setInactiveRow(node);

      samplers->addTopLevelItem(node);
    }
  }
  samplers->clearSelection();
  samplers->endUpdate();
  samplers->verticalScrollBar()->setValue(vs);

  // Constant Registers - float constants
  vs = cbuffers->verticalScrollBar()->value();
  cbuffers->beginUpdate();
  cbuffers->clear();

  int numFloatRegs = stage.constants.floatConstants.count() / 4;
  for(int i = 0; i < numFloatRegs; i++)
  {
    float x = stage.constants.floatConstants[i * 4 + 0];
    float y = stage.constants.floatConstants[i * 4 + 1];
    float z = stage.constants.floatConstants[i * 4 + 2];
    float w = stage.constants.floatConstants[i * 4 + 3];

    bool isNonZero = (x != 0.0f || y != 0.0f || z != 0.0f || w != 0.0f);

    if(isNonZero || ui->showEmpty->isChecked())
    {
      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {QFormatStr("c%1").arg(i), Formatter::Format((double)x), Formatter::Format((double)y),
           Formatter::Format((double)z), Formatter::Format((double)w)});

      if(!isNonZero)
        setInactiveRow(node);

      cbuffers->addTopLevelItem(node);
    }
  }

  // Integer constants
  int numIntRegs = stage.constants.intConstants.count() / 4;
  for(int i = 0; i < numIntRegs; i++)
  {
    int32_t x = stage.constants.intConstants[i * 4 + 0];
    int32_t y = stage.constants.intConstants[i * 4 + 1];
    int32_t z = stage.constants.intConstants[i * 4 + 2];
    int32_t w = stage.constants.intConstants[i * 4 + 3];

    bool isNonZero = (x != 0 || y != 0 || z != 0 || w != 0);

    if(isNonZero || ui->showEmpty->isChecked())
    {
      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {QFormatStr("i%1").arg(i), QString::number(x), QString::number(y), QString::number(z),
           QString::number(w)});

      if(!isNonZero)
        setInactiveRow(node);

      cbuffers->addTopLevelItem(node);
    }
  }

  // Boolean constants
  for(int i = 0; i < stage.constants.boolConstants.count(); i++)
  {
    bool val = stage.constants.boolConstants[i] != 0;

    if(val || ui->showEmpty->isChecked())
    {
      RDTreeWidgetItem *node = new RDTreeWidgetItem(
          {QFormatStr("b%1").arg(i), val ? tr("true") : tr("false"), QString(), QString(),
           QString()});

      if(!val)
        setInactiveRow(node);

      cbuffers->addTopLevelItem(node);
    }
  }

  cbuffers->clearSelection();
  cbuffers->endUpdate();
  cbuffers->verticalScrollBar()->setValue(vs);
}

void D3D9PipelineStateViewer::clearState()
{
  m_VBNodes.clear();
  m_EmptyNodes.clear();

  ui->iaLayouts->clear();
  ui->iaBuffers->clear();
  ui->iaFVF->setText(QString());
  ui->topology->setText(QString());
  ui->topologyDiagram->setPixmap(QPixmap());

  ui->vsShader->setText(QString());
  ui->vsResources->clear();
  ui->vsSamplers->clear();
  ui->vsCBuffers->clear();

  ui->psShader->setText(QString());
  ui->psResources->clear();
  ui->psSamplers->clear();
  ui->psCBuffers->clear();

  ui->ffpWorldMatrix->setText(QString());
  ui->ffpViewMatrix->setText(QString());
  ui->ffpProjMatrix->setText(QString());
  ui->ffpTexTransforms->clear();
  ui->ffpLights->clear();
  ui->ffpDiffuse->setText(QString());
  ui->ffpSpecular->setText(QString());
  ui->ffpAmbient->setText(QString());
  ui->ffpEmissive->setText(QString());
  ui->ffpPower->setText(QString());

  const QPixmap &cross = Pixmaps::cross(this);

  ui->ffpLightingEnabled->setPixmap(cross);
  ui->ffpFogEnabled->setPixmap(cross);

  ui->texStages->clear();
  ui->texSamplers->clear();

  ui->fillMode->setText(lit("Solid"));
  ui->cullMode->setText(lit("None"));
  ui->depthBias->setText(lit("0.0"));
  ui->slopeScaledDepthBias->setText(lit("0.0"));
  ui->rsScissorEnabled->setPixmap(cross);
  ui->rsMultisampleEnabled->setPixmap(cross);
  ui->rsAntialiasedLineEnabled->setPixmap(cross);
  ui->rsClipPlanes->clear();

  ui->omTargets->clear();
  ui->omDepth->setText(QString());
  ui->blends->clear();
  ui->blendFactor->setText(QString());
  ui->alphaToCoverage->setPixmap(cross);
  ui->separateAlpha->setPixmap(cross);

  ui->depthEnabled->setPixmap(cross);
  ui->depthFunc->setText(QString());
  ui->depthWrite->setPixmap(cross);
  ui->stencilEnabled->setPixmap(cross);
  ui->stencils->clear();
}

bool D3D9PipelineStateViewer::showNode(bool usedSlot, bool filledSlot)
{
  const bool showUnused = ui->showUnused->isChecked();
  const bool showEmpty = ui->showEmpty->isChecked();

  // show if it's bound
  if(usedSlot)
    return true;

  if(showUnused && filledSlot)
    return true;

  if(showEmpty && !filledSlot)
    return true;

  return false;
}

void D3D9PipelineStateViewer::setInactiveRow(RDTreeWidgetItem *node)
{
  node->setItalic(true);
}

void D3D9PipelineStateViewer::setEmptyRow(RDTreeWidgetItem *node)
{
  node->setBackgroundColor(QColor(255, 70, 70));
  node->setForegroundColor(QColor(0, 0, 0));
}

void D3D9PipelineStateViewer::exportHTML(QXmlStreamWriter &xml, const D3D9Pipe::State &pipe)
{
  // Input Assembly
  {
    xml.writeStartElement(lit("h3"));
    xml.writeCharacters(tr("Input Assembly"));
    xml.writeEndElement();

    // Vertex Elements
    {
      QStringList cols = {tr("Index"), tr("Semantic"), tr("Format"), tr("Stream"), tr("Offset"),
                          tr("Method")};
      QList<QVariantList> rows;

      for(int i = 0; i < pipe.inputAssembly.vertexElements.count(); i++)
      {
        const D3D9Pipe::VertexElement &el = pipe.inputAssembly.vertexElements[i];

        rows.push_back({i,
                        QFormatStr("%1%2").arg(D3D9DeclUsageStr(el.usage)).arg(el.usageIndex),
                        D3D9DeclTypeStr(el.type), el.stream, el.offset,
                        D3D9DeclMethodStr(el.method)});
      }

      m_Common.exportHTMLTable(xml, cols, rows);
    }

    // Vertex Buffers
    {
      QStringList cols = {tr("Slot"), tr("Buffer"), tr("Stride"), tr("Offset")};
      QList<QVariantList> rows;

      for(int i = 0; i < pipe.inputAssembly.vertexBuffers.count(); i++)
      {
        const D3D9Pipe::VertexBuffer &vb = pipe.inputAssembly.vertexBuffers[i];
        rows.push_back(
            {i, (qulonglong)vb.resourceId.IsValid(), vb.byteStride, vb.byteOffset});
      }

      m_Common.exportHTMLTable(xml, cols, rows);
    }
  }

  // Vertex Shader
  {
    xml.writeStartElement(lit("h3"));
    xml.writeCharacters(tr("Vertex Shader"));
    xml.writeEndElement();

    xml.writeStartElement(lit("p"));
    xml.writeCharacters(pipe.vertexShader.resourceId != ResourceId()
                            ? m_Ctx.GetResourceName(pipe.vertexShader.resourceId)
                            : tr("No Shader (Fixed Function)"));
    xml.writeEndElement();
  }

  // Rasterizer
  {
    xml.writeStartElement(lit("h3"));
    xml.writeCharacters(tr("Rasterizer State"));
    xml.writeEndElement();

    QStringList cols = {tr("Property"), tr("Value")};
    QList<QVariantList> rows;

    rows.push_back({tr("Fill Mode"), D3D9FillModeStr(pipe.rasterizer.fillMode)});
    rows.push_back({tr("Cull Mode"), D3D9CullModeStr(pipe.rasterizer.cullMode)});
    rows.push_back(
        {tr("Depth Bias"), Formatter::Format((double)pipe.rasterizer.depthBias)});
    rows.push_back({tr("Slope Scaled Depth Bias"),
                    Formatter::Format((double)pipe.rasterizer.slopeScaledDepthBias)});
    rows.push_back(
        {tr("Scissor Enable"), pipe.rasterizer.scissorEnable ? tr("True") : tr("False")});
    rows.push_back(
        {tr("Multisample"), pipe.rasterizer.multisampleEnable ? tr("True") : tr("False")});
    rows.push_back({tr("Antialiased Lines"),
                    pipe.rasterizer.antialiasedLineEnable ? tr("True") : tr("False")});

    m_Common.exportHTMLTable(xml, cols, rows);
  }

  // Pixel Shader
  {
    xml.writeStartElement(lit("h3"));
    xml.writeCharacters(tr("Pixel Shader"));
    xml.writeEndElement();

    xml.writeStartElement(lit("p"));
    xml.writeCharacters(pipe.pixelShader.resourceId != ResourceId()
                            ? m_Ctx.GetResourceName(pipe.pixelShader.resourceId)
                            : tr("No Shader (Fixed Function)"));
    xml.writeEndElement();
  }

  // Output Merger
  {
    xml.writeStartElement(lit("h3"));
    xml.writeCharacters(tr("Output Merger"));
    xml.writeEndElement();

    // Render Targets
    {
      QStringList cols = {tr("Slot"), tr("Resource")};
      QList<QVariantList> rows;

      for(int i = 0; i < pipe.outputMerger.renderTargets.count(); i++)
      {
        const ResourceId &rt = pipe.outputMerger.renderTargets[i];
        rows.push_back(
            {i, rt != ResourceId() ? m_Ctx.GetResourceName(rt) : tr("Unbound")});
      }

      m_Common.exportHTMLTable(xml, cols, rows);
    }

    // Depth surface
    xml.writeStartElement(lit("p"));
    xml.writeCharacters(QFormatStr("Depth Surface: %1")
                            .arg(pipe.outputMerger.depthStencil != ResourceId()
                                     ? m_Ctx.GetResourceName(pipe.outputMerger.depthStencil)
                                     : tr("Unbound")));
    xml.writeEndElement();

    // Blend state
    {
      const D3D9Pipe::BlendState &bs = pipe.outputMerger.blendState;
      QStringList cols = {tr("Property"), tr("Value")};
      QList<QVariantList> rows;

      rows.push_back({tr("Alpha Blend Enable"), bs.alphaBlendEnable ? tr("True") : tr("False")});
      rows.push_back({tr("Src Blend"), D3D9BlendStr(bs.srcBlend)});
      rows.push_back({tr("Dst Blend"), D3D9BlendStr(bs.destBlend)});
      rows.push_back({tr("Blend Op"), D3D9BlendOpStr(bs.blendOp)});
      rows.push_back({tr("Alpha Test Enable"), bs.alphaTestEnable ? tr("True") : tr("False")});
      rows.push_back({tr("Alpha Func"), D3D9CmpFuncStr(bs.alphaFunc)});
      rows.push_back({tr("Alpha Ref"), bs.alphaRef});

      m_Common.exportHTMLTable(xml, cols, rows);
    }

    // Depth-stencil state
    {
      const D3D9Pipe::DepthStencilState &ds = pipe.outputMerger.depthStencilState;
      QStringList cols = {tr("Property"), tr("Value")};
      QList<QVariantList> rows;

      rows.push_back({tr("Depth Enable"), ds.depthEnable ? tr("True") : tr("False")});
      rows.push_back({tr("Depth Write"), ds.depthWrite ? tr("True") : tr("False")});
      rows.push_back({tr("Depth Func"), D3D9CmpFuncStr(ds.depthFunc)});
      rows.push_back({tr("Stencil Enable"), ds.stencilEnable ? tr("True") : tr("False")});

      m_Common.exportHTMLTable(xml, cols, rows);
    }
  }
}

void D3D9PipelineStateViewer::exportHTMLTable(QXmlStreamWriter &xml, const QStringList &cols,
                                              const QList<QVariantList> &rows)
{
  m_Common.exportHTMLTable(xml, cols, rows);
}
