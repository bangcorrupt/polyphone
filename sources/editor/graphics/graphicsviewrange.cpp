/***************************************************************************
**                                                                        **
**  Polyphone, a soundfont editor                                         **
**  Copyright (C) 2013-2020 Davy Triponney                                **
**                                                                        **
**  This program is free software: you can redistribute it and/or modify  **
**  it under the terms of the GNU General Public License as published by  **
**  the Free Software Foundation, either version 3 of the License, or     **
**  (at your option) any later version.                                   **
**                                                                        **
**  This program is distributed in the hope that it will be useful,       **
**  but WITHOUT ANY WARRANTY; without even the implied warranty of        **
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          **
**  GNU General Public License for more details.                          **
**                                                                        **
**  You should have received a copy of the GNU General Public License     **
**  along with this program. If not, see http://www.gnu.org/licenses/.    **
**                                                                        **
****************************************************************************
**           Author: Davy Triponney                                       **
**  Website/Contact: https://www.polyphone-soundfonts.com                 **
**             Date: 01.01.2013                                           **
***************************************************************************/

#include "graphicsviewrange.h"
#include "contextmanager.h"
#include "graphicssimpletextitem.h"
#include "graphicsrectangleitem.h"
#include "graphicslegenditem.h"
#include "graphicslegenditem2.h"
#include "graphicszoomline.h"
#include "graphicskey.h"
#include <QScrollBar>
#include <QMouseEvent>
#include <QApplication>

const double GraphicsViewRange::WIDTH = 128.0;
const double GraphicsViewRange::MARGIN = 0.5;
const double GraphicsViewRange::OFFSET = -0.5;

// Z values (all GraphicsItem being at the top level)
//   0: grid
//  50: non selected rectangles
//  51: selected rectangles
//  80: play marker
// 100: axis values
// 120: legends
// 150: zoom line

GraphicsViewRange::GraphicsViewRange(QWidget *parent) : QGraphicsView(parent),
    _sf2(SoundfontManager::getInstance()),
    _scene(new QGraphicsScene(OFFSET, OFFSET, WIDTH, WIDTH)),
    _legendItem(nullptr),
    _legendItem2(nullptr),
    _zoomLine(nullptr),
    _dontRememberScroll(false),
    _keyTriggered(-1),
    _keepIndexOnRelease(false),
    _editing(false),
    _buttonPressed(Qt::NoButton),
    _moveOccured(false),
    _zoomX(1.),
    _zoomY(1.),
    _posX(0.5),
    _posY(0.5),
    _displayedRect(OFFSET, OFFSET, WIDTH, WIDTH),
    _shiftRectangles(2, nullptr)
{
    // Colors
    QColor color = ContextManager::theme()->getColor(ThemeManager::LIST_TEXT);
    color.setAlpha(180);
    _textColor = color;
    color.setAlpha(40);
    _lineColor = color;

    // Configuration
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->setRenderHint(QPainter::Antialiasing, true);
    this->setMouseTracking(true);

    // Preparation of the graphics
    this->setScene(_scene);
    this->initItems();
}

GraphicsViewRange::~GraphicsViewRange()
{
    while (!_rectangles.isEmpty())
        delete _rectangles.takeFirst();
    while (!_leftLabels.isEmpty())
        delete _leftLabels.takeFirst();
    while (!_bottomLabels.isEmpty())
        delete _bottomLabels.takeFirst();
    delete _legendItem;
    delete _legendItem2;
    delete _zoomLine;
    while (!_keyLines.isEmpty())
        delete _keyLines.takeFirst();
    while (!_mapGraphicsKeys.isEmpty())
        delete _mapGraphicsKeys.take(_mapGraphicsKeys.keys().first());
    delete _scene;
}

