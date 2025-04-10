#ifndef DATACONSUMER_H
#define DATACONSUMER_H

#include <QObject>
#include <QVector>
#include "sharedbuffer.h"
#include <QAtomicInteger>

/**
 * @brief The DataConsumer class
 *
 * This class represents the consumer side in a producer-consumer pattern.
 * It is responsible for retrieving formatted data from a shared buffer,
 * populated by processingDataThread, to then perform some processing (decimating,
 * rotating, computing coil combination states, and formatting data into
 * final format for saving). Then it passes back data to the MainWindow thread
 * for saving purposes.
 */

class DataConsumer : public QObject
{
    Q_OBJECT
public:
    explicit DataConsumer(SharedBuffer *sharedBuffer, QObject *parent = nullptr);

    void stop();                                //sets m_stop flag to true, to terminate this thread
    QAtomicInteger<bool> m_syncEnabled{false};  //retrieves autoSync flag from main thread

public slots:
    void processBuffers();                      //main slot of this class

signals:

    void processedChunkResult(const QVector<QVector<double>> &global2DArray);   //emits final data for saving
    void autoSyncUpdated(const int &autoSyncUpdatedValue);                      //emits 'Auto Sync' value for UI display
    void actualFrequencyUpdated(const double &actualFrequencyValue);            //emits 'Actual Frequency' value for UI display

private:
    SharedBuffer *m_sharedBuffer;               //pointer to inter-thread shared buffer holding processed data from processingDataThread
    const int chunkSize = 480;                  //number of data processed from the processingDataThread
    int autosync = 0;                           //initialises Auto Synch value to zero
    double actualfrequency = 0;                 //initialises Actual Frequency value to zero
    bool m_stop;                                //flag used to run/stop this thread
};

#endif // DATACONSUMER_H
