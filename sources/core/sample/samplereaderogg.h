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

#ifndef SAMPLEREADEROGG_H
#define SAMPLEREADEROGG_H

#include "samplereader.h"

class SampleReaderOgg : public SampleReader
{
public:
    SampleReaderOgg(QString filename);
    ~SampleReaderOgg() override {}

    // Extract general information (sampling rate, ...)
    SampleReaderResult getInfo(QFile &fi, InfoSound &info) override;

    // Get sample data (16 bits)
    SampleReaderResult getData16(QFile &fi, QByteArray &smpl) override;

    // Get sample data (extra 8 bits)
    SampleReaderResult getExtraData24(QFile &fi, QByteArray &sm24) override;

private:
    SampleReaderResult launchDecoder(bool justMetadata);

    QFile * _file;
    InfoSound * _info;
    QByteArray * _data;
    bool _readExtra8;
};

#endif // SAMPLEREADEROGG_H