void GraphicsViewRange::initItems()
{
    // Vertical lines
    QPen penVerticalLines(_lineColor, 1);
    penVerticalLines.setCosmetic(true);
    for (quint32 note = 12; note <= 120; note += 12)
    {
        QGraphicsLineItem * line = new QGraphicsLineItem(note, OFFSET - MARGIN, note, OFFSET + WIDTH + MARGIN);
        _scene->addItem(line);
        line->setPen(penVerticalLines);
        line->setZValue(0);
        GraphicsSimpleTextItem * text = new GraphicsSimpleTextItem(Qt::AlignHCenter | Qt::AlignBottom);
        _scene->addItem(text);
        text->setZValue(100);
        text->setBrush(_textColor);
        text->setText(ContextManager::keyName()->getKeyName(note));
        text->setPos(note, OFFSET + WIDTH);
        _bottomLabels << text;
        _keyLines << line;
    }

    // Horizontal lines
    QPen penHorizontalLines(_lineColor, 1, Qt::DotLine);
    penHorizontalLines.setCosmetic(true);
    for (int vel = 10; vel <= 120; vel += 10)
    {
        QGraphicsLineItem * line = new QGraphicsLineItem(OFFSET - MARGIN, 127 - vel, OFFSET + WIDTH + MARGIN, 127 - vel);
        _scene->addItem(line);
        line->setPen(penHorizontalLines);
        line->setZValue(0);
        GraphicsSimpleTextItem * text = new GraphicsSimpleTextItem(Qt::AlignLeft | Qt::AlignVCenter);
        _scene->addItem(text);
        text->setZValue(100);
        text->setBrush(_textColor);
        text->setText(QString::number(vel));
        text->setPos(OFFSET, OFFSET + WIDTH - vel);
        _leftLabels << text;
        _keyLines << line;
    }

    // Legends
    _legendItem = new GraphicsLegendItem(this->font().family());
    _scene->addItem(_legendItem);
    _legendItem->setZValue(120);
    _legendItem2 = new GraphicsLegendItem2(this->font().family());
    _scene->addItem(_legendItem2);
    _legendItem2->setZValue(120);

    // Zoomline
    _zoomLine = new GraphicsZoomLine();
    _scene->addItem(_zoomLine);
    _zoomLine->setZValue(150);
}

void GraphicsViewRange::updateLabelPosition()
{
    // Current rect
    QRectF rect = getCurrentRect();

    // Update the position of the axis labels (they stay to the left and bottom)
    foreach (GraphicsSimpleTextItem * label, _leftLabels)
        label->setX(qMax(OFFSET, rect.x()));
    foreach (GraphicsSimpleTextItem * label, _bottomLabels)
        label->setY(qMin(WIDTH + OFFSET, rect.y() + rect.height()));

    // Update the position of the legend (it stays in a corner)
    if (_legendItem->isLeft())
    {
        double posX = 35. * rect.width() / this->width() + qMax(OFFSET, rect.x());
        _legendItem->setX(posX);
        _legendItem2->setX(posX);
    }
    else
    {
        double posX = qMin(WIDTH + OFFSET, rect.x() + rect.width()) - 5. * rect.width() / this->width();
        _legendItem->setX(posX);
        _legendItem2->setX(posX);
    }
    _legendItem->setY(5. * rect.height() / this->height() + qMax(OFFSET, rect.y()));
    _legendItem2->setY(13. * rect.height() / this->height() + qMax(OFFSET, rect.y()));
}

void GraphicsViewRange::updateHover(QPoint mousePos)
{
    // Rectangles under the mouse
    QList<QList<GraphicsRectangleItem *> > pairs = getRectanglesUnderMouse(mousePos);

    // Highlighted rectangles below the mouse, priority for the selected rectangles
    QList<GraphicsRectangleItem*> hoveredRectangles;
    int selectionNumber = pairs.count();
    int selectionIndex = 0;
    if (selectionNumber > 0)
    {
        for (int i = 0; i < pairs.count(); i++)
        {
            bool ok = !pairs[i].isEmpty();
            foreach (GraphicsRectangleItem * item, pairs[i])
                ok &= item->isSelected();
            if (ok)
            {
                selectionIndex = i;
                break;
            }
        }
        hoveredRectangles = pairs[selectionIndex];
    }

    // Update the hover, get the type of editing in the same time
    GraphicsRectangleItem::EditingMode editingMode = GraphicsRectangleItem::NONE;
    foreach (GraphicsRectangleItem * item, _rectangles)
    {
        if (hoveredRectangles.contains(item))
            editingMode = item->setHover(true, mousePos);
        else
            item->setHover(false);
    }

    // Adapt the cursor depending on the way we can edit the rectangle
    switch (editingMode)
    {
    case GraphicsRectangleItem::NONE:
        this->setCursor(Qt::ArrowCursor);
        break;
    case GraphicsRectangleItem::MOVE_ALL:
        this->setCursor(Qt::SizeAllCursor);
        break;
    case GraphicsRectangleItem::MOVE_RIGHT: case GraphicsRectangleItem::MOVE_LEFT:
        this->setCursor(Qt::SizeHorCursor);
        break;
    case GraphicsRectangleItem::MOVE_TOP: case GraphicsRectangleItem::MOVE_BOTTOM:
        this->setCursor(Qt::SizeVerCursor);
        break;
    }

    // Update the content of the first legend
    QList<EltID> ids;
    QList<int> selectedIds;
    int count = 0;
    for (int i = 0; i < pairs.count(); i++)
    {
        foreach  (GraphicsRectangleItem * item, pairs[i])
        {
            ids << item->getID();
            if (i == selectionIndex)
                selectedIds << count;
            count++;
        }
    }
    _legendItem->setIds(ids, selectedIds, selectionIndex, pairs.count());

    // Offset and location of the second legend
    _legendItem2->setOffsetY(_legendItem->boundingRect().bottom());
}

