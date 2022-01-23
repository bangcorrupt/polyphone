/***************************************************************************
**                                                                        **
**  Polyphone, a soundfont editor                                         **
**  Copyright (C) 2013-2019 Davy Triponney                                **
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

#include "coloredtabwidget.h"
#include "contextmanager.h"
#include "tabbar.h"
#include <QPushButton>

const QSize ColoredTabWidget::TAB_ICON_SIZE = QSize(24, 24);

QString ColoredTabWidget::s_styleSheetLastPart = "\
QTabBar {\
    background-color: transparent;\
}\
QTabWidget::tab-bar {\
    left: 0;\
}\
QTabWidget:pane {\
    border-right: 0px;\
    border-left: 0px;\
    border-bottom: 0px;\
    top: -1px;\
}\
QTabBar::tab {\
    border-bottom: 0px;\
    border-top-left-radius: 3px;\
    border-top-right-radius: 3px;\
    border-bottom-left-radius: 0px;\
    border-bottom-right-radius: 0px;\
    padding: 2px 1px 2px 10px;\
    margin: 3px -1px 0 -1px;\
}\
QTabBar::tab:selected {\
    padding-top: 7px;\
    padding-right: 0px;\
    margin-top: 0px;\
    margin-right: 0px;\
}\
QTabBar::tab:last {\
    margin-right: 0px;\
}";

ColoredTabWidget::ColoredTabWidget(QWidget *parent) : QTabWidget(parent),
    _lastWidget(nullptr)
{
    // Tweak for Adwaita
    if (ContextManager::configuration()->getValue(ConfManager::SECTION_DISPLAY, "style", "Fusion").toString().contains("Adwaita"))
        s_styleSheetLastPart = s_styleSheetLastPart.replace("top: -1px;", "top: 0;");

    // First tab doesn't move
    this->setTabBar(new TabBar());

    // Colors
    _defaultWindowColor = ContextManager::theme()->getColor(ThemeManager::WINDOW_BACKGROUND);
    _defaultTextColor = ContextManager::theme()->getColor(ThemeManager::WINDOW_TEXT);
    QColor listColor = ContextManager::theme()->getColor(ThemeManager::LIST_BACKGROUND);
    QColor listColor2 = listColor.darker(110);
    QColor listColor3 = listColor2.darker(130);
    QColor borderColor = ContextManager::theme()->isDark(ThemeManager::WINDOW_BACKGROUND, ThemeManager::WINDOW_TEXT) ?
                _defaultWindowColor.lighter(120) : _defaultWindowColor.darker(150);
    QColor highlightColor = ContextManager::theme()->getColor(ThemeManager::HIGHLIGHTED_BACKGROUND);

    // Compute the first part of the stylesheet
    _styleSheetFirstPart = QString("\
        QTabWidget:pane{\
            border: 1px solid %1;\
        }\
        QTabBar::tab{\
            border: 1px solid %1;\
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %2, stop: 0.85 %2, stop: 0.93 %3, stop:1 %4);\
        }\
        QTabBar::tab:selected{\
            border: 1px solid %1;\
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %5, stop:0.10 %5, stop:0.15")
                   .arg(borderColor.name())
                   .arg(listColor.name())
                   .arg(listColor2.name())
                   .arg(listColor3.name())
                   .arg(highlightColor.name());

    // Apply default stylesheet, based on windowColor
    this->changeStyleSheet(_defaultWindowColor, _defaultTextColor);

    connect(this, SIGNAL(currentChanged(int)), this, SLOT(onCurrentChanged(int)));
}

void ColoredTabWidget::changeStyleSheet(QColor backgroundColor, QColor textColor)
{
    QString styleSheet = _styleSheetFirstPart + QString("\
        %1);\
        border-bottom: 1px solid %1;\
        color:%2\
    }")
       .arg(backgroundColor.name())
       .arg(textColor.name()) + s_styleSheetLastPart;

    // Max width of the first tab
    if (this->count() == 1)
        styleSheet += "QTabBar::tab{max-width:41px;}";
    else
        styleSheet += "QTabBar::tab:first{max-width:41px;}";

    this->setStyleSheet(styleSheet);
}

int ColoredTabWidget::addColoredTab(QWidget *widget, QString iconName, const QString &label, QColor backgroundColor, QColor textColor)
{
    // Store information about the new tab
    _tabInfo[widget]._backgroundColor = backgroundColor;
    _tabInfo[widget]._textColor = textColor;
    _tabInfo[widget]._iconName = iconName;

    // Add a tab
    int maxTabTitleSize = 30;
    int indexTab = this->addTab(widget, label.size() > maxTabTitleSize ? label.left(maxTabTitleSize - 3) + "..." : label);

    // Style the close button
    QPushButton * button = new QPushButton();
    button->setFlat(true);
    button->setMaximumSize(TAB_ICON_SIZE);
    button->setCursor(Qt::PointingHandCursor);
    _tabInfo[widget]._closeButton = button;
    connect(button, SIGNAL(clicked()), this, SLOT(onCloseButtonClicked()));
    this->tabBar()->setTabButton(indexTab, QTabBar::RightSide, button);
    if (this->tabBar()->tabButton(indexTab, QTabBar::LeftSide))
        this->tabBar()->tabButton(indexTab, QTabBar::LeftSide)->deleteLater();
    this->tabBar()->setTabButton(indexTab, QTabBar::LeftSide, nullptr);

    return indexTab;
}

void ColoredTabWidget::onCurrentChanged(int index)
{
    // Find the corresponding widget
    QWidget * currentWidget = this->widget(index);
    if (_tabInfo.contains(currentWidget))
    {
        changeStyleSheet(_tabInfo[currentWidget]._backgroundColor, _tabInfo[currentWidget]._textColor);
        QMap<QString, QString> replacement;
        replacement["currentColor"] = _tabInfo[currentWidget]._textColor.name();

        // Icons
        this->setTabIcon(index, ContextManager::theme()->getColoredSvg(_tabInfo[currentWidget]._iconName, TAB_ICON_SIZE, replacement));
        _tabInfo[currentWidget]._closeButton->setIcon(ContextManager::theme()->getColoredSvg(":/icons/close.svg", QSize(12, 12), replacement));
    }
    else
        changeStyleSheet(_defaultWindowColor, _defaultTextColor);

    // Restore the icon of the previous index
    if (_lastWidget != nullptr && _tabInfo.contains(_lastWidget) && this->indexOf(_lastWidget) != -1 && currentWidget != _lastWidget)
    {
        int lastIndex = this->indexOf(_lastWidget);
        QMap<QString, QString> replacement;
        replacement["currentColor"] = _defaultTextColor.name();

        // Icons
        this->setTabIcon(lastIndex, ContextManager::theme()->getColoredSvg(_tabInfo[_lastWidget]._iconName, TAB_ICON_SIZE, replacement));
        _tabInfo[_lastWidget]._closeButton->setIcon(ContextManager::theme()->getColoredSvg(":/icons/close.svg", QSize(12, 12), replacement));
    }

    // Store the new widget
    _lastWidget = currentWidget;
}

void ColoredTabWidget::onCloseButtonClicked()
{
    QPushButton * sender = static_cast<QPushButton *>(QObject::sender());
    if (sender == nullptr)
        return;

    // Find the widget having this button
    foreach (QWidget * widget, _tabInfo.keys())
    {
        if (_tabInfo[widget]._closeButton == sender)
        {
            // Send a tabbar event
            this->tabBar()->tabCloseRequested(this->indexOf(widget));
            onCurrentChanged(this->currentIndex());
            return;
        }
    }
}
