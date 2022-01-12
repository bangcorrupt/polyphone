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

#include "sampleutils.h"
#include <QMessageBox>

SampleUtils::SampleUtils()
{

}

QByteArray SampleUtils::resampleMono(QByteArray baData, double echInit, quint32 echFinal, quint16 wBps)
{
    // Paramètres
    double alpha = 3;
    qint32 nbPoints = 10;

    // Préparation signal d'entrée
    baData = bpsConversion(baData, wBps, 32);
    if (echFinal < echInit)
    {
        // Filtre passe bas (voir sinc filter)
        baData = SampleUtils::bandFilter(baData, 32, echInit, echFinal / 2, 0, -1);
    }
    quint32 sizeInit = static_cast<quint32>(baData.size()) / 4;
    qint32 * dataI = reinterpret_cast<qint32*>(baData.data());
    double * data = new double[sizeInit]; // utilisation de new sinon possibilité de dépasser une limite de mémoire
    for (quint32 i = 0; i < sizeInit; i++)
        data[i] = static_cast<double>(dataI[i]) / 2147483648.;

    // Création fenêtre Kaiser-Bessel 2048 points
    double kbdWindow[2048];
    KBDWindow(kbdWindow, 2048, alpha);

    // Nombre de points à trouver
    quint32 sizeFinal = static_cast<quint32>(1. + (sizeInit - 1.0) * echFinal / echInit);
    double * dataRet = new double[sizeFinal]; // utilisation de new : même raison

    // Calcul des points par interpolation à bande limitée
    double pos, delta;
    qint32 pos1, pos2;
    double * sincCoef = new double[1 + 2 * static_cast<quint32>(nbPoints)];
    double valMax = 0;
    for (quint32 i = 0; i < sizeFinal; i++)
    {
        // Position à interpoler
        pos = (echInit * i) / echFinal;

        // Calcul des coefs
        for (qint32 j = -nbPoints; j <= nbPoints; j++)
        {
            delta = pos - floor(pos);

            // Calcul du sinus cardinal
            sincCoef[j + nbPoints] = sinc(M_PI * (static_cast<double>(j) - delta));

            // Application fenêtre
            delta = static_cast<double>(j + nbPoints - delta) / (1 + 2 * nbPoints) * 2048;
            pos1 = static_cast<qint32>(qMax(0., qMin(floor(delta), 2047.)) + .5);
            pos2 = static_cast<qint32>(qMax(0., qMin(ceil (delta), 2047.)) + .5);
            sincCoef[j + nbPoints] *= kbdWindow[pos1] * (ceil((delta)) - delta)
                    + kbdWindow[pos2] * (1. - ceil((delta)) + delta);
        }
        // Valeur
        dataRet[i] = 0;
        for (int j = qMax(0, static_cast<qint32>(pos) - nbPoints);
             j <= qMin(static_cast<qint32>(sizeInit) - 1, static_cast<qint32>(pos) + nbPoints); j++)
            dataRet[i] += sincCoef[j - static_cast<qint32>(pos) + nbPoints] * data[j];

        valMax = qMax(valMax, qAbs(dataRet[i]));
    }
    delete [] sincCoef;

    // Passage qint32 et limitation si besoin
    QByteArray baRet;
    baRet.resize(static_cast<int>(sizeFinal) * 4);
    qint32 * dataRetI = reinterpret_cast<qint32 *>(baRet.data());
    double coef;
    if (valMax > 1)
        coef = 2147483648. / valMax;
    else
        coef = 2147483648LL;
    for (quint32 i = 0; i < sizeFinal; i++)
        dataRetI[i] = static_cast<qint32>(dataRet[i] * coef);

    delete [] dataRet;
    delete [] data;

    // Filtre passe bas après resampling
    baRet = SampleUtils::bandFilter(baRet, 32, echFinal, echFinal / 2, 0, -1);
    baRet = bpsConversion(baRet, 32, wBps);
    return baRet;
}

QByteArray SampleUtils::bandFilter(QByteArray baData, quint16 wBps, double dwSmplRate, double fBas, double fHaut, int ordre)
{
    /******************************************************************************
     ***********************    passe_bande_frequentiel    ************************
     ******************************************************************************
     * But :
     *  - filtre un signal par un filtre passe-bande de Butterworth
     * Entrees :
     *  - baData : tableau contenant les donnees a filtrer
     *  - dwSmplRate : frequence d'echantillonnage du signal
     *  - fHaut : frequence de coupure du passe-haut
     *  - fBas : frequence de coupure du passe-bas
     *  - ordre : ordre du filtre
     * Sorties :
     *  - tableau contenant les donnees apres filtrage
     ******************************************************************************/

    // Paramètres valides ?
    if (dwSmplRate < 1 || (fHaut <= 0 && fBas <= 0) || 2 * fHaut > dwSmplRate || 2 * fBas > dwSmplRate)
    {
        // Controle des fréquences de coupures (il faut que Fc<Fe/2 )
        return baData;
    }

    quint32 size;

    // Conversion de baData en complexes
    Complex * cpxData;
    cpxData = fromBaToComplex(baData, wBps, size);

    // Calculer la fft du signal
    Complex * fc_sortie_fft = FFT(cpxData, size);
    delete [] cpxData;

    // Convoluer par le filtre Butterworth d'ordre 4, applique dans le sens direct et retrograde
    // pour supprimer la phase (Hr4 * H4 = Gr4 * G4 = (G4)^2)
    double d_gain_ph, d_gain_pb;
    if (fHaut <= 0)
    {
        double pos;

        // Filtre passe bas uniquement
        if (ordre == -1)
        {
            // "Mur de brique"
            for (unsigned long i = 0; i < (size + 1) / 2; i++)
            {
                pos = static_cast<double>(i) / (size - 1);
                fc_sortie_fft[i] *= (pos * dwSmplRate) < fBas;
                fc_sortie_fft[size - 1 - i] *= (pos * dwSmplRate) < fBas;
            }
        }
        else
        {
            for (unsigned long i = 0; i < (size + 1) / 2; i++)
            {
                pos = static_cast<double>(i) / (size - 1);
                d_gain_pb = 1.0 / (1.0 + pow(pos * dwSmplRate / fBas, 2 * ordre));
                fc_sortie_fft[i] *= d_gain_pb;
                fc_sortie_fft[size - 1 - i] *= d_gain_pb;
            }
        }
    }
    else if (fBas <= 0)
    {
        double pos;

        // Filtre passe haut uniquement
        if (ordre == -1)
        {
            // "Mur de brique"
            for (unsigned long i = 0; i < (size + 1) / 2; i++)
            {
                pos = static_cast<double>(i) / (size - 1);
                fc_sortie_fft[i] *= (pos * dwSmplRate) > fHaut;
                fc_sortie_fft[size - 1 - i] *= (pos * dwSmplRate) > fHaut;
            }
        }
        else
        {
            for (unsigned long i = 0; i < (size + 1) / 2; i++)
            {
                pos = static_cast<double>(i) / (size - 1);
                d_gain_ph = 1 - (1.0 / (1.0 + pow((pos * dwSmplRate) / fHaut, 2 * ordre)));
                fc_sortie_fft[i] *= d_gain_ph;
                fc_sortie_fft[size - 1 - i] *= d_gain_ph;
            }
        }
    }
    else
    {
        double pos;

        // Filtre passe bande
        for (unsigned long i = 0; i < (size+1)/2; i++)
        {
            pos = static_cast<double>(i) / (size - 1);
            d_gain_ph = 1 - (1.0 / (1.0 + pow((pos * dwSmplRate) / fHaut, 2 * ordre)));
            d_gain_pb = 1.0 / (1.0 + pow(pos * dwSmplRate / fBas, 2 * ordre));
            fc_sortie_fft[i] *= d_gain_ph * d_gain_pb;
            fc_sortie_fft[size-1-i] *= d_gain_ph * d_gain_pb;
        }
    }

    // Calculer l'ifft du signal
    cpxData = IFFT(fc_sortie_fft, size);
    delete [] fc_sortie_fft;
    // Prise en compte du facteur d'echelle
    for (unsigned long i = 0; i < size; i++)
        cpxData[i].real(cpxData[i].real() / size);

    // Retour en QByteArray
    QByteArray baRet;
    baRet = fromComplexToBa(cpxData, baData.size() * 8 / wBps, wBps);
    delete [] cpxData;
    return baRet;
}