void GraphicsViewRange::display(IdList ids, bool justSelection)
{
    if (!justSelection)
    {
        // Clear previous rectangles
        while (!_rectangles.isEmpty())
        {
            _scene->removeItem(_rectangles.first());
            delete _rectangles.takeFirst();
        }

        // Add new ones
        _defaultID = ids[0];
        EltID idDiv = ids[0];
        switch (_defaultID.typeElement)
        {
        case elementInst: case elementInstSmpl:
            _defaultID.typeElement = elementInst;
            idDiv.typeElement = elementInstSmpl;
            break;
        case elementPrst: case elementPrstInst:
            _defaultID.typeElement = elementPrst;
            idDiv.typeElement = elementPrstInst;
            break;
        default:
            return;
        }

        foreach (int i, _sf2->getSiblings(idDiv))
        {
            idDiv.indexElt2 = i;
            GraphicsRectangleItem * item = new GraphicsRectangleItem(idDiv);
            item->setZValue(50);
            _scene->addItem(item);
            _rectangles << item;
        }

        // Reset the shiftpoints
        _shiftRectangles.fill(nullptr, 2);

        updateLabelPosition();
    }

    // Selection
    foreach (GraphicsRectangleItem * item, _rectangles)
    {
        if (ids.contains(item->getID()))
        {
            item->setSelected(true);
            _shiftRectangles[1] = item;
        }
        else {
            item->setSelected(false);
        }
    }

    viewport()->update();
}

void GraphicsViewRange::resizeEvent(QResizeEvent * event)
{
    _dontRememberScroll = true;
    QGraphicsView::resizeEvent(event);
    fitInView(_displayedRect);
    _dontRememberScroll = false;
}

void GraphicsViewRange::mousePressEvent(QMouseEvent *event)
{
    // Return immediately if a button is already pressed
    if (_buttonPressed != Qt::NoButton)
        return;

    if (event->button() == Qt::MiddleButton)
    {
        QPointF p = this->mapToScene(event->pos());
        int key = qRound(p.x());
        int velocity = 127 - qRound(p.y());
        if (velocity > 0)
        {
            ContextManager::midi()->processKeyOn(key, velocity, true);
            _keyTriggered = key;
        }
    }
    else if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
    {
        // Update current position
        double deltaX = WIDTH - _displayedRect.width();
        if (deltaX < 0.5)
            _posX = 0.5;
        else
            _posX = (_displayedRect.left() - OFFSET) / deltaX;
        double deltaY = WIDTH - _displayedRect.height();
        if (deltaY < 0.5)
            _posY = 0.5;
        else
            _posY = (_displayedRect.top() - OFFSET) / deltaY;

        // Remember situation
        _xInit = normalizeX(event->pos().x());
        _yInit = normalizeY(event->pos().y());
        _zoomXinit = _zoomX;
        _zoomYinit = _zoomY;
        _posXinit = _posX;
        _posYinit = _posY;

        // Rectangles below the mouse?
        QList<QList<GraphicsRectangleItem*> > pairs = getRectanglesUnderMouse(event->pos());
        if (event->button() == Qt::LeftButton && !pairs.empty())
        {
            _editing = true;
            GraphicsRectangleItem::syncHover(true);

            // Store the highlighted rectangle for the shift selection
            _shiftRectangles[0] = _shiftRectangles[1];
            _shiftRectangles[1] = pairs[0][0]; // By default
            for (int i = 0; i < pairs.count(); i++)
                foreach (GraphicsRectangleItem * item, pairs[i])
                    if (item->isHovered())
                        _shiftRectangles[1] = item;

            // Other properties
            Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
            bool hasSelection = false;
            for (int i = 0; i < pairs.count(); i++)
                foreach (GraphicsRectangleItem * item, pairs[i])
                    hasSelection |= item->isSelected();

            _keepIndexOnRelease = !hasSelection;
            if (modifiers == Qt::ShiftModifier)
            {
                // Select all rectangles between the previous rectangle and the new one
                if (_shiftRectangles[0] != nullptr && _shiftRectangles[1] != nullptr)
                {
                    QRectF shiftZone = _shiftRectangles[0]->rect().united(_shiftRectangles[1]->rect());
                    foreach (GraphicsRectangleItem * item, _rectangles)
                        item->setSelected(shiftZone.intersects(item->rect()));
                }
                else if (!hasSelection)
                {
                    // Select the first pair
                    foreach (GraphicsRectangleItem * item, pairs[0])
                        item->setSelected(true);
                }
            }
            else if (modifiers == Qt::ControlModifier)
            {
                if (!hasSelection)
                {
                    // Select the first pair
                    foreach (GraphicsRectangleItem * item, pairs[0])
                        item->setSelected(true);
                }
            }
            else // No modifiers
            {
                if (!hasSelection)
                {
                    // Deselect everything
                    foreach (GraphicsRectangleItem * item, _rectangles)
                        item->setSelected(false);

                    // Select the first pair
                    foreach (GraphicsRectangleItem * item, pairs[0])
                        item->setSelected(true);
                }
            }

            // Update the selection outside the range editor
            triggerDivisionSelected();
        }
    }

    _moveOccured = false;
    _buttonPressed = event->button();
    updateHover(event->pos());
    viewport()->update();
}

