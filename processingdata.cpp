#include "processingdata.h"
#include <QRegularExpression>
#include <QtMath>
#include <QDebug>
#include <algorithm>

ProcessingData::ProcessingData(SharedBuffer *sharedBuffer, QObject *parent)
    : QObject{parent}
    , m_sharedBuffer(sharedBuffer)
{
}

void ProcessingData::processDatagrams(const QList<QByteArray> &datagrams)
{
    // Local variables for processing (thread-local, so thread safe)
    QStringList formattedChunks;
    QStringList ADCListStr;
    QStringList OTRListStr;
    QList<qint32> convertedIntegers;
    QList<qint32> finalFrequency;

    // For each datagram, process in 32-character segments.
    for (const QByteArray &buffer : datagrams) {
        QString binaryString = QString(buffer);
        for (int i = 0; i + 32 <= binaryString.length(); i += 32) {
            QString chunk = binaryString.mid(i, 32);
            QString FrequencyRas = chunk.mid(0, 4);
            QString SCoil = chunk.mid(4, 1);
            QString ECoil = chunk.mid(5, 1);
            QString ADC = chunk.mid(6, 1);
            ADCListStr.append(ADC);
            QString OTR = chunk.mid(7, 1);
            OTRListStr.append(OTR);
            QString IData = chunk.mid(8, 8);
            QString FrequencyStand = chunk.mid(16, 8);
            QString QData = chunk.mid(24, 8);
            QString formattedChunk = QString("%1,%2,%3,%4;%5,%6;")
                                         .arg(FrequencyRas)
                                         .arg(SCoil)
                                         .arg(ECoil)
                                         .arg(IData)
                                         .arg(FrequencyStand)
                                         .arg(QData);
            formattedChunks.append(formattedChunk);
        }
    }

    quint32 sumOTR = 0;
    for (const QString &otrStr : OTRListStr){
        bool sumOTRok = false;
        quint32 value = otrStr.toUInt(&sumOTRok, 16);
        if(!sumOTRok){
            return;
        }
        sumOTR += value;
    }
    if (sumOTR > 0)
        emit booleanOTRUpdated("YES");
    else
        emit booleanOTRUpdated("NO");

    QHash<QString, int> adcCount;
    for (const QString &adcStr : ADCListStr){
        bool ADCok = false;
        quint32 value = adcStr.toUInt(&ADCok, 16);
        if(!ADCok){
            return;
        }
        adcCount[adcStr] += 1;
    }

    quint32 modeValue = 0;
    int maxCount = 0;
    int modeCandidates = 0;
    for(auto it = adcCount.constBegin() ; it !=adcCount.constEnd(); ++it){
        if(it.value()>maxCount){
            maxCount = it.value();
            modeValue = it.key().toUInt(nullptr,16);
            modeCandidates = 1;
        } else if (it.value() == maxCount){
            modeCandidates++;
        }
    }

    quint32 finalMode = (maxCount > 1 && modeCandidates == 1) ? modeValue : 0;
    QString mode = QString::number(finalMode);
    emit numberADCUpdated(mode);

    // Continue processing: reverse the overall string and tokenize.
    QString finalOutput = formattedChunks.join("");
    std::reverse(finalOutput.begin(), finalOutput.end());
    emit rawDataUpdated(finalOutput);

    static const QRegularExpression re("[,;]");
    QStringList tokens = finalOutput.split(re, Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        bool ok = false;
        quint32 uValue = token.toUInt(&ok, 16);
        if (ok)
            convertedIntegers.append(static_cast<qint32>(uValue));
        else
            qDebug() << "Error converting token to int:" << token;
    }
    std::reverse(convertedIntegers.begin(), convertedIntegers.end());

    // Decimate the converted integers into 6 arrays.
    QList<qint32> decimated[6];
    for (int i = 0; i < convertedIntegers.size(); ++i) {
        int index = i % 6;
        decimated[index].append(convertedIntegers[i]);
    }

    int secondArraySize = decimated[1].size();
    emit samplesPacketUpdated(secondArraySize);

    // Process finalFrequency from decimated[0]
    for (int i = 0; i < decimated[0].size(); ++i) {
        qint32 newVal = decimated[0].at(i) * 8;
        qint64 newValue = static_cast<qint64>(newVal);
        finalFrequency.append(newValue);
    }

    // Process the fourth decimated array: convert each to double and divide by 2^31.
    QList<double> fourthArrayDivided;
    double divisor = qPow(2.0, 31.0);
    for (int i = 0; i < decimated[3].size(); ++i) {
        double dVal = static_cast<double>(decimated[3].at(i));
        double result = dVal / divisor;
        fourthArrayDivided.append(result);
    }

    // Process the sixth decimated array: convert each to double and divide by 2^31.
    QList<double> sixthArrayDivided;
    for (int i = 0; i < decimated[5].size(); ++i) {
        double dVal = static_cast<double>(decimated[5].at(i));
        double result = dVal / divisor;
        sixthArrayDivided.append(result);
    }

    // For now, stop processing here.
    // Build a result string (for example, list the values in the sixthArrayDivided).
    /*QString result;
    result += "Sixth Array Divided:\n";
    for (int i = 0; i < sixthArrayDivided.size(); ++i) {
        result += QString::number(sixthArrayDivided.at(i)) + " ";
    }
    emit processedDataReady(result);*/

    {
        QMutexLocker locker(&m_sharedBuffer->mutex);
        for (const qint64 &val : qAsConst(finalFrequency)){
            m_sharedBuffer->bufferFinalFrequency.enqueue(val);
        }
        for (const qint32 &val : qAsConst(decimated[1])){
            m_sharedBuffer->bufferDecimated1.enqueue(val);
        }
        for (const qint32 &val : qAsConst(decimated[2])){
            m_sharedBuffer->bufferDecimated2.enqueue(val);
        }
        for (const double &d : qAsConst(fourthArrayDivided)) {
            m_sharedBuffer->bufferFourthArrayDivided.enqueue(d);
        }
        //int idx = 0;
        for (const double &d : qAsConst(sixthArrayDivided)) {
            m_sharedBuffer->bufferSixthArrayDivided.enqueue(d);
            //qDebug() << "queued times: " << ++idx;
        }
    }
    m_sharedBuffer->dataAvailable.wakeAll();
}