QByteArray SampleUtils::cutFilter(QByteArray baData, quint32 dwSmplRate, QVector<double> dValues, quint16 wBps, int maxFreq)
{
    // Convert baData in complex
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    quint32 size;
    Complex * cpxData = fromBaToComplex(baData, 32, size);

    // Compute the fft
    Complex * fc_sortie_fft = FFT(cpxData, size);
    delete [] cpxData;

    // Get the maximum module of the FFT
    double moduleMax = 0;
    for (unsigned long i = 0; i < (size + 1) / 2; i++)
    {
        // Left side
        double module = sqrt(fc_sortie_fft[i].imag() * fc_sortie_fft[i].imag() +
                             fc_sortie_fft[i].real() * fc_sortie_fft[i].real());
        moduleMax = qMax(moduleMax, module);

        // Right side
        module = sqrt(fc_sortie_fft[size-1-i].imag() * fc_sortie_fft[size-1-i].imag() +
                fc_sortie_fft[size-1-i].real() * fc_sortie_fft[size-1-i].real());
        moduleMax = qMax(moduleMax, module);
    }

    // Cut the frequencies according to dValues (representing maximum intensities from minFreq to maxFreq)
    int nbValues = dValues.count();
    for (unsigned long i = 0; i < (size + 1) / 2; i++)
    {
        // Current frequency and current module
        double freq = static_cast<double>(dwSmplRate * i) / (size - 1);
        double module1 = sqrt(fc_sortie_fft[i].imag() * fc_sortie_fft[i].imag() +
                              fc_sortie_fft[i].real() * fc_sortie_fft[i].real());
        double module2 = sqrt(fc_sortie_fft[size - 1 - i].imag() * fc_sortie_fft[size - 1 - i].imag() +
                fc_sortie_fft[size - 1 - i].real() * fc_sortie_fft[size - 1 - i].real());

        // Module max
        double limit = moduleMax;
        int index1 = static_cast<int>(freq / maxFreq * dValues.count());
        if (index1 >= nbValues - 1)
            limit *= dValues[nbValues - 1];
        else
        {
            double x1 = static_cast<double>(index1) / nbValues * maxFreq;
            double y1 = dValues[index1];
            double x2 = static_cast<double>(index1 + 1) / nbValues * maxFreq;
            double y2 = dValues[index1 + 1];
            limit *= ((freq - x1) / (x2 - x1)) * (y2 - y1) + y1;
        }

        // Cut the frequency if it's above the limit
        if (module1 > limit)
            fc_sortie_fft[i] *= limit / module1;
        if (module2 > limit)
            fc_sortie_fft[size - 1 - i] *= limit / module2;
    }

    // Calculer l'ifft du signal
    cpxData = IFFT(fc_sortie_fft, size);
    delete [] fc_sortie_fft;

    // Prise en compte du facteur d'echelle
    for (unsigned long i = 0; i < size; i++)
        cpxData[i].real(cpxData[i].real() / size);

    // Retour en QByteArray
    QByteArray baRet = fromComplexToBa(cpxData, baData.size() * 8 / 32, 32);
    delete [] cpxData;

    // retour wBps si nécessaire
    if (wBps != 32)
        baRet = bpsConversion(baRet, 32, wBps);

    return baRet;
}

QByteArray SampleUtils::EQ(QByteArray baData, quint32 dwSmplRate, quint16 wBps, int i1, int i2, int i3, int i4, int i5,
                           int i6, int i7, int i8, int i9, int i10)
{
    quint32 size;

    // Conversion de baData en complexes
    Complex * cpxData;
    cpxData = fromBaToComplex(baData, wBps, size);

    // Calculer la fft du signal
    Complex * fc_sortie_fft = FFT(cpxData, size);
    delete [] cpxData;
    // Filtrage
    double freq;
    double gain;
    for (unsigned long i = 0; i < (size + 1) / 2; i++)
    {
        freq = static_cast<double>(i * dwSmplRate) / (size - 1);
        gain = gainEQ(freq, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10);
        fc_sortie_fft[i] *= gain;
        fc_sortie_fft[size - 1 - i] *= gain;
    }

    // Calculer l'ifft du signal
    cpxData = IFFT(fc_sortie_fft, size);
    delete [] fc_sortie_fft;

    // Prise en compte du facteur d'echelle
    for (unsigned long i = 0; i < size; i++)
        cpxData[i].real(cpxData[i].real() / size);

    // Retour en QByteArray
    QByteArray baRet;
    baRet = fromComplexToBa(cpxData, baData.size() * 8 / wBps, wBps);
    delete [] cpxData;
    return baRet;
}

Complex * SampleUtils::FFT(Complex * x, quint32 N)
{
    Complex* out = new Complex[N];
    Complex* scratch = new Complex[N];
    Complex* twiddles = new Complex [N];
    quint32 k;
    for (k = 0; k != N; ++k)
    {
        twiddles[k].real(cos(-2.0 * M_PI * k / N));
        twiddles[k].imag(sin(-2.0 * M_PI * k / N));
    }
    FFT_calculate(x, N, out, scratch, twiddles);
    delete [] twiddles;
    delete [] scratch;
    return out;
}

Complex * SampleUtils::IFFT(Complex * x, quint32 N)
{
    Complex * out = new Complex[N];
    Complex * scratch = new Complex[N];
    Complex * twiddles = new Complex [N];
    quint32 k;
    for (k = 0; k != N; ++k)
    {
        twiddles[k].real(cos(2.0 * M_PI * k / N));
        twiddles[k].imag(sin(2.0 * M_PI * k / N));
    }
    FFT_calculate(x, N, out, scratch, twiddles);
    delete [] twiddles;
    delete [] scratch;
    return out;
}