void GraphicsViewRange::mouseReleaseEvent(QMouseEvent *event)
{
    // Only continue if the button released if the one that initiated the move
    if (_buttonPressed != event->button())
        return;

    if (event->button() == Qt::MiddleButton)
    {
        if (_keyTriggered != -1)
        {
            ContextManager::midi()->processKeyOff(_keyTriggered, true);
            _keyTriggered = -1;
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        // Stop zooming
        this->setZoomLine(-1, 0, 0, 0);
    }
    else if (event->button() == Qt::LeftButton)
    {
        if (_moveOccured)
        {
            // Save the changes
            bool withChanges = false;
            foreach (GraphicsRectangleItem * item, _rectangles)
                if (item->isSelected())
                    withChanges |= item->saveChanges();

            if (withChanges)
            {
                _sf2->endEditing("rangeEditor");
                updateKeyboard();
            }
        }
        else
        {
            QList<QList<GraphicsRectangleItem*> > pairs = getRectanglesUnderMouse(event->pos());
            if (pairs.empty())
            {
                if (QApplication::keyboardModifiers() != Qt::ControlModifier)
                {
                    // Remove the selection
                    foreach (GraphicsRectangleItem * item, _rectangles)
                        item->setSelected(false);
                }
            }
            else
            {
                if (QApplication::keyboardModifiers() == Qt::ControlModifier && !_keepIndexOnRelease)
                {
                    // Something more to select?
                    int indexToSelect = -1;
                    for (int i = 0; i < pairs.count(); i++)
                    {
                        for (int j = 0; j < pairs[i].count(); j++)
                        {
                            if (!pairs[i][j]->isSelected())
                            {
                                pairs[i][j]->setSelected(false);
                                indexToSelect = i;
                                break;
                            }
                        }
                        if (indexToSelect != -1)
                            break;
                    }

                    if (indexToSelect != -1)
                    {
                        foreach (GraphicsRectangleItem * item, pairs[indexToSelect])
                            item->setSelected(true);
                    }
                    else
                    {
                        // Deselect everything below the mouse
                        for (int i = 0; i < pairs.count(); i++)
                            foreach (GraphicsRectangleItem * item, pairs[i])
                                item->setSelected(false);
                    }
                }
                else if (QApplication::keyboardModifiers() == Qt::NoModifier)
                {
                    // Current index of the selected pair (maximum index)
                    int index = -1;
                    for (int i = 0; i < pairs.count(); i++)
                    {
                        for (int j = 0; j < pairs[i].count(); j++)
                        {
                            if (pairs[i][j]->isSelected())
                            {
                                index = i;
                                break;
                            }
                        }
                    }

                    if (index == -1)
                    {
                        // Deselect everything
                        foreach (GraphicsRectangleItem * item, _rectangles)
                            item->setSelected(false);

                        // Select the first pair if possible
                        if (!pairs.empty())
                            for (int j = 0; j < pairs[0].count(); j++)
                                pairs[0][j]->setSelected(true);
                    }
                    else if (!_keepIndexOnRelease)
                    {
                        // Deselect everything
                        foreach (GraphicsRectangleItem * item, _rectangles)
                            item->setSelected(false);

                        // Next index is the new selection
                        index = (index + 1) % pairs.count();
                        foreach (GraphicsRectangleItem * item, pairs[index])
                            item->setSelected(true);
                    }
                }
            }

            // Update the selection outside the range editor
            triggerDivisionSelected();
        }
    }

    GraphicsRectangleItem::syncHover(false);
    _legendItem2->setNewValues(-1, 0, 0, 0);
    _editing = false;
    _buttonPressed = Qt::NoButton;
    updateHover(event->pos());
    viewport()->update();
}

void GraphicsViewRange::mouseMoveEvent(QMouseEvent *event)
{
    _moveOccured = true;

    switch (_buttonPressed)
    {
    case Qt::LeftButton: {
        this->setCursor(Qt::ClosedHandCursor);
        if (_editing)
        {
            // Try to move rectangles
            GraphicsRectangleItem * highlightedRectangle = nullptr;
            foreach (GraphicsRectangleItem * item, _rectangles)
            {
                if (item->isSelected())
                {
                    QPointF pointInit = this->mapToScene(
                                static_cast<int>(_xInit * this->width()), static_cast<int>(_yInit * this->height()));
                    QPointF pointFinal = this->mapToScene(event->pos());
                    item->computeNewRange(pointInit, pointFinal);

                    if (item->isHovered())
                        highlightedRectangle = item;
                }
            }

            if (highlightedRectangle != nullptr)
            {
                // Update the modification legend
                _legendItem2->setNewValues(highlightedRectangle->currentMinKey(), highlightedRectangle->currentMaxKey(),
                                           highlightedRectangle->currentMinVel(), highlightedRectangle->currentMaxVel());
            }
        }
        else
        {
            // Drag
            this->drag(event->pos());
        }
    } break;
    case Qt::RightButton:
        this->setCursor(Qt::SizeAllCursor);
        this->setZoomLine(_xInit, _yInit, normalizeX(event->pos().x()), normalizeY(event->pos().y()));
        this->zoom(event->pos());
        break;
    case Qt::NoButton: default: {
        // Section of the legend
        bool isLeft = (event->pos().x() > this->width() / 2);
        if (_legendItem->isLeft() != isLeft)
        {
            _legendItem->setLeft(isLeft);
            _legendItem2->setLeft(isLeft);
            updateLabelPosition();
        }

        // Update legend content
        updateHover(event->pos());
    } break;
    }

    viewport()->update();
}

void GraphicsViewRange::wheelEvent(QWheelEvent * event)
{
    if (_buttonPressed == Qt::NoButton)
        QGraphicsView::wheelEvent(event);
}

void GraphicsViewRange::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (_dontRememberScroll)
        return;

    // Update the displayed rect
    _displayedRect = getCurrentRect();

    // Limits
    if (_displayedRect.left() < OFFSET)
        _displayedRect.setLeft(OFFSET);
    if (_displayedRect.right() > WIDTH + OFFSET)
        _displayedRect.setRight(WIDTH + OFFSET);
    if (_displayedRect.top() < OFFSET)
        _displayedRect.setTop(OFFSET);
    if (_displayedRect.bottom() > WIDTH + OFFSET)
        _displayedRect.setBottom(WIDTH + OFFSET);

    updateLabelPosition();
}

