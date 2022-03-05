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

#ifndef CONTROLLEREVENT_H
#define CONTROLLEREVENT_H

#include <QEvent>

class ControllerEvent : public QEvent
{
public:
    ControllerEvent(qint8 channel, quint8 numController, quint8 value) : QEvent((QEvent::Type)(QEvent::User+1)),
        _channel(channel),
        _numController(numController),
        _value(value) {}

    qint8 getChannel() const
    {
        return _channel;
    }

    quint8 getNumController() const
    {
        return _numController;
    }

    quint8 getValue() const
    {
        return _value;
    }

protected:
    qint8 _channel;
    quint8 _numController;
    quint8 _value;
};

#endif // CONTROLLEREVENT_H