void SampleUtils::bpsConversion(char *cDest, const char *cFrom, qint32 size, quint16 wBpsInit, quint16 wBpsFinal, bool bigEndian)
{
    // Conversion entre formats 32, 24 et 16 bits
    // Particularité : demander format 824 bits renvoie les 8 bits de poids faible
    //                 dans les 24 bits de poids fort

    // Remplissage
    switch (wBpsInit)
    {
    case 8:
        switch (wBpsFinal)
        {
        case 824:
            // Remplissage de 0
            for (int i = 0; i < size; i++)
                cDest[i] = 0;
            break;
        case 8:
            for (int i = 0; i < size; i++)
                cDest[i] = cFrom[i];
            break;
        case 16:
            if (bigEndian)
            {
                for (int i = 0; i < size; i++)
                {
                    cDest[2*i+1] = 0;
                    cDest[2*i] = cFrom[i];
                }
            }
            else
            {
                for (int i = 0; i < size; i++)
                {
                    cDest[2*i] = 0;
                    cDest[2*i+1] = cFrom[i];
                }
            }
            break;
        case 24:
            if (bigEndian)
            {
                for (int i = 0; i < size; i++)
                {
                    cDest[3*i+2] = 0;
                    cDest[3*i+1] = 0;
                    cDest[3*i] = cFrom[i];
                }
            }
            else
            {
                for (int i = 0; i < size; i++)
                {
                    cDest[3*i] = 0;
                    cDest[3*i+1] = 0;
                    cDest[3*i+2] = cFrom[i];
                }
            }
            break;
        case 32:
            if (bigEndian)
            {
                for (int i = 0; i < size; i++)
                {
                    cDest[4*i+3] = 0;
                    cDest[4*i+2] = 0;
                    cDest[4*i+1] = 0;
                    cDest[4*i] = cFrom[i];
                }
            }
            else
            {
                for (int i = 0; i < size; i++)
                {
                    cDest[4*i] = 0;
                    cDest[4*i+1] = 0;
                    cDest[4*i+2] = 0;
                    cDest[4*i+3] = cFrom[i];
                }
            }
            break;
        }
        break;
    case 16:
        switch (wBpsFinal)
        {
        case 824:
            // Remplissage de 0
            for (int i = 0; i < size/2; i++)
                cDest[i] = 0;
            break;
        case 8:
            for (int i = 0; i < size/2; i++)
                cDest[i] = cFrom[2*i+1];
            break;
        case 16:
            if (bigEndian)
            {
                for (int i = 0; i < size/2; i++)
                {
                    cDest[2*i+1] = cFrom[2*i];
                    cDest[2*i] = cFrom[2*i+1];
                }
            }
            else
                for (int i = 0; i < size; i++)
                    cDest[i] = cFrom[i];
            break;
        case 24:
            if (bigEndian)
            {
                for (int i = 0; i < size/2; i++)
                {
                    cDest[3*i+2] = 0;
                    cDest[3*i+1] = cFrom[2*i];
                    cDest[3*i] = cFrom[2*i+1];
                }
            }
            else
            {
                for (int i = 0; i < size/2; i++)
                {
                    cDest[3*i] = 0;
                    cDest[3*i+1] = cFrom[2*i];
                    cDest[3*i+2] = cFrom[2*i+1];
                }
            }
            break;
        case 32:
            if (bigEndian)
            {
                for (int i = 0; i < size/2; i++)
                {
                    cDest[4*i+3] = 0;
                    cDest[4*i+2] = 0;
                    cDest[4*i+1] = cFrom[2*i];
                    cDest[4*i] = cFrom[2*i+1];
                }
            }
            else
            {
                for (int i = 0; i < size/2; i++)
                {
                    cDest[4*i] = 0;
                    cDest[4*i+1] = 0;
                    cDest[4*i+2] = cFrom[2*i];
                    cDest[4*i+3] = cFrom[2*i+1];
                }
            }
            break;
        }
        break;
    case 24:
        switch (wBpsFinal)
        {
        case 824:
            // 8 bits de poids faible
            for (int i = 0; i < size/3; i++)
                cDest[i] = cFrom[3*i];
            break;
        case 8:
            for (int i = 0; i < size/3; i++)
                cDest[i] = cFrom[3*i+2];
            break;
        case 16:
            if (bigEndian)
            {
                for (int i = 0; i < size/3; i++)
                {
                    cDest[2*i+1] = cFrom[3*i+1];
                    cDest[2*i] = cFrom[3*i+2];
                }
            }
            else
            {
                for (int i = 0; i < size/3; i++)
                {
                    cDest[2*i] = cFrom[3*i+1];
                    cDest[2*i+1] = cFrom[3*i+2];
                }
            }
            break;
        case 24:
            if (bigEndian)
            {
                for (int i = 0; i < size/3; i++)
                {
                    cDest[3*i+2] = cFrom[3*i];
                    cDest[3*i+1] = cFrom[3*i+1];
                    cDest[3*i] = cFrom[3*i+2];
                }
            }
            else
                for (int i = 0; i < size; i++)
                    cDest[i] = cFrom[i];
            break;
        case 32:
            if (bigEndian)
            {
                for (int i = 0; i < size/3; i++)
                {
                    cDest[4*i+3] = 0;
                    cDest[4*i+2] = cFrom[3*i];
                    cDest[4*i+1] = cFrom[3*i+1];
                    cDest[4*i] = cFrom[3*i+2];
                }
            }
            else
            {
                for (int i = 0; i < size/3; i++)
                {
                    cDest[4*i] = 0;
                    cDest[4*i+1] = cFrom[3*i];
                    cDest[4*i+2] = cFrom[3*i+1];
                    cDest[4*i+3] = cFrom[3*i+2];
                }
            }
            break;
        }
        break;
    case 32:
        switch (wBpsFinal)
        {
        case 824:
            // 8 bits poids faible après 16
            for (int i = 0; i < size/4; i++)
                cDest[i] = cFrom[4*i+1];
            break;
        case 8:
            for (int i = 0; i < size/4; i++)
                cDest[i] = cFrom[4*i+3];
            break;
        case 16:
            if (bigEndian)
            {
                for (int i = 0; i < size/4; i++)
                {
                    cDest[2*i+1] = cFrom[4*i+2];
                    cDest[2*i] = cFrom[4*i+3];
                }
            }
            else
            {
                for (int i = 0; i < size/4; i++)
                {
                    cDest[2*i] = cFrom[4*i+2];
                    cDest[2*i+1] = cFrom[4*i+3];
                }
            }
            break;
        case 24:
            if (bigEndian)
            {
                for (int i = 0; i < size/4; i++)
                {
                    cDest[3*i+2] = cFrom[4*i+1];
                    cDest[3*i+1] = cFrom[4*i+2];
                    cDest[3*i] = cFrom[4*i+3];
                }
            }
            else
            {
                for (int i = 0; i < size/4; i++)
                {
                    cDest[3*i] = cFrom[4*i+1];
                    cDest[3*i+1] = cFrom[4*i+2];
                    cDest[3*i+2] = cFrom[4*i+3];
                }
            }
            break;
        case 32:
            if (bigEndian)
            {
                for (int i = 0; i < size/4; i++)
                {
                    cDest[4*i+3] = cFrom[4*i];
                    cDest[4*i+2] = cFrom[4*i+1];
                    cDest[4*i+1] = cFrom[4*i+2];
                    cDest[4*i] = cFrom[4*i+3];
                }
            }
            else
                for (int i = 0; i < size; i++)
                    cDest[i] = cFrom[i];
            break;
        }
        break;
    }
}