void GraphicsViewRange::drag(QPoint point)
{
    // Décalage
    double decX = normalizeX(point.x()) - _xInit;
    double decY = normalizeY(point.y()) - _yInit;

    // Modification posX et posY
    if (_zoomXinit > 1)
        _posX = _posXinit - decX / (_zoomXinit - 1);
    if (_zoomYinit > 1)
        _posY = _posYinit - decY / (_zoomYinit - 1);

    // Mise à jour
    this->zoomDrag();
}

void GraphicsViewRange::zoom(QPoint point)
{
    // Décalage
    double decX = normalizeX(point.x()) - _xInit;
    double decY = _yInit - normalizeY(point.y());

    // Modification zoom & drag
    _zoomX = _zoomXinit * pow(2, 10.0 * decX);
    _zoomY = _zoomYinit * pow(2, 10.0 * decY);

    // Ajustement posX et posY
    if (_zoomX > 1)
        _posX = (_zoomX * _posXinit * (_zoomXinit - 1) +
                 _xInit * (_zoomX - _zoomXinit)) / (_zoomXinit * (_zoomX - 1));
    if (_zoomY > 1)
        _posY = (_zoomY * _posYinit * (_zoomYinit - 1) +
                 _yInit * (_zoomY - _zoomYinit)) / (_zoomYinit * (_zoomY - 1));

    // Mise à jour
    this->zoomDrag();
}

