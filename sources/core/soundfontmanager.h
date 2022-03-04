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

#ifndef PILE_SF2_H
#define PILE_SF2_H

#include "sound.h"
#include "basetypes.h"
#include <QMap>
#include <QObject>
class Action;
class ActionManager;
class Soundfonts;
class QAbstractItemModel;
class SoloManager;

class SoundfontManager : public QObject
{
    Q_OBJECT

public:
    static SoundfontManager * getInstance();
    static void kill();
    ~SoundfontManager();
    QAbstractItemModel * getModel(int indexSf2);

    // Add / delete data
    int add(EltID id);
    void remove(EltID id, int *message = nullptr);

    // Get / set properties
    bool isSet(EltID id, AttributeType champ);
    AttributeValue get(EltID id, AttributeType champ);
    QString getQstr(EltID id, AttributeType champ);
    Sound *getSound(EltID id);
    QByteArray getData(EltID id, AttributeType champ);
    int set(EltID id, AttributeType champ, AttributeValue value);
    int set(EltID id, AttributeType champ, QString qStr);
    int set(EltID id, AttributeType champ, QByteArray data);
    void reset(EltID id, AttributeType champ);
    void simplify(EltID id, AttributeType champ);

    // Nombre de freres de id (id compris)
    QList<int> getSiblings(EltID &id);

    // Gestionnaire d'actions
    void endEditing(QString editingSource);
    void clearNewEditing(); // Keep the changes but don't make an undo
    void revertNewEditing(); // Doesn't keep the changes
    bool isUndoable(int indexSf2);
    bool isRedoable(int indexSf2);
    void undo(int indexSf2);
    void redo(int indexSf2);

    // Edition management
    void markAsSaved(int indexSf2);
    bool isEdited(int indexSf2);

    // Get all attributes or modulators related to inst, instsmpl, prst, prstinst
    void getAllAttributes(EltID id, QList<AttributeType> &listeChamps, QList<AttributeValue> &listeValeurs);
    void getAllModulators(EltID id, QList<ModulatorData> &modulators);

    // Find if an ID is valid (allowing or not browing in hidden ID, not allowed by default)
    bool isValid(EltID &id, bool acceptHidden = false, bool justCheckParentLevel = false);

    // Availability of banks / presets
    void firstAvailablePresetBank(EltID id, int &nBank, int &nPreset);
    int closestAvailablePreset(EltID id, quint16 wBank, quint16 wPreset);
    bool isAvailable(EltID id, quint16 wBank, quint16 wPreset);

    // Access to the solo manager
    SoloManager * solo() { return _solo; }

    // Create a notification about a new soundfont that has been loaded
    void emitNewSoundfontLoaded(int sf2Index) { emit(this->soundfontLoaded(sf2Index)); }

signals:
    // Emitted when a group of actions is finished
    // "editingSource" can be:
    //   * command:{command name} (for instance "command:undo", "command:redo", "command:display", "command:selection")
    //   * tool:{tool kind}:{tool name}
    //   * page:{page name}
    void editingDone(QString editingSource, QList<int> sf2Indexes);

    // Emitted when a new soundfont is loaded
    void soundfontLoaded(int indexSf2);

    // Emitted when a soundfont is closed
    void soundfontClosed(int indexSf2);

private slots:
    void onDropId(EltID id);

private:
    SoundfontManager();

    /// Display the element ID
    int display(EltID id);

    /// Delete or hide the element id. Error if the element is used by another
    int remove(EltID id, bool permanently, bool storeAction, int *message = nullptr);

    /// Clear parameters
    void supprGenAndStore(EltID id, int storeAction);

    QList<int> undo(QList<Action *> actions);

    static SoundfontManager * s_instance;
    Soundfonts * _soundfonts;
    ActionManager * _undoRedo;
    QRecursiveMutex _mutex;
    SoloManager * _solo;
};

#endif // PILE_SF2_H