QByteArray SampleUtils::bpsConversion(QByteArray baData, quint16 wBpsInit, quint16 wBpsFinal, bool bigEndian)
{
    // Conversion entre formats 32, 24 et 16 bits
    // Particularité : demander format 824 bits renvoie les 8 bits de poids faible
    //                 dans les 24 bits de poids fort
    int size = baData.size();

    // Données de retour
    QByteArray baRet;

    // Redimensionnement
    int i = 1;
    int j = 1;
    switch (wBpsInit)
    {
    case 16: i = 2; break;
    case 24: i = 3; break;
    case 32: i = 4; break;
    default: i = 1;
    }
    switch (wBpsFinal)
    {
    case 16: j = 2; break;
    case 24: j = 3; break;
    case 32: j = 4; break;
    default: j = 1;
    }
    baRet.resize((size * j) / i);

    // Remplissage
    char * cDest = baRet.data();
    const char * cFrom = baData.constData();
    bpsConversion(cDest, cFrom, size, wBpsInit, wBpsFinal, bigEndian);
    return baRet;
}

QByteArray SampleUtils::from2MonoTo1Stereo(QByteArray baData1, QByteArray baData2, quint16 wBps, bool bigEndian)
{
    int size;
    // Si tailles différentes, on complète le petit avec des 0
    if (baData1.size() < baData2.size())
    {
        QByteArray baTmp;
        baTmp.fill(0, baData2.size() - baData1.size());
        baData1.append(baTmp);
    }
    else if (baData2.size() < baData1.size())
    {
        QByteArray baTmp;
        baTmp.fill(0, baData1.size() - baData2.size());
        baData2.append(baTmp);
    }
    size = baData1.size();
    // Assemblage
    QByteArray baRet;
    baRet.resize(2 * size);
    char * cDest = baRet.data();
    const char * cFrom1 = baData1.constData();
    const char * cFrom2 = baData2.constData();
    if (wBps == 32)
    {
        if (bigEndian)
        {
            for (int i = 0; i < size/4; i++)
            {
                cDest[8*i] = cFrom1[4*i+3];
                cDest[8*i+1] = cFrom1[4*i+2];
                cDest[8*i+2] = cFrom1[4*i+1];
                cDest[8*i+3] = cFrom1[4*i];
                cDest[8*i+4] = cFrom2[4*i+3];
                cDest[8*i+5] = cFrom2[4*i+2];
                cDest[8*i+6] = cFrom2[4*i+1];
                cDest[8*i+7] = cFrom2[4*i];
            }
        }
        else
        {
            for (int i = 0; i < size/4; i++)
            {
                cDest[8*i] = cFrom1[4*i];
                cDest[8*i+1] = cFrom1[4*i+1];
                cDest[8*i+2] = cFrom1[4*i+2];
                cDest[8*i+3] = cFrom1[4*i+3];
                cDest[8*i+4] = cFrom2[4*i];
                cDest[8*i+5] = cFrom2[4*i+1];
                cDest[8*i+6] = cFrom2[4*i+2];
                cDest[8*i+7] = cFrom2[4*i+3];
            }
        }
    }
    else if (wBps == 24)
    {
        if (bigEndian)
        {
            for (int i = 0; i < size/3; i++)
            {
                cDest[6*i] = cFrom1[3*i+2];
                cDest[6*i+1] = cFrom1[3*i+1];
                cDest[6*i+2] = cFrom1[3*i];
                cDest[6*i+3] = cFrom2[3*i+2];
                cDest[6*i+4] = cFrom2[3*i+1];
                cDest[6*i+5] = cFrom2[3*i];
            }
        }
        else
        {
            for (int i = 0; i < size/3; i++)
            {
                cDest[6*i] = cFrom1[3*i];
                cDest[6*i+1] = cFrom1[3*i+1];
                cDest[6*i+2] = cFrom1[3*i+2];
                cDest[6*i+3] = cFrom2[3*i];
                cDest[6*i+4] = cFrom2[3*i+1];
                cDest[6*i+5] = cFrom2[3*i+2];
            }
        }
    }
    else
    {
        if (bigEndian)
        {
            for (int i = 0; i < size/2; i++)
            {
                cDest[4*i] = cFrom1[2*i+1];
                cDest[4*i+1] = cFrom1[2*i];
                cDest[4*i+2] = cFrom2[2*i+1];
                cDest[4*i+3] = cFrom2[2*i];
            }
        }
        else
        {
            for (int i = 0; i < size/2; i++)
            {
                cDest[4*i] = cFrom1[2*i];
                cDest[4*i+1] = cFrom1[2*i+1];
                cDest[4*i+2] = cFrom2[2*i];
                cDest[4*i+3] = cFrom2[2*i+1];
            }
        }
    }
    return baRet;
}

QVector<float> SampleUtils::getFourierTransform(QVector<float> input)
{
    quint32 size = 0;
    Complex * cpxData = fromBaToComplex(input, size);
    Complex * fc_sortie_fft = FFT(cpxData, size);
    delete [] cpxData;
    QVector<float> vectFourier;
    vectFourier.resize(size / 2);
    for (quint32 i = 0; i < size / 2; i++)
    {
        vectFourier[static_cast<int>(i)] =
                static_cast<float>(0.5 * qSqrt(fc_sortie_fft[i].real() * fc_sortie_fft[i].real() +
                                               fc_sortie_fft[i].imag() * fc_sortie_fft[i].imag()));
        vectFourier[static_cast<int>(i)] +=
                static_cast<float>(0.5 * qSqrt(fc_sortie_fft[size-i-1].real() * fc_sortie_fft[size-i-1].real() +
                fc_sortie_fft[size-i-1].imag() * fc_sortie_fft[size-i-1].imag()));
    }
    delete [] fc_sortie_fft;

    return vectFourier;
}

Complex * SampleUtils::fromBaToComplex(QVector<float> fData, quint32 &size)
{
    // Nombre de données (puissance de 2 la plus proche)
    quint32 nb = static_cast<quint32>(ceil(qLn(fData.size()) / 0.69314718056));
    size = 1;
    for (quint32 i = 0; i < nb; i++)
        size *= 2;

    // Création et remplissage d'un tableau de complexes
    Complex * cpxData = new Complex[size];

    // Remplissage
    for (int i = 0; i < fData.size(); i++)
    {
        cpxData[i].real(static_cast<double>(fData[i]));
        cpxData[i].imag(0);
    }

    // On complète avec des 0
    for (quint32 i = static_cast<quint32>(fData.size()); i < size; i++)
    {
        cpxData[i].real(0);
        cpxData[i].imag(0);
    }

    return cpxData;
}

Complex * SampleUtils::fromBaToComplex(QByteArray baData, quint16 wBps, quint32 &size)
{
    Complex * cpxData;
    // Création et remplissage d'un tableau de complexes
    if (wBps == 16)
    {
        qint16 * data = reinterpret_cast<qint16 *>(baData.data());

        // Nombre de données (puissance de 2 la plus proche)
        quint32 nb = static_cast<quint32>(ceil(qLn(baData.size() / 2) / 0.69314718056 /* ln(2) */));
        size = 1;
        for (quint32 i = 0; i < nb; i++)
            size *= 2;

        // Remplissage
        cpxData = new Complex[size];
        for (int i = 0; i < baData.size() / 2; i++)
        {
            cpxData[i].real(data[i]);
            cpxData[i].imag(0);
        }

        // On complète avec des 0
        for (quint32 i = static_cast<quint32>(baData.size()) / 2; i < size; i++)
        {
            cpxData[i].real(0);
            cpxData[i].imag(0);
        }
    }
    else
    {
        // Passage 32 bits si nécessaire
        if (wBps == 24)
            baData = bpsConversion(baData, 24, 32);
        qint32 * data = reinterpret_cast<qint32 *>(baData.data());

        // Nombre de données (puissance de 2 la plus proche)
        quint32 nb = static_cast<quint32>(ceil(qLn(baData.size()/4) / 0.69314718056 /* ln(2) */));
        size = 1;
        for (quint32 i = 0; i < nb; i++)
            size *= 2;

        // Remplissage
        cpxData = new Complex[size];
        for (int i = 0; i < baData.size()/4; i++)
        {
            cpxData[i].real(data[i]);
            cpxData[i].imag(0);
        }

        // On complète avec des 0
        for (quint32 i = static_cast<quint32>(baData.size()) / 4; i < size; i++)
        {
            cpxData[i].real(0);
            cpxData[i].imag(0);
        }
    }
    return cpxData;
}