void GraphicsViewRange::zoomDrag()
{
    // Bornes des paramètres d'affichage
    if (_zoomX < 1)
        _zoomX = 1;
    else if (_zoomX > 8)
        _zoomX = 8;
    if (_zoomY < 1)
        _zoomY = 1;
    else if (_zoomY > 8)
        _zoomY = 8;
    if (_posX < 0)
        _posX = 0;
    else if (_posX > 1)
        _posX = 1;
    if (_posY < 0)
        _posY = 0;
    else if (_posY > 1)
        _posY = 1;

    // Application du drag et zoom
    double etendueX = WIDTH / _zoomX;
    double offsetX = (WIDTH - etendueX) * _posX + OFFSET;
    double etendueY = WIDTH / _zoomY;
    double offsetY = (WIDTH - etendueY) * _posY + OFFSET;
    _displayedRect.setRect(offsetX, offsetY, etendueX, etendueY);

    // Mise à jour
    _dontRememberScroll = true;
    this->fitInView(_displayedRect);
    _dontRememberScroll = false;
    updateLabelPosition();
}

void GraphicsViewRange::setZoomLine(double x1Norm, double y1Norm, double x2Norm, double y2Norm)
{
    if (x1Norm < 0)
        _zoomLine->setSize(0, 0);
    else
    {
        QRectF rect = getCurrentRect();

        // Initial position
        _zoomLine->setPos(rect.left() + x1Norm * rect.width(),
                          rect.top() + y1Norm * rect.height());

        // Size
        _zoomLine->setSize((x2Norm - x1Norm) * this->width(),
                           (y2Norm - y1Norm) * this->height());
    }
}

QList<QList<GraphicsRectangleItem*> > GraphicsViewRange::getRectanglesUnderMouse(QPoint mousePos)
{
    // Rectangles under the mouse
    QList<GraphicsRectangleItem *> rectanglesUnderMouse;
    foreach (GraphicsRectangleItem * item, _rectangles)
        if (item->contains(this->mapToScene(mousePos)))
            rectanglesUnderMouse << item;

    // Sort them by pairs
    QList<QList<GraphicsRectangleItem*> > pairs;
    while (!rectanglesUnderMouse.isEmpty())
    {
        QList<GraphicsRectangleItem*> listTmp;
        listTmp << rectanglesUnderMouse.takeFirst();
        EltID idBrother = listTmp[0]->findBrother();
        foreach (GraphicsRectangleItem* item, rectanglesUnderMouse)
        {
            if (*item == idBrother)
            {
                listTmp << item;
                rectanglesUnderMouse.removeOne(item);
                break;
            }
        }
        pairs << listTmp;
    }

    return pairs;
}

double GraphicsViewRange::normalizeX(int xPixel)
{
    return static_cast<double>(xPixel) / this->width();
}

double GraphicsViewRange::normalizeY(int yPixel)
{
    return static_cast<double>(yPixel) / this->height();
}

QRectF GraphicsViewRange::getCurrentRect()
{
    QPointF tl(horizontalScrollBar()->value(), verticalScrollBar()->value());
    QPointF br = tl + viewport()->rect().bottomRight();
    QTransform mat = transform().inverted();
    return mat.mapRect(QRectF(tl,br));
}

void GraphicsViewRange::playKey(int key, int velocity)
{
    if (velocity == 0 && _mapGraphicsKeys[key] != nullptr)
    {
        // A key is removed
        delete _mapGraphicsKeys.take(key);
    }
    else if (velocity > 0 && _mapGraphicsKeys[key] == nullptr)
    {
        // A key is added
        _mapGraphicsKeys[key] = new GraphicsKey();
        _scene->addItem(_mapGraphicsKeys[key]);
        _mapGraphicsKeys[key]->setPos(QPoint(key, 127 - velocity));
        _mapGraphicsKeys[key]->setZValue(80);
    }
}

void GraphicsViewRange::triggerDivisionSelected()
{
    IdList ids;
    foreach (GraphicsRectangleItem * item, _rectangles)
        if (item->isSelected())
            ids << item->getID();
    if (ids.empty())
        ids << _defaultID;
    divisionsSelected(ids);
    updateKeyboard();
}
