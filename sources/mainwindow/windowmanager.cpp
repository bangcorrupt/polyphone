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

#include "windowmanager.h"
#include "contextmanager.h"
#include "configpanel.h"
#include "soundfontbrowser.h"
#include "mainstackedwidget.h"
#include "soundfontmanager.h"
#include "soundfontfilter.h"
#include "soundfontviewer.h"
#include "editor.h"
#include "userarea.h"
#include "inputfactory.h"
#include "outputfactory.h"
#include "repositorymanager.h"
#include "utils.h"
#include "synth.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QAbstractButton>
#include <QApplication>

WindowManager * WindowManager::s_instance = nullptr;

WindowManager * WindowManager::getInstance(MainStackedWidget * stackedWidget)
{
    if (s_instance == nullptr)
        s_instance = new WindowManager(stackedWidget);
    return s_instance;
}

void WindowManager::kill()
{
    delete s_instance;
    s_instance = nullptr;
}

WindowManager::WindowManager(MainStackedWidget * stackedWidget) : QObject(nullptr),
    _stackedWidget(stackedWidget),
    _configTab(new ConfigPanel()),
    _browserTab(new SoundfontBrowser()),
    _userTab(new UserArea())
{
    SoundfontManager * sf2 = SoundfontManager::getInstance();
    connect(sf2, SIGNAL(editingDone(QString,QList<int>)), this, SLOT(editingDone(QString,QList<int>)));
    connect(_stackedWidget, SIGNAL(tabCloseRequested(QWidget*)), this, SLOT(onTabCloseRequested(QWidget*)));
    connect(_stackedWidget, SIGNAL(currentChanged(int)), this, SLOT(onTabIndexChanged(int)));
}

WindowManager::~WindowManager()
{
    delete _configTab;
    delete _browserTab;
    delete _userTab;
    while (_stackedWidget->count() > 0)
    {
        QWidget * widget = _stackedWidget->widget(0);
        _stackedWidget->removeWidgetWithTab(widget);
    }
    QApplication::processEvents();
    while (!_editors.isEmpty())
        delete _editors.takeFirst();
    while (!_viewers.isEmpty())
        delete _viewers.takeFirst();
}

void WindowManager::openConfiguration()
{
    _configTab->initializeInterface();
    int index = _stackedWidget->indexOf(_configTab);
    if (index == -1)
        index = _stackedWidget->addWidgetWithTab(_configTab, ":/icons/settings.svg", tr("Settings"), false);
    _stackedWidget->setCurrentIndex(index);
}

void WindowManager::openNewSoundfont()
{
    // Create a new editor
    Editor * editor = new Editor();
    int index = _stackedWidget->addWidgetWithTab(editor, ":/icons/file-audio.svg", "", true);
    connect(editor, SIGNAL(tabTitleChanged(QString)), this, SLOT(onTabTitleChanged(QString)));
    connect(editor, SIGNAL(filePathChanged(QString)), this, SLOT(onFilePathChanged(QString)));
    connect(editor, SIGNAL(keyboardDisplayChanged(bool)), this, SIGNAL(keyboardDisplayChanged(bool)));
    connect(editor, SIGNAL(recorderDisplayChanged(bool)), this, SIGNAL(recorderDisplayChanged(bool)));
    _editors << editor;

    // Initialize and display it
    editor->initialize(InputFactory::getInput(""));
    _stackedWidget->setCurrentIndex(index);
}

