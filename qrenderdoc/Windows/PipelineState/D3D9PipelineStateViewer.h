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

#include <QFrame>
#include "Code/Interface/QRDInterface.h"

namespace Ui
{
class D3D9PipelineStateViewer;
}

class QXmlStreamWriter;

class RDLabel;
class RDTreeWidget;
class RDTreeWidgetItem;
class PipelineStateViewer;

class D3D9PipelineStateViewer : public QFrame, public ICaptureViewer
{
  Q_OBJECT

public:
  explicit D3D9PipelineStateViewer(ICaptureContext &ctx, PipelineStateViewer &common,
                                   QWidget *parent = 0);
  ~D3D9PipelineStateViewer();

  // ICaptureViewer
  void OnCaptureLoaded() override;
  void OnCaptureClosed() override;
  void OnSelectedEventChanged(uint32_t eventId) override {}
  void OnEventChanged(uint32_t eventId) override;

  void SelectPipelineStage(PipelineStage stage);
  ResourceId GetResource(RDTreeWidgetItem *item);

private slots:
  // automatic slots
  void on_showUnused_toggled(bool checked);
  void on_showEmpty_toggled(bool checked);
  void on_exportHTML_clicked();
  void on_meshView_clicked();
  void on_pipeFlow_stageSelected(int index);

private:
  Ui::D3D9PipelineStateViewer *ui;
  ICaptureContext &m_Ctx;
  PipelineStateViewer &m_Common;

  void setState();
  void clearState();

  bool showNode(bool usedSlot, bool filledSlot);
  void setInactiveRow(RDTreeWidgetItem *node);
  void setEmptyRow(RDTreeWidgetItem *node);

  void exportHTML(QXmlStreamWriter &xml, const D3D9Pipe::State &pipe);
  void exportHTMLTable(QXmlStreamWriter &xml, const QStringList &cols,
                       const QList<QVariantList> &rows);

  // keep track of the VB nodes (we want to be able to highlight them easily on hover)
  QList<RDTreeWidgetItem *> m_VBNodes;
  // list of empty VB nodes that shouldn't be highlighted on hover
  QList<RDTreeWidgetItem *> m_EmptyNodes;
};