QByteArray SampleUtils::fromComplexToBa(Complex * cpxData, int size, quint16 wBps)
{
    QByteArray baData;
    if (wBps == 16)
    {
        // Calcul du maximum
        quint64 valMax = 0;
        for (int i = 0; i < size; i++)
            valMax = qMax(valMax, static_cast<quint64>(qAbs(cpxData[i].real())));

        // Atténuation si dépassement de la valeur max
        double att = 1;
        if (valMax > 32700)
            att = 32700. / valMax;

        // Conversion qint16
        baData.resize(size*2);
        qint16 * data = reinterpret_cast<qint16 *>(baData.data());
        for (int i = 0; i < size; i++)
            data[i] = static_cast<qint16>(cpxData[i].real() * att);
    }
    else
    {
        // Calcul du maximum
        quint64 valMax = 0;
        for (int i = 0; i < size; i++)
            valMax = qMax(valMax, static_cast<quint64>(qAbs(cpxData[i].real())));

        // Atténuation si dépassement de la valeur max
        double att = 1;
        if (valMax > 2147483000)
            att = 2147483000. / valMax;

        // Conversion qint32
        baData.resize(size*4);
        qint32 * data = reinterpret_cast<qint32 *>(baData.data());
        for (int i = 0; i < size; i++)
            data[i] = static_cast<qint32>(cpxData[i].real() * att);

        // Conversion 24 bits
        if (wBps == 24)
            baData = bpsConversion(baData, 32, 24);
    }
    return baData;
}

QByteArray SampleUtils::normaliser(QByteArray baData, double dVal, quint16 wBps, double &db)
{
    // Conversion 32 bits si nécessaire
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    // Calcul valeur max
    qint32 * data = reinterpret_cast<qint32 *>(baData.data());
    qint32 valMax = 0;
    for (int i = 0; i < baData.size()/4; i++) valMax = qMax(valMax, qAbs(data[i]));
    // Calcul amplification
    double mult = dVal * 2147483648. / valMax;
    db = 20.0 * log10(mult);
    // Amplification
    for (int i = 0; i < baData.size()/4; i++) data[i] *= mult;
    // Conversion format d'origine si nécessaie
    if (wBps != 32)
        baData = bpsConversion(baData, 32, wBps);
    return baData;
}

QByteArray SampleUtils::multiplier(QByteArray baData, double dMult, quint16 wBps, double &db)
{
    // Conversion 32 bits si nécessaire
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    // Calcul amplification
    db = 20.0 * log10(dMult);
    // Amplification
    qint32 * data = reinterpret_cast<qint32 *>(baData.data());
    for (int i = 0; i < baData.size()/4; i++) data[i] *= dMult;
    // Conversion format d'origine si nécessaie
    if (wBps != 32)
        baData = bpsConversion(baData, 32, wBps);
    return baData;
}

void SampleUtils::removeBlankStep1(QByteArray baData24, quint32 &pos1, quint32 &pos2)
{
    // Thresholds
    const qint16 threshold1Int = 10;
    const qint16 threshold2Int = 20;

    // Compute the number of elements to skip
    pos1 = pos2 = 0;
    bool pos1Found = false;
    bool pos2Found = false;
    quint32 size = static_cast<quint32>(baData24.size()) / 3;
    for (quint32 i = 0; i < size; i++)
    {
        qint16 value = reinterpret_cast<qint16*>(&baData24.data()[3 * i + 1])[0];
        if (value < 0)
            value = -value;
        if (!pos1Found && value > threshold1Int)
        {
            pos1 = i;
            pos1Found = true;
        }
        if (!pos2Found && value > threshold2Int)
        {
            pos2 = i;
            pos2Found = true;
        }
        if (pos1Found && pos2Found)
            break;
    }
}

QByteArray SampleUtils::removeBlankStep2(QByteArray baData24, quint32 pos)
{
    // Skip the first points
    if (3 * pos + 8 < static_cast<quint32>(baData24.size()))
        baData24 = baData24.mid(static_cast<int>(pos) * 3, baData24.size() - 3 * static_cast<int>(pos));
    return baData24;
}

void SampleUtils::regimePermanent(QByteArray baData, quint32 dwSmplRate, quint16 wBps, quint32 &posStart, quint32 &posEnd)
{
    // Recherche d'un régiment permanent (sans attaque ni release)
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    qint32 size = baData.size() / 4;

    QVector<float> fData;
    fData.resize(size);
    qint32 * iData = reinterpret_cast<qint32 *>(baData.data());
    for (int i = 0; i < size; i++)
        fData[i] = static_cast<float>(iData[i]);

    regimePermanent(fData, dwSmplRate, posStart, posEnd);
}

void SampleUtils::regimePermanent(QVector<float> fData, quint32 dwSmplRate, quint32 &posStart, quint32 &posEnd)
{
    quint32 size = static_cast<quint32>(fData.size());

    // Recherche fine
    regimePermanent(fData, dwSmplRate, posStart, posEnd, 10, 1.05f);
    if (posEnd < size / 2 + posStart)
    {
        // Recherche grossière
        regimePermanent(fData, dwSmplRate, posStart, posEnd, 7, 1.2f);
        if (posEnd < size / 2 + posStart)
        {
            // Recherche très grossière
            regimePermanent(fData, dwSmplRate, posStart, posEnd, 4, 1.35f);
            if (posEnd < size / 2 + posStart)
            {
                // moitié du milieu
                posStart = size / 4;
                posEnd = size * 3 / 4;
            }
        }
    }
}

QVector<float> SampleUtils::correlation(const float * fData, quint32 size, quint32 dwSmplRate, quint32 fMin, quint32 fMax, quint32 &dMin)
{
    QVector<float> vectCorrel;
    if (size < 10)
        return vectCorrel;

    // Décalage max (fréquence basse)
    quint32 dMax = dwSmplRate / fMin;
    if (dMax >= size / 2)
        dMax = size / 2 - 1;

    // Décalage min (fréquence haute)
    dMin = dwSmplRate / fMax;

    // Calcul de la corrélation
    if (dMax + 1 <= dMin)
        return vectCorrel;
    vectCorrel.resize(static_cast<int>(dMax - dMin + 1));

    double qTmp;
    float fTmp;
    for (quint32 i = dMin; i <= dMax; ++i)
    {
        // Mesure de la ressemblance
        qTmp = 0;
        for (quint32 j = 0; j < size - dMax; j++)
        {
            fTmp = fData[j] - fData[j+i];
            qTmp += static_cast<double>(fTmp * fTmp);
        }
        vectCorrel[static_cast<int>(i - dMin)] = static_cast<float>(qTmp / (size - dMax));
    }

    return vectCorrel;
}

