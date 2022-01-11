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

#include "auto_loop/toolautoloop.h"
#include "soundfontmanager.h"
#include "sampleutils.h"

void ToolAutoLoop::beforeProcess(IdList ids)
{
    Q_UNUSED(ids)
    _samplesNotLooped.clear();
}

void ToolAutoLoop::process(SoundfontManager * sm, EltID id, AbstractToolParameters *parameters)
{
    Q_UNUSED(parameters)

    // Get data, sample rate, start and end loop
    QByteArray baData = sm->getData(id, champ_sampleDataFull24);
    quint32 dwSmplRate = sm->get(id, champ_dwSampleRate).dwValue;
    quint32 startLoop = sm->get(id, champ_dwStartLoop).dwValue;
    quint32 endLoop = sm->get(id, champ_dwEndLoop).dwValue;

    // Loop
    baData = SampleUtils::bpsConversion(baData, 24, 32);
    quint32 crossfadeLength;
    bool result = SampleUtils::loopStep1(baData, dwSmplRate, startLoop, endLoop, crossfadeLength);
    if (result)
    {
        // Update data
        baData = SampleUtils::loopStep2(baData, startLoop, endLoop, crossfadeLength);
        baData = SampleUtils::bpsConversion(baData, 32, 24);
        sm->set(id, champ_sampleDataFull24, baData);

        // Update length, startloop and endloop
        AttributeValue val;
        val.dwValue = startLoop;
        sm->set(id, champ_dwStartLoop, val);
        val.dwValue = endLoop;
        sm->set(id, champ_dwEndLoop, val);
        val.dwValue = static_cast<quint32>(baData.size()) / 3;
        sm->set(id, champ_dwLength, val);
    }
    else
    {
        _mutex.lock();
        _samplesNotLooped << sm->getQstr(id, champ_name);
        _mutex.unlock();
    }
}

QString ToolAutoLoop::getWarning()
{
    QString txt;

    if (!_samplesNotLooped.isEmpty())
    {

        if (_samplesNotLooped.size() == 1)
            txt = tr("Failed to loop sample \"%1\".").arg(_samplesNotLooped.first()) + "<br/>";
        else
        {
            txt = tr("The following samples couldn't be looped:") + "<ul>";
            for (int i = 0; i < _samplesNotLooped.size(); i++)
                txt += "<li>" + _samplesNotLooped.at(i) + "</li>";
            txt += "</ul>";
        }
        txt += tr("Possible reasons: too short or too turbulent.");
    }

    return txt;
}
