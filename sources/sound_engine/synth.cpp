/***************************************************************************
**                                                                        **
**  Polyphone, a soundfont editor                                         **
**  Copyright (C) 2013-2019 Davy Triponney                                **
**                2014      Andrea Celani                                 **
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

#include "synth.h"
#include <QThread>
#include <QFile>
#include "contextmanager.h"
#include "soundfontmanager.h"

int Synth::s_sampleVoiceTokenCounter = 0;

// Constructeur, destructeur
Synth::Synth(ConfManager *configuration) : QObject(nullptr),
    _sf2(SoundfontManager::getInstance()),
    _gain(0),
    _tuningFork(440),
    _choLevel(0), _choDepth(0), _choFrequency(0),
    _clipCoef(1),
    _recordFile(nullptr),
    _isRecording(true),
    _fTmpSumRev1(nullptr),
    _fTmpSumRev2(nullptr),
    _dataWav(nullptr),
    _bufferSize(0),
    _configuration(configuration)
{
    // Default temperament: equal
    memset(_temperament, 0, 12 * sizeof(double));

    // Creation buffers and sound engines
    updateConfiguration();
}

Synth::~Synth()
{
    destroySoundEnginesAndBuffers();
}

void Synth::destroySoundEnginesAndBuffers()
{
    // Stop sound engines
    for (int i = 0; i < _soundEngines.size(); i++)
        _soundEngines.at(i)->stop();

    // Stop threads
    for (int i = 0; i < _soundEngines.size(); i++)
        _soundEngines.at(i)->thread()->quit();

    // Delete sound engines and threads
    while (_soundEngines.size())
    {
        QThread * thread = _soundEngines.last()->thread();
        thread->wait(50);
        delete _soundEngines.takeLast();
        delete thread;
    }

    delete [] _fTmpSumRev1;
    delete [] _fTmpSumRev2;
    delete [] _dataWav;
}

void Synth::createSoundEnginesAndBuffers()
{
    _fTmpSumRev1 = new float [4 * _bufferSize];
    _fTmpSumRev2 = new float [4 * _bufferSize];
    _dataWav = new float[8 * _bufferSize];

    int nbEngines = qMax(QThread::idealThreadCount() - 2, 1);
    for (int i = 0; i < nbEngines; i++)
    {
        SoundEngine * soundEngine = new SoundEngine(_bufferSize);
        connect(soundEngine, SIGNAL(readFinished(int)), this, SIGNAL(readFinished(int)));
        soundEngine->moveToThread(new QThread());
        soundEngine->thread()->start(QThread::TimeCriticalPriority);
        QMetaObject::invokeMethod(soundEngine, "start");
        _soundEngines << soundEngine;
    }
}

int Synth::play(EltID id, int key, int velocity)
{
    if (velocity == 0)
    {
        // Release of a key
        SoundEngine::releaseNote(key);
        return -1;
    }

    // A key is pressed
    int playingToken = -1;
    switch (id.typeElement)
    {
    case elementSmpl:
        playingToken = playSmpl(id.indexSf2, id.indexElt, key, velocity);
        break;
    case elementInst: case elementInstSmpl:
        playInst(id.indexSf2, id.indexElt, key, velocity);
        break;
    case elementPrst: case elementPrstInst:
        playPrst(id.indexSf2, id.indexElt, key, velocity);
        break;
    default:
        return -1;
    }

    // Synchronize all new voices that have been added
    _mutexSynchro.lock();
    SoundEngine::syncNewVoices();
    _mutexSynchro.unlock();

    // Reset the list used for the exclusive class system
    _listVoixTmp.clear();
    return playingToken;
}

void Synth::playPrst(int idSf2, int idElt, int key, int velocity)
{
    // Default preset range
    EltID idPrst(elementPrst, idSf2, idElt, 0, 0);
    RangesType defaultKeyRange, defaultVelRange;
    if (_sf2->isSet(idPrst, champ_keyRange))
        defaultKeyRange = _sf2->get(idPrst, champ_keyRange).rValue;
    else
    {
        defaultKeyRange.byLo = 0;
        defaultKeyRange.byHi = 127;
    }
    if (_sf2->isSet(idPrst, champ_velRange))
        defaultVelRange = _sf2->get(idPrst, champ_velRange).rValue;
    else
    {
        defaultVelRange.byLo = 0;
        defaultVelRange.byHi = 127;
    }

    // Browse the ranges of all linked instruments
    EltID idPrstInst(elementPrstInst, idSf2, idElt, 0, 0);
    int keyMin, keyMax, velMin, velMax;
    RangesType rangeTmp;
    foreach (int i, _sf2->getSiblings(idPrstInst))
    {
        idPrstInst.indexElt2 = i;

        // Skip muted divisions
        if (_sf2->get(idPrstInst, champ_mute).bValue > 0)
            continue;

        if (_sf2->isSet(idPrstInst, champ_keyRange))
        {
            rangeTmp = _sf2->get(idPrstInst, champ_keyRange).rValue;
            keyMin = rangeTmp.byLo;
            keyMax = rangeTmp.byHi;
        }
        else
        {
            keyMin = defaultKeyRange.byLo;
            keyMax = defaultKeyRange.byHi;
        }
        if (_sf2->isSet(idPrstInst, champ_velRange))
        {
            rangeTmp = _sf2->get(idPrstInst, champ_velRange).rValue;
            velMin = rangeTmp.byLo;
            velMax = rangeTmp.byHi;
        }
        else
        {
            velMin = defaultVelRange.byLo;
            velMax = defaultVelRange.byHi;
        }

        // Check {key, vel} is in the division and go inside the instruments
        if (keyMin <= key && key <= keyMax && velMin <= velocity && velocity <= velMax)
            this->playInst(idSf2, _sf2->get(idPrstInst, champ_instrument).wValue, key, velocity, idPrstInst);
    }
}

void Synth::playInst(int idSf2, int idElt, int key, int velocity, EltID idPrstInst)
{
    // Default instrument range
    EltID idInst(elementInst, idSf2, idElt, 0, 0);
    idInst.typeElement = elementInst;
    RangesType defaultKeyRange, defaultVelRange;
    if (_sf2->isSet(idInst, champ_keyRange))
        defaultKeyRange = _sf2->get(idInst, champ_keyRange).rValue;
    else
    {
        defaultKeyRange.byLo = 0;
        defaultKeyRange.byHi = 127;
    }
    if (_sf2->isSet(idInst, champ_velRange))
        defaultVelRange = _sf2->get(idInst, champ_velRange).rValue;
    else
    {
        defaultVelRange.byLo = 0;
        defaultVelRange.byHi = 127;
    }

    // Browse the range of all linked samples
    EltID idInstSmpl(elementInstSmpl, idSf2, idElt, 0, 0);
    int keyMin, keyMax, velMin, velMax;
    RangesType rangeTmp;
    foreach (int i, _sf2->getSiblings(idInstSmpl))
    {
        idInstSmpl.indexElt2 = i;

        // Skip muted divisions
        if (_sf2->get(idInstSmpl, champ_mute).bValue > 0)
            continue;

        if (_sf2->isSet(idInstSmpl, champ_keyRange))
        {
            rangeTmp = _sf2->get(idInstSmpl, champ_keyRange).rValue;
            keyMin = rangeTmp.byLo;
            keyMax = rangeTmp.byHi;
        }
        else
        {
            keyMin = defaultKeyRange.byLo;
            keyMax = defaultKeyRange.byHi;
        }
        if (_sf2->isSet(idInstSmpl, champ_velRange))
        {
            rangeTmp = _sf2->get(idInstSmpl, champ_velRange).rValue;
            velMin = rangeTmp.byLo;
            velMax = rangeTmp.byHi;
        }
        else
        {
            velMin = defaultVelRange.byLo;
            velMax = defaultVelRange.byHi;
        }

        // Check {key, vel} is in the division and go inside the samples
        if (keyMin <= key && key <= keyMax && velMin <= velocity && velocity <= velMax)
            this->playSmpl(idSf2, _sf2->get(idInstSmpl, champ_sampleID).wValue, key, velocity, idInstSmpl, idPrstInst);
    }
}

int Synth::playSmpl(int idSf2, int idElt, int key, int velocity, EltID idInstSmpl, EltID idPrstInst)
{
    // Only one -1 or -2 at a time
    if (key < 0)
        SoundEngine::releaseNote(key);

    EltID idSmpl(elementSmpl, idSf2, idElt, 0, 0);

    // Prepare the parameters for the voice
    VoiceParam * voiceParam = new VoiceParam(idPrstInst, idInstSmpl, idSmpl, key, velocity);

    if (key < 0) // Smpl area
        voiceParam->prepareForSmpl(key, _sf2->get(idSmpl, champ_sfSampleType).sfLinkValue);

    // Create a voice
    int currentToken = s_sampleVoiceTokenCounter++;
    Voice * voiceTmp = new Voice(_sf2->getData(idSmpl, champ_sampleData32),
                                 _sf2->get(idSmpl, champ_dwSampleRate).dwValue,
                                 _format.sampleRate(), key, voiceParam, currentToken);

    // Initialize chorus and gain
    if (key < 0)
        voiceTmp->setChorus(0, 0, 0);
    else
    {
        voiceTmp->setChorus(_choLevel, _choDepth, _choFrequency);
        voiceTmp->setGain(_gain);
        voiceTmp->setTuningFork(_tuningFork);
        voiceTmp->setTemperament(_temperament);
    }

    // Add the voice in the list
    _listVoixTmp << voiceTmp;
    SoundEngine::addVoice(voiceTmp, _listVoixTmp);

    if (key == -1) // -2 is the linked sample
    {
        // Position in the graphics
        connect(voiceTmp, SIGNAL(currentPosChanged(quint32)), this, SIGNAL(currentPosChanged(quint32)));

        // Stereo link?
        SFSampleLink typeLien = _sf2->get(idSmpl, champ_sfSampleType).sfLinkValue;
        if (typeLien != monoSample && typeLien != RomMonoSample)
            this->playSmpl(idSf2, _sf2->get(idSmpl, champ_wSampleLink).wValue, -2, 127);
    }

    return currentToken;
}

void Synth::stop()
{
    // Stop required for all voices
    SoundEngine::stopAllVoices();
}

void Synth::updateConfiguration()
{
    this->stop();

    // Update chorus
    _choLevel = _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "cho_level", 0).toInt();
    _choDepth = _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "cho_depth", 0).toInt();
    _choFrequency = _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "cho_frequency", 0).toInt();
    SoundEngine::setChorus(_choLevel, _choDepth, _choFrequency);

    // Update reverb
    double revLevel = 0.01 * _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "rev_level", 0).toInt();
    double revSize = 0.01 * _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "rev_size", 0).toInt();
    double revWidth = 0.01 * _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "rev_width", 0).toInt();
    double revDamping = 0.01 * _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "rev_damping", 0).toInt();

    _mutexReverb.lock();
    _reverb.setEffectMix(revLevel);
    _reverb.setRoomSize(revSize);
    _reverb.setWidth(revWidth);
    _reverb.setDamping(revDamping);
    _mutexReverb.unlock();

    // Update gain, tuning fork and temperament
    _gain = _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "gain", 0).toInt();
    SoundEngine::setGain(_gain);

    _tuningFork = _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "tuning_fork", 440).toInt();
    SoundEngine::setTuningFork(_tuningFork);

    QStringList temperamentArgs = _configuration->getValue(ConfManager::SECTION_SOUND_ENGINE, "temperament", "").toString().split(",");
    if (temperamentArgs.count() == 13)
        for (int i = 1; i < 13; i++)
            _temperament[i - 1] = temperamentArgs[i].toDouble();
    else
        memset(_temperament, 0, 12 * sizeof(double));
    SoundEngine::setTemperament(_temperament);

    // Update buffer size
    quint32 bufferSize = 2 * _configuration->getValue(ConfManager::SECTION_AUDIO, "buffer_size", 512).toUInt();
    if (_bufferSize != bufferSize)
    {
        _bufferSize = bufferSize;
        _mutexSynchro.lock();
        destroySoundEnginesAndBuffers();
        createSoundEnginesAndBuffers();
        _mutexSynchro.unlock();
    }
}

void Synth::setGainSample(int gain)
{
    // Modification du gain des samples
    SoundEngine::setGainSample(gain);
}

void Synth::setStereo(bool isStereo)
{
    // Enable the stereo when playing a sample
    SoundEngine::setStereo(isStereo);
}

bool Synth::isStereo()
{
    return SoundEngine::isStereo();
}

void Synth::setLoopEnabled(bool isEnabled)
{
    // Enable the loop when playing a sample
    SoundEngine::setLoopEnabled(isEnabled);
}

void Synth::setSinus(bool isOn, int rootKey)
{
    _sinus.setPitch(rootKey);
    if (isOn)
        _sinus.on();
    else
        _sinus.off();
}

void Synth::setStartLoop(quint32 startLoop, bool repercute)
{
    // Update voices -1 and -2 if repercussion
    SoundEngine::setStartLoop(startLoop, repercute);
}

void Synth::setEndLoop(quint32 endLoop, bool repercute)
{
    // Update voices -1 and -2  if repercussion
    SoundEngine::setEndLoop(endLoop, repercute);
}

void Synth::setPitchCorrection(qint16 correction, bool repercute)
{
    // Update voices -1 and -2  if repercussion
    SoundEngine::setPitchCorrection(correction, repercute);
}

void Synth::activateSmplEq(bool isActivated)
{
    if (isActivated)
        _eq.on();
    else
        _eq.off();
}

void Synth::setSmplEqValues(QVector<int> values)
{
    _eq.setValues(values);
}

void Synth::setFormat(AudioFormat format)
{
    // Mutex not mandatory: no data generation when "setFormat" is called
    _format = format;

    // Reset
    this->stop();

    // Sample rate update
    _sinus.setSampleRate(format.sampleRate());
    _eq.setSampleRate(format.sampleRate());
    this->sampleRateChanged(format.sampleRate());
}

void Synth::startNewRecord(QString fileName)
{
    _mutexRecord.lock();
    if (_recordFile)
        this->endRecord();
    _recordFile = new QFile(fileName);
    if (_recordFile->open(QIODevice::WriteOnly))
    {
        // Create header
        quint32 dwTemp = 0;
        quint16 wTemp;
        _recordStream.setDevice(_recordFile);
        _recordStream.setByteOrder(QDataStream::LittleEndian);
        _recordLength = 0;
        // header
        _recordStream.writeRawData("RIFF", 4);
        _recordStream << static_cast<quint32>(_recordLength + 18 + 4 + 8 + 8);
        _recordStream.writeRawData("WAVE", 4);
        ///////////// BLOC FMT /////////////
        _recordStream.writeRawData("fmt ", 4);
        dwTemp = 18;
        _recordStream << dwTemp;
        // Compression code
        wTemp = 3;
        _recordStream << wTemp;
        // Number of channels
        wTemp = 2;
        _recordStream << wTemp;
        // Sample rate
        dwTemp = _format.sampleRate();
        _recordStream << dwTemp;
        // Average byte per second
        dwTemp *= 2 * 4;
        _recordStream << dwTemp;
        // Block align
        wTemp = 2 * 4;
        _recordStream << wTemp;
        // Significants bits per smpl
        _recordStream << static_cast<quint16>(32);
        // Extra format bytes
        wTemp = 0;
        _recordStream << wTemp;
        ///////////// BLOC DATA /////////////
        _recordStream.writeRawData("data", 4);
        _recordStream << _recordLength;

        _isRecording = true;
    }
    else
    {
        delete _recordFile;
        _recordFile = nullptr;
    }
    _mutexRecord.unlock();
}

void Synth::endRecord()
{
    _mutexRecord.lock();
    if (_recordFile)
    {
        // Adjust file dimensions
        _recordFile->seek(4);
        _recordStream << static_cast<quint32>(_recordLength + 18 + 4 + 8 + 8);
        _recordFile->seek(42);
        _recordStream << _recordLength;

        // Close file
        _recordStream.setDevice(nullptr);
        _recordFile->close();
        delete _recordFile;
        _recordFile = nullptr;

        _isRecording = false;
    }
    _mutexRecord.unlock();
}

void Synth::pause(bool isOn)
{
    _mutexRecord.lock();
    _isRecording = !isOn;
    _mutexRecord.unlock();
}

void Synth::readData(float *data1, float *data2, quint32 maxlen)
{
    for (quint32 i = 0; i < maxlen; i++)
        data1[i] = data2[i] = _fTmpSumRev1[i] = _fTmpSumRev2[i] = 0;

    // Merge sound engines
    _mutexSynchro.lock();
    for (int i = 0; i < _soundEngines.size(); i++)
        _soundEngines.at(i)->addData(data1, data2, _fTmpSumRev1, _fTmpSumRev2, maxlen);
    _mutexSynchro.unlock();

    // EQ filter (live preview of filtered samples)
    _eq.filterData(data1, data2, maxlen);

    // Apply reverb and add data
    _mutexReverb.lock();
    for (quint32 i = 0; i < maxlen; i++)
    {
        data1[i] += static_cast<float>(
                    _reverb.tick(static_cast<double>(_fTmpSumRev1[i]), static_cast<double>(_fTmpSumRev2[i])));
        data2[i] += static_cast<float>(_reverb.lastOut(1));
    }
    _mutexReverb.unlock();

    // Add calibrating sinus
    _sinus.addData(data1, data2, maxlen);

    // Clipping
    clip(data1, data2, maxlen);

    // Possibly record in a file
    _mutexRecord.lock();
    if (_recordFile && _isRecording)
    {
        // Interleave and write
        for (quint32 i = 0; i < maxlen; i++)
        {
            _dataWav[2 * i + 1] = data1[i];
            _dataWav[2 * i]     = data2[i];
        }
        _recordStream.writeRawData(reinterpret_cast<char*>(_dataWav), static_cast<int>(maxlen * 8));

        // Prise en compte de l'avance
        _recordLength += maxlen * 8;
        emit(dataWritten(_format.sampleRate(), maxlen));
    }
    _mutexRecord.unlock();
}