float SampleUtils::correlation(const float *fData1, const float* fData2, quint32 size, float *bestValue)
{
    // Mesure ressemblance
    double sum = 0;
    float tmp;
    if (bestValue == nullptr)
    {
        // Just compute the value
        for (quint32 i = 0; i < size; ++i)
        {
            tmp = fData1[i] - fData2[i];
            sum += static_cast<double>(tmp * tmp);
        }
    }
    else
    {
        // If the sum exceeds bestValue, return immediately
        double max = size * static_cast<double>(*bestValue);
        for (quint32 i = 0; i < size; ++i)
        {
            tmp = fData1[i] - fData2[i];
            sum += static_cast<double>(tmp * tmp);
            if (sum > max)
                return *bestValue + 1; // Anything more than bestvalue is ok
        }
    }

    // Normalisation et retour
    return static_cast<float>(sum / size);
}

bool SampleUtils::loopStep1(QByteArray baData32, quint32 dwSmplRate, quint32 &loopStart, quint32 &loopEnd, quint32 &loopCrossfadeLength)
{
    // Conversion en float
    qint32 size = baData32.size() / 4;
    QVector<float> fData;
    fData.resize(size);
    qint32 * iData = reinterpret_cast<qint32*>(baData32.data());
    for (int i = 0; i < size; i++)
        fData[i] = static_cast<float>(iData[i]);

    // Recherche du régime permament
    quint32 posStart = loopStart;
    if (posStart == loopEnd || loopEnd < dwSmplRate / 4 + posStart)
        regimePermanent(fData, dwSmplRate, posStart, loopEnd);
    if (loopEnd < dwSmplRate / 4 + posStart)
        return false;

    // Extraction du segment B de 0.05s à la fin du régime permanent
    quint32 longueurSegmentB = static_cast<quint32>(0.05 * dwSmplRate);
    QVector<float> segmentB = fData.mid(static_cast<int>(loopEnd - longueurSegmentB), static_cast<int>(longueurSegmentB));

    // Find the best correlation
    float minCorValue;
    quint32 bestCorPos;
    {
        float fTmp;
        quint32 nbCor = (loopEnd - posStart) / 2 - 2 * longueurSegmentB;

        if (nbCor == 0)
            return false;

        const float * pointerSegB = segmentB.constData();
        const float * pointerData = fData.constData();

        minCorValue = correlation(pointerSegB, &pointerData[longueurSegmentB + posStart], longueurSegmentB, nullptr);
        bestCorPos = 0;
        for (quint32 i = 1; i < nbCor; ++i)
        {
            fTmp = correlation(pointerSegB, &pointerData[longueurSegmentB + posStart + i], longueurSegmentB, &minCorValue);
            if (fTmp < minCorValue)
            {
                minCorValue = fTmp;
                bestCorPos = i;
            }
        }
    }

    // Update loop start
    loopStart = 2 * longueurSegmentB + bestCorPos + posStart;

    // Longueur du crossfade pour bouclage (augmente avec l'incohérence)
    loopCrossfadeLength = qMin(bestCorPos + 2 * longueurSegmentB,
                               static_cast<quint32>((2147483647.0f - minCorValue) * dwSmplRate * 4 / 2147483647 + 0.5f));

    return true;
}

QByteArray SampleUtils::loopStep2(QByteArray baData32, quint32 loopStart, quint32 loopEnd, quint32 loopCrossfadeLength)
{
    // Loop with a crossfade
    qint32 * iData = reinterpret_cast<qint32*>(baData32.data());
    for (quint32 i = 0; i < loopCrossfadeLength; i++)
    {
        float dTmp = static_cast<float>(i) / static_cast<float>(loopCrossfadeLength - 1);
        iData[loopEnd - loopCrossfadeLength + i] = static_cast<qint32>(
                    (1.f - dTmp) * static_cast<float>(iData[loopEnd - loopCrossfadeLength + i]) +
                dTmp * static_cast<float>(iData[loopStart - loopCrossfadeLength + i]));
    }

    // Get 8 more points
    QByteArray baTmp;
    baTmp.resize(4 * 8);
    qint32 * dataTmp = reinterpret_cast<qint32 *>(baTmp.data());
    for (quint32 i = 0; i < 8; i++)
        dataTmp[i] = iData[static_cast<int>(loopStart + i)];

    // Coupure et ajout de 8 valeurs
    return baData32.left(static_cast<int>(loopEnd * 4)).append(baTmp);
}

QList<quint32> SampleUtils::findMins(QVector<float> vectData, int maxNb, float minFrac)
{
    if (vectData.isEmpty())
        return QList<quint32>();

    // Calcul mini maxi
    float mini = vectData[0], maxi = vectData[0];
    for (qint32 i = 1; i < vectData.size(); i++)
    {
        if (vectData[i] < mini)
            mini = vectData[i];
        if (vectData[i] > maxi)
            maxi = vectData[i];
    }

    // Valeur à ne pas dépasser
    float valMax = maxi - minFrac * (maxi - mini);

    // Recherche des indices de tous les creux
    QMap<quint32, float> mapCreux;
    for (int i = 1; i < vectData.size() - 1; i++)
        if (vectData[i-1] > vectData[i] && vectData[i+1] > vectData[i] && vectData[i] < valMax)
            mapCreux[static_cast<quint32>(i)] = vectData[i];

    // Sélection des plus petits creux
    QList<float> listCreux = mapCreux.values();
    std::sort(listCreux.begin(), listCreux.end());
    QList<quint32> listRet;
    for (int i = 0; i < qMin(maxNb, listCreux.size()); i++)
        listRet << mapCreux.key(listCreux.at(i));

    return listRet;
}

QList<quint32> SampleUtils::findMax(QVector<float> vectData, int maxNb, float minFrac)
{
    if (vectData.isEmpty())
        return QList<quint32>();

    // Calcul mini maxi
    float mini = vectData[0], maxi = vectData[0];
    for (qint32 i = 1; i < vectData.size(); i++)
    {
        if (vectData[i] < mini)
            mini = vectData[i];
        if (vectData[i] > maxi)
            maxi = vectData[i];
    }

    // Valeur à dépasser
    float valMin = mini + minFrac * (maxi - mini);

    // Recherche des indices de tous les pics
    QMap<int, float> mapPics;
    for (int i = 1; i < vectData.size() - 1; i++)
        if (vectData[i-1] < vectData[i] && vectData[i+1] < vectData[i] && vectData[i] > valMin)
            mapPics[i] = vectData[i];

    // Sélection des plus grands pics
    QList<float> listPics = mapPics.values();
    std::sort(listPics.begin(), listPics.end());
    QList<quint32> listRet;
    for (int i = listPics.size() - 1; i >= qMax(0, listPics.size() - maxNb); i--)
        listRet << static_cast<quint32>(mapPics.key(listPics.at(i)));

    return listRet;
}

qint32 SampleUtils::max(QByteArray baData, quint16 wBps)
{
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    qint32 * data = reinterpret_cast<qint32 *>(baData.data());
    qint32 maxi = data[0];
    for (int i = 1; i < baData.size()/4; i++)
    {
        if (data[i] > maxi)
            maxi = data[i];
    }
    return maxi;
}