void WindowManager::openSoundfont(QString fileName)
{
    fileName = Utils::fixFilePath(fileName);

    // Extension supported?
    if (!InputFactory::isSuffixSupported(QFileInfo(fileName).suffix()))
    {
        QMessageBox::warning(_stackedWidget, tr("Warning"),
                             tr("Cannot open file \"%1\"").arg(fileName));
        return;
    }

    ContextManager::recentFile()->addRecentFile(RecentFileManager::FILE_TYPE_SOUNDFONT, fileName);

    // Check if the file is not already open?
    SoundfontManager * sf2 = SoundfontManager::getInstance();
    int indexSf2 = -1;
    EltID idSf2(elementSf2);
    foreach (int i, sf2->getSiblings(idSf2))
    {
        if (sf2->getQstr(EltID(elementSf2, i), champ_filenameInitial) == fileName)
        {
            indexSf2 = i;
            break;
        }
    }

    // Find the corresponding editor if the file is already open
    if (indexSf2 != -1)
    {
        foreach (Editor * editor, _editors)
        {
            if (editor->getSf2Index() == indexSf2)
            {
                int index = _stackedWidget->indexOf(editor);
                _stackedWidget->setCurrentIndex(index);
                return;
            }
        }
    }

    // Otherwise, create a new editor
    Editor * editor = new Editor();
    int index = _stackedWidget->addWidgetWithTab(editor, ":/icons/file-audio.svg", QFileInfo(fileName).fileName(), true);
    connect(editor, SIGNAL(tabTitleChanged(QString)), this, SLOT(onTabTitleChanged(QString)));
    connect(editor, SIGNAL(filePathChanged(QString)), this, SLOT(onFilePathChanged(QString)));
    connect(editor, SIGNAL(keyboardDisplayChanged(bool)), this, SIGNAL(keyboardDisplayChanged(bool)));
    connect(editor, SIGNAL(recorderDisplayChanged(bool)), this, SIGNAL(recorderDisplayChanged(bool)));
    _editors << editor;

    // Initialize and display it
    _stackedWidget->setCurrentIndex(index);
    editor->initialize(InputFactory::getInput(fileName));
}

void WindowManager::openRepository(SoundfontFilter *filter)
{
    if (filter)
    {
        _browserTab->applyFilter(filter);
        delete filter;
    }
    int index = _stackedWidget->indexOf(_browserTab);
    if (index == -1)
        index = _stackedWidget->addWidgetWithTab(_browserTab, ":/icons/globe.svg", tr("Online repository"), true);
    _stackedWidget->setCurrentIndex(index);
}

void WindowManager::editingDone(QString source, QList<int> sf2Indexes)
{
    // Update all editing pages related to one of the edited sf2
    foreach (Editor* editor, _editors)
        if (sf2Indexes.contains(editor->getSf2Index()))
            editor->update(source);
}

void WindowManager::onTabTitleChanged(QString title)
{
    int maxTabTitleSize = 30;
    _stackedWidget->setWidgetLabel(dynamic_cast<Editor*>(QObject::sender()), title.size() > maxTabTitleSize ? title.left(maxTabTitleSize - 3) + "..." : title);
}

void WindowManager::onFilePathChanged(QString filePath)
{
    _stackedWidget->setWidgetToolTip(dynamic_cast<Editor*>(QObject::sender()), filePath);
}

