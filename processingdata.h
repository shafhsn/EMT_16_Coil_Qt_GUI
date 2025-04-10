#ifndef PROCESSINGDATA_H
#define PROCESSINGDATA_H

#include <QObject>
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include "sharedbuffer.h"

/**
 * @brief The ProcessingData class
 *
 * This calss handles the parsin and processing of raw UDP datagrams received from instrument,
 * performs tasks such as segmenting data into little chunks, tokenisation, data type conversion,
 * decimation, and more.
 * Processed data then passed to dataConsumerThread
 */

class ProcessingData : public QObject
{
    Q_OBJECT
public:
    explicit ProcessingData(SharedBuffer *sharedBuffer, QObject *parent = nullptr);

public slots:
    void processDatagrams(const QList<QByteArray> &datagrams);      //processes the incoming UDP data

signals:
    void processedDataReady(const QString &result);                 //notifies other threads that an UDP packet has been parsed fully
    void booleanOTRUpdated(const QString &status);                  //to update 'Over Range?' display on GUI
    void numberADCUpdated(const QString &modeADC);                  //to update 'ADC level' display on GUI
    void samplesPacketUpdated(const int &samplesPerPacket);         //to update 'Samples/Packet display on GUI
    void rawDataUpdated(const QString &rawDatastr);                 //to update 'Raw Data' display on GUI

private:
    SharedBuffer *m_sharedBuffer;                                   //pointer to shared container between two threads
};

#endif // PROCESSINGDATA_H