// Reconnaissance de liens R - L dans les noms de samples
int SampleUtils::lastLettersToRemove(QString str1, QString str2)
{
    str1 = str1.toLower();
    str2 = str2.toLower();
    int nbLetters = 0;

    int size = 0;
    if (str1.size() == str2.size())
        size = str1.size();
    else return 0;

    if (str1.left(size - 2).compare(str2.left(size - 2)) != 0)
        return 0;

    QString fin1_3 = str1.right(3);
    QString fin2_3 = str2.right(3);
    QString fin1_2 = str1.right(2).left(1);
    QString fin2_2 = str2.right(2).left(1);
    QString fin1_1 = str1.right(1);
    QString fin2_1 = str2.right(1);

    if ((fin1_3.compare("(r)") == 0 && fin2_3.compare("(l)") == 0) ||
            (fin1_3.compare("(l)") == 0 && fin2_3.compare("(r)") == 0))
        nbLetters = 3;
    else if (((fin1_1.compare("r") == 0 && fin2_1.compare("l") == 0) ||
              (fin1_1.compare("l") == 0 && fin2_1.compare("r") == 0)) &&
             str1.left(size - 1).compare(str2.left(size - 1)) == 0)
    {
        nbLetters = 1;
        if ((fin1_2.compare("-") == 0 && fin2_2.compare("-") == 0) ||
                (fin1_2.compare("_") == 0 && fin2_2.compare("_") == 0) ||
                (fin1_2.compare(".") == 0 && fin2_2.compare(".") == 0) ||
                (fin1_2.compare(" ") == 0 && fin2_2.compare(" ") == 0))
            nbLetters = 2;
    }

    return nbLetters;
}


// UTILITAIRES, PARTIE PRIVEE

void SampleUtils::FFT_calculate(Complex * x, quint32 N /* must be a power of 2 */,
                                Complex * X, Complex * scratch, Complex * twiddles)
{
    quint32 k, m, n, skip;
    bool evenIteration = N & 0x55555555;
    Complex* E;
    Complex* Xp, * Xp2, * Xstart;
    if (N == 1)
    {
        X[0] = x[0];
        return;
    }
    E = x;
    for (n = 1; n < N; n *= 2)
    {
        Xstart = evenIteration? scratch : X;
        skip = N / (2 * n);
        /* each of D and E is of length n, and each element of each D and E is
        separated by 2*skip. The Es begin at E[0] to E[skip - 1] and the Ds
        begin at E[skip] to E[2*skip - 1] */
        Xp = Xstart;
        Xp2 = Xstart + N / 2;
        for (k = 0; k != n; k++)
        {
            double tim = twiddles[k * skip].imag();
            double tre = twiddles[k * skip].real();
            for (m = 0; m != skip; ++m)
            {
                Complex* D = E + skip;
                /* twiddle *D to get dre and dim */
                double dre = D->real() * tre - D->imag() * tim;
                double dim = D->real() * tim + D->imag() * tre;
                Xp->real(E->real() + dre);
                Xp->imag(E->imag() + dim);
                Xp2->real(E->real() - dre);
                Xp2->imag(E->imag() - dim);
                ++Xp;
                ++Xp2;
                ++E;
            }
            E += skip;
        }
        E = Xstart;
        evenIteration = !evenIteration;
    }
}

double SampleUtils::moyenne(QByteArray baData, quint16 wBps)
{
    if (baData.size())
        return somme(baData, wBps) / (baData.size() / (wBps/8));
    else
        return 0;
}

double SampleUtils::moyenneCarre(QByteArray baData, quint16 wBps)
{
    //return sommeAbs(baData, wBps) / (baData.size() / (wBps/8));
    return qSqrt(sommeCarre(baData, wBps)) / (baData.size() / (wBps/8));
}

float SampleUtils::mediane(QVector<float> data)
{
    float * arr = data.data();
    qint32 n = data.size();
    qint32 low, high;
    qint32 median;
    qint32 middle, ll, hh;
    float qTmp;
    low = 0 ; high = n-1 ; median = (low + high) / 2;
    for (;;)
    {
        if (high <= low) // One element only
            return arr[median] ;

        if (high == low + 1)
        {  // Two elements only
            if (arr[low] > arr[high])
            {
                qTmp = arr[low];
                arr[low] = arr[high];
                arr[high] = qTmp;
            }
            return arr[median] ;
        }
        // Find median of low, middle and high items; swap into position low
        middle = (low + high) / 2;
        if (arr[middle] > arr[high])
        {
            qTmp = arr[middle];
            arr[middle] = arr[high];
            arr[high] = qTmp;
        }
        if (arr[low] > arr[high])
        {
            qTmp = arr[low];
            arr[low] = arr[high];
            arr[high] = qTmp;
        }
        if (arr[middle] > arr[low])
        {
            qTmp = arr[middle];
            arr[middle] = arr[low];
            arr[low] = qTmp;
        }
        // Swap low item (now in position middle) into position (low+1)
        qTmp = arr[middle];
        arr[middle] = arr[low+1];
        arr[low+1] = qTmp;
        // Nibble from each end towards middle, swapping items when stuck
        ll = low + 1;
        hh = high;
        for (;;)
        {
            do ll++; while (arr[low] > arr[ll]) ;
            do hh--; while (arr[hh]  > arr[low]) ;
            if (hh < ll)
                break;
            qTmp = arr[ll];
            arr[ll] = arr[hh];
            arr[hh] = qTmp;
        }
        // Swap middle item (in position low) back into correct position
        qTmp = arr[low];
        arr[low] = arr[hh];
        arr[hh] = qTmp;
        // Re-set active partition
        if (hh <= median)
            low = ll;
        if (hh >= median)
            high = hh - 1;
    }
}

qint64 SampleUtils::somme(QByteArray baData, quint16 wBps)
{
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    qint32 n = baData.size() / 4;
    qint32 * arr = reinterpret_cast<qint32 *>(baData.data());
    qint64 valeur = 0;
    for (int i = 0; i < n; i++)
        valeur += arr[i];
    return valeur;
}

qint64 SampleUtils::sommeCarre(QByteArray baData, quint16 wBps)
{
    if (wBps != 32)
        baData = bpsConversion(baData, wBps, 32);
    qint32 n = baData.size() / 4;
    qint32 * arr = reinterpret_cast<qint32 *>(baData.data());
    qint64 valeur = 0;
    for (int i = 0; i < n; i++)
        valeur += (arr[i] / 47000) * (arr[i] / 47000);
    return valeur;
}

double SampleUtils::gainEQ(double freq, int i1, int i2, int i3, int i4, int i5, int i6, int i7, int i8, int i9, int i10)
{
    int x1 = 0;
    int x2 = 1;
    int y1 = 0;
    int y2 = 1;
    if (freq < 32)
    {
        x1 = 32; x2 = 64;
        y1 = qMin(i1, i2); y2 = i2;
    }
    else if (freq < 64)
    {
        x1 = 32; x2 = 64;
        y1 = i1; y2 = i2;
    }
    else if (freq < 125)
    {
        x1 = 64; x2 = 125;
        y1 = i2; y2 = i3;
    }
    else if (freq < 250)
    {
        x1 = 125; x2 = 250;
        y1 = i3; y2 = i4;
    }
    else if (freq < 500)
    {
        x1 = 250; x2 = 500;
        y1 = i4; y2 = i5;
    }
    else if (freq < 1000)
    {
        x1 = 500; x2 = 1000;
        y1 = i5; y2 = i6;
    }
    else if (freq < 2000)
    {
        x1 = 1000; x2 = 2000;
        y1 = i6; y2 = i7;
    }
    else if (freq < 4000)
    {
        x1 = 2000; x2 = 4000;
        y1 = i7; y2 = i8;
    }
    else if (freq < 8000)
    {
        x1 = 4000; x2 = 8000;
        y1 = i8; y2 = i9;
    }
    else if (freq < 16000)
    {
        x1 = 8000; x2 = 16000;
        y1 = i9; y2 = i10;
    }
    else
    {
        x1 = 8000; x2 = 16000;
        y1 = i9; y2 = qMin(i9, i10);
    }
    double a = static_cast<double>(y1 - y2) / (x1 - x2);
    double b = static_cast<double>(y2) - a * x2;

    // Gain en dB
    double val = a * freq + b;

    // Conversion
    return pow(10.0, 0.1 * val);
}