void WindowManager::onTabCloseRequested(QWidget * widget)
{
    SoundfontManager * sf2 = SoundfontManager::getInstance();
    if (_editors.contains(dynamic_cast<Editor*>(widget)))
    {
        // Close a soundfont
        Editor * editor = dynamic_cast<Editor*>(widget);
        editor->setFocus();

        int ret;
        EltID id(elementSf2, editor->getSf2Index());
        if (sf2->isEdited(id.indexSf2))
        {
            QMessageBox msgBox(_stackedWidget);
            msgBox.setIcon(QMessageBox::Warning);
            id.typeElement = elementSf2;
            msgBox.setText("<b>" + tr("Save before exiting?") + "</b>");
            QString filename = sf2->getQstr(id, champ_name);
            if (filename.isEmpty())
                filename = tr("untitled");
            msgBox.setInformativeText(tr("File \"%1\" has been modified.").arg(filename));
            msgBox.setWindowTitle(tr("Warning"));
            msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            msgBox.button(QMessageBox::Save)->setText(tr("&Save"));
            msgBox.button(QMessageBox::Cancel)->setText(tr("&Cancel"));
            msgBox.button(QMessageBox::Discard)->setText(tr("Do&n't save"));
            msgBox.button(QMessageBox::Save)->setIcon(QIcon::fromTheme("filesave"));
            msgBox.setDefaultButton(QMessageBox::Save);
            ret = msgBox.exec();
        }
        else
            ret = QMessageBox::Discard;

        // Possibly cancel the close action
        switch (ret)
        {
        case QMessageBox::Cancel:
            return;
        case QMessageBox::Save:
            if (!OutputFactory::save(id.indexSf2, false))
                return;
            break;
        case QMessageBox::Discard:
            break;
        }

        // Delete the editor
        _editors.removeAll(editor);
        _stackedWidget->removeWidgetWithTab(widget);
        delete editor;

        // Mute all sounds produced by the soundfont, if any
        ContextManager::audio()->getSynth()->play(id, -2, -2, 0);

        // Delete the model linked to the soundfont
        if (id.indexSf2 >= 0)
            sf2->remove(id);

        if (_editors.empty())
        {
            emit(recorderDisplayChanged(false));
            emit(keyboardDisplayChanged(false));
        }
    }
    else if (widget == dynamic_cast<QWidget*>(_configTab))
    {
        // Close the configurations
        _stackedWidget->removeWidgetWithTab(_configTab);
    }
    else if (widget == dynamic_cast<QWidget*>(_browserTab))
    {
        // Close the soundfont browser
        _stackedWidget->removeWidgetWithTab(_browserTab);
    }
    else if (widget == dynamic_cast<QWidget*>(_userTab))
    {
        // Close the user area
        _stackedWidget->removeWidgetWithTab(_userTab);
    }
    else if (_viewers.contains(dynamic_cast<SoundfontViewer*>(widget)))
    {
        // Close a soundfont from the repository
        SoundfontViewer * viewer = dynamic_cast<SoundfontViewer*>(widget);
        _viewers.removeAll(viewer);
        _stackedWidget->removeWidgetWithTab(widget);
        delete viewer;
    }
}

int WindowManager::getCurrentSf2()
{
    QWidget * widget = _stackedWidget->currentWidget();
    if (_editors.contains(dynamic_cast<Editor*>(widget)))
    {
        Editor * editor = dynamic_cast<Editor*>(widget);
        return editor->getSf2Index();
    }
    return -1;
}

void WindowManager::closeCurrentTab()
{
    this->onTabCloseRequested(_stackedWidget->currentWidget());
}

void WindowManager::onTabIndexChanged(int tabIndex)
{
    QWidget * widget = _stackedWidget->widget(tabIndex);
    emit(editorOpen(_editors.contains(dynamic_cast<Editor*>(widget))));
}

void WindowManager::openUser()
{
    _userTab->initializeInterface();
    int index = _stackedWidget->indexOf(_userTab);
    if (index == -1)
        index = _stackedWidget->addWidgetWithTab(_userTab, ":/icons/user.svg", tr("User area"), false);
    _stackedWidget->setCurrentIndex(index);
}

void WindowManager::openRepositorySoundfont(int id)
{
    // Find the corresponding viewer if the file is already open
    foreach (SoundfontViewer * viewer, _viewers)
    {
        if (viewer->getSoundfontId() == id)
        {
            int index = _stackedWidget->indexOf(viewer);
            _stackedWidget->setCurrentIndex(index);
            return;
        }
    }

    // Title of the soundfont
    SoundfontInformation * si = RepositoryManager::getInstance()->getSoundfontInformation(id);
    if (si == nullptr)
        return;
    QFontMetrics fontMetrics(QApplication::font());
    QString title = fontMetrics.elidedText(si->getTitle(), Qt::ElideMiddle, 150);

    // Create a new viewer
    SoundfontViewer * viewer = new SoundfontViewer();
    connect(viewer, SIGNAL(itemClicked(SoundfontFilter*)), this, SLOT(openRepository(SoundfontFilter*)));
    int index = _stackedWidget->addWidgetWithTab(viewer, ":/icons/file-description.svg", title, true);
    _viewers << viewer;

    // Initialize and display it
    viewer->initialize(id, false);
    _stackedWidget->setCurrentIndex(index);
}
