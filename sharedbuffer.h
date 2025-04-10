#ifndef SHAREDBUFFER_H
#define SHAREDBUFFER_H

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>

/**
 * @brief The SharedBuffer class
 *
 * This class is used to share data between two threads, safely
 * The threds in question are processingDataThread and dataConsumerThread
 * It does so by making use of multiple QQueue instances
 * Access to queue synchronised using a QMutex and QWaitCondition
 */

class SharedBuffer
{
public:
    SharedBuffer();

    //shared buffers
    QQueue<qint64> bufferFinalFrequency;            //stores actual frequency values
    QQueue<qint32> bufferDecimated1;                //stores sensing coil data
    QQueue<qint32> bufferDecimated2;                //stores excitation coil data
    QQueue<double> bufferFourthArrayDivided;        //stores real data
    QQueue<double> bufferSixthArrayDivided;         //stores imaginary data

    //for thread-safe communication
    QMutex mutex;                                   //provides exclusive access to data by one thread
    QWaitCondition dataAvailable;                   //used to put threads to sleep and wake them when needed
};

#endif // SHAREDBUFFER_H
