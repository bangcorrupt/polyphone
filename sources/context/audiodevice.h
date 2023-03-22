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

#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include <QObject>
#include "synth.h"

class Synth;
class ConfManager;
typedef void PaStream;
typedef struct _jack_port jack_port_t;
typedef struct _jack_client jack_client_t;

#define SAMPLE_RATE           44100

class HostInfo
{
public:
    class DeviceInfo
    {
    public:
        DeviceInfo(QString name, int index, bool isDefault) :
            _name(name),
            _index(index),
            _isDefault(isDefault)
        {}

        QString _name;
        int _index;
        bool _isDefault;
    };

    HostInfo(QString name, int type) :
        _name(name),
        _type(type),
        _isDefault(false)
    {}

    QString _name;
    int _type; // -2 is jack, -1 is none, >= 0 are from port audio
    bool _isDefault;
    QList<DeviceInfo> _devices;
};

class AudioDevice : public QObject
{
    Q_OBJECT

public:
    AudioDevice(ConfManager * configuration);
    Synth * getSynth() { return _synth; }
    ~AudioDevice() override;

    QList<HostInfo> getHostInfo();

    // Jack needs a public access for the ports and the synth
    jack_port_t * _output_port_R;
    jack_port_t * _output_port_L;
    Synth * _synth;

    // Public access to the audio format
    quint32 _channelCount;
    quint32 _sampleRate;
    quint32 _sampleSize;

public slots:
    void initAudio();
    void closeConnections();

signals:
    void connectionDone();
    void connectionDefault();
    void connectionProblem();

private:
    void openDefaultConnection(quint32 bufferSize);
    void openJackConnection(quint32 bufferSize);
    void openStandardConnection(int hostType, int device, quint32 bufferSize);

    // Serveur son, sortie standard ou asio
    bool _isStandardRunning;
    PaStream * _standardStream;

    // Serveur son, jack
    jack_client_t * _jack_client;

    ConfManager * _configuration;
    bool _initialized;
};

#endif // AUDIODEVICE_H