void SampleUtils::regimePermanent(QVector<float> data, quint32 dwSmplRate, quint32 &posStart, quint32 &posEnd, quint32 nbOK, float coef)
{
    // Calcul de la moyenne des valeurs absolues sur une période de 0.05 s à chaque 10ième de seconde
    quint32 sizePeriode = dwSmplRate / 10;
    quint32 len = static_cast<quint32>(data.size());
    if (len < sizePeriode)
    {
        // Take the full length of the sample
        posStart = 0;
        posEnd = (len == 0 ? 0 : len - 1);
        return;
    }
    quint32 nbValeurs = (len - sizePeriode) / (dwSmplRate / 20);
    if (nbValeurs == 0)
    {
        // Take also the full length of the sample
        posStart = 0;
        posEnd = (len == 0 ? 0 : len - 1);
        return;
    }
    QVector<float> tableauMoyennes;
    tableauMoyennes.resize(static_cast<int>(nbValeurs));
    for (quint32 i = 0; i < nbValeurs; i++)
    {
        float valTmp = 0;
        for (quint32 j = 0; j < sizePeriode; j++)
            valTmp += qAbs(data[static_cast<int>((dwSmplRate / 20) * i + j)]);
        data[static_cast<int>(i)] = valTmp / sizePeriode;
    }

    // Calcul de la médiane des valeurs
    float median = mediane(tableauMoyennes);
    posStart = 0;
    posEnd = nbValeurs - 1;
    quint32 count = 0;
    while (count < nbOK && posStart <= posEnd)
    {
        if (data[static_cast<int>(posStart)] < coef * median && data[static_cast<int>(posStart)] > median / coef)
            count++;
        else
            count = 0;
        posStart++;
    }
    posStart = posStart + 2 - count;
    count = 0;
    while (count < nbOK && posEnd > 0)
    {
        if (data[static_cast<int>(posEnd)] < coef * median && data[static_cast<int>(posEnd)] > median / coef)
            count++;
        else
            count = 0;
        posEnd--;
    }
    posEnd += count-2;

    // Conversion position
    posStart *= dwSmplRate / 20;
    posEnd *= dwSmplRate / 20;
    posEnd += sizePeriode;
}

double SampleUtils::sinc(double x)
{
    double epsilon0 = 0.32927225399135962333569506281281311031656150598474e-9L;
    double epsilon2 = qSqrt(epsilon0);
    double epsilon4 = qSqrt(epsilon2);

    if (qAbs(x) >= epsilon4)
        return(qSin(x)/x);
    else
    {
        // x très proche de 0, développement limité ordre 0
        double result = 1;
        if (qAbs(x) >= epsilon0)
        {
            double x2 = x*x;

            // x un peu plus loin de 0, développement limité ordre 2
            result -= x2 / 6.;

            if (qAbs(x) >= epsilon2)
            {
                // x encore plus loin de 0, développement limité ordre 4
                result += (x2 * x2) / 120.;
            }
        }
        return(result);
    }
}

// Keser-Bessel window
void SampleUtils::KBDWindow(double* window, int size, double alpha)
{
    double sumvalue = 0.;
    int i;

    for (i = 0; i < size/2; i++)
    {
        sumvalue += BesselI0(M_PI * alpha * sqrt(1. - pow(4.0*i/size - 1., 2)));
        window[i] = sumvalue;
    }

    // need to add one more value to the nomalization factor at size/2:
    sumvalue += BesselI0(M_PI * alpha * sqrt(1. - pow(4.0*(size/2) / size-1., 2)));

    // normalize the window and fill in the righthand side of the window:
    for (i = 0; i < size/2; i++)
    {
        window[i] = sqrt(window[i]/sumvalue);
        window[size-1-i] = window[i];
    }
}

double SampleUtils::BesselI0(double x)
{
    double denominator;
    double numerator;
    double z;

    if (x == 0.0)
        return 1.0;
    else
    {
        z = x * x;
        numerator = (z* (z* (z* (z* (z* (z* (z* (z* (z* (z* (z* (z* (z*
                                                                     (z* 0.210580722890567e-22  + 0.380715242345326e-19 ) +
                                                                     0.479440257548300e-16) + 0.435125971262668e-13 ) +
                                                             0.300931127112960e-10) + 0.160224679395361e-7  ) +
                                                     0.654858370096785e-5)  + 0.202591084143397e-2  ) +
                                             0.463076284721000e0)   + 0.754337328948189e2   ) +
                                     0.830792541809429e4)   + 0.571661130563785e6   ) +
                             0.216415572361227e8)   + 0.356644482244025e9   ) +
                     0.144048298227235e10);
        denominator = (z*(z*(z-0.307646912682801e4)+
                          0.347626332405882e7)-0.144048298227235e10);
    }

    return -numerator/denominator;
}

float SampleUtils::computeLoopQuality(QByteArray baData, quint32 loopStart, quint32 loopEnd)
{
    qint16 * data = reinterpret_cast<qint16*>(baData.data());
    quint32 length = baData.size() / 2;
    if (loopStart < 2 || loopStart >= loopEnd || loopEnd >= length)
        return 0;

    // Compute the difference at the loop points and a bit before
    int n = 1;
    float result = getDiffForLoopQuality(data, loopStart, loopEnd);

    if (loopStart > 7)
    {
        result += getDiffForLoopQuality(data, loopStart - 5, loopEnd - 5);
        n++;
    }

    if (loopStart > 11)
    {
        result += getDiffForLoopQuality(data, loopStart - 9, loopEnd - 9);
        n++;
    }

    // Maximum value inside the loop
    float max = 15;
    float tmp;
    for (quint32 i = loopStart; i <= loopEnd; i++)
    {
       tmp = static_cast<float>(data[i]);
       if (tmp > max)
           max = tmp;
       else if (-tmp > max)
           max = -tmp;
    }

    // Differences are relative to the maximum
    return result / (max * n);
}

float SampleUtils::getDiffForLoopQuality(qint16 * data, quint32 pos1, quint32 pos2)
{
    // Difference on the loop points
    float diff0 = static_cast<float>(data[pos1] - data[pos2]);

    // Difference on the slope
    float diff1 = static_cast<float>(data[pos1 - 1] - data[pos2 - 1]) - diff0;

    // Difference on the slope difference
    float diff2 = static_cast<float>(data[pos1 - 2] - data[pos2 - 2]) - 2 * diff1 + diff0;

    // Combine the values
    return 0.45 * qAbs(diff0) + 0.33 * qAbs(diff1) + 0.22 * qAbs(diff2);
}
