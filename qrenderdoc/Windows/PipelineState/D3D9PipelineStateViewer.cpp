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

D3D9PipelineStateViewer::D3D9PipelineStateViewer(ICaptureContext &ctx,
                                                 PipelineStateViewer &common, QWidget *parent)
    : QFrame(parent), ui(new Ui::D3D9PipelineStateViewer), m_Ctx(ctx), m_Common(common)
{
  ui->setupUi(this);

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
  setState();
}

void D3D9PipelineStateViewer::SelectPipelineStage(PipelineStage stage)
{
  // TODO: Task 4 will implement stage selection via pipeFlow/stagesTabs
}

ResourceId D3D9PipelineStateViewer::GetResource(RDTreeWidgetItem *item)
{
  // TODO: Task 4 will implement resource ID extraction from tree items
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
  // TODO: Task 4 will implement HTML export
}

void D3D9PipelineStateViewer::on_meshView_clicked()
{
  // TODO: Task 4 will implement mesh view opening
}

void D3D9PipelineStateViewer::on_pipeFlow_stageSelected(int index)
{
  // TODO: Task 4 will implement stage selection from flow chart
}

void D3D9PipelineStateViewer::setState()
{
  // TODO: Task 4 will populate all UI widgets from D3D9Pipe::State
}

void D3D9PipelineStateViewer::clearState()
{
  m_VBNodes.clear();
  m_EmptyNodes.clear();

  // TODO: Task 4 will clear all tree widgets and labels
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
  // TODO: Task 4 will implement inactive row styling
}

void D3D9PipelineStateViewer::setEmptyRow(RDTreeWidgetItem *node)
{
  // TODO: Task 4 will implement empty row styling
}

void D3D9PipelineStateViewer::exportHTML(QXmlStreamWriter &xml, const D3D9Pipe::State &pipe)
{
  // TODO: Task 4 will implement HTML export
}

void D3D9PipelineStateViewer::exportHTMLTable(QXmlStreamWriter &xml, const QStringList &cols,
                                              const QList<QVariantList> &rows)
{
  m_Common.exportHTMLTable(xml, cols, rows);
}
