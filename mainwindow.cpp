#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "processingdata.h"
#include "dataconsumer.h"
#include "sharedbuffer.h"

#include <QDebug>
#include <QByteArray>
#include <QHostAddress>
#include <QUdpSocket>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QTimer>
#include <QStringList>
#include <QtMath>
#include <QRegularExpression>
#include <QtGlobal>
#include <algorithm>
#include <QBuffer>
#include <QThread>
#include <QCloseEvent>
#include <QMetaObject>

/**
 * MainWindow.cpp
 * ------------------------------------------
 * Implements MainWindow class
 * Handles UDP communication, data processing, monitors UI updates and controls,
 * takes filepaths and saves data
 * MainThread
 **/

//constructor: initialises UI, UDP sockets, and connects signals
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)                    //allocate UI from Designer
    , udpSocket(new QUdpSocket(this))           //create primary UDP socket
    , udpSocketOut(new QUdpSocket(this))        //create secondary UDP socket
    , messageReceivedFlag(false)                //initiliases message flag to false
    , storedFrequencyConfiguration(0.0)         //default frequency configuration value
{
    ui->setupUi(this);                          //initialises UI
    //ui->buttonSync->setCheckable(true);       //redundant

    //Clear any previous data in processing containers
    formattedChunks.clear();
    convertedIntegers.clear();
    joinedValueArray.clear();
    global2DArray.clear();
    global2DArray.resize(6);

    phaseOffsetArray << 10 << 20 << 30 << 40 << 50;                 //set up phase offset array

    localPort = static_cast<quint16>(ui->inputLocalPort->value());  //retrieve and store local port from UI control

    QTimer *processTimer = new QTimer(this);                        //redundant, may delete

    //bind primary UDP socket for incoming message
    //replace LocalHost below with ("192.168.1.2"), local port should be 4593 by default
    //or use QHostAddress::LocalHost instead for offline testing
    if (udpSocket -> bind(QHostAddress("192.168.1.2"), localPort)){
        ui->outputMessageLog->append("Socket bound successfully to port: " + QString::number(localPort));
    } else {
        ui->outputMessageLog->append("Binding failed to port: " + QString::number(localPort) + "Because: " + udpSocket->errorString());
    }

    connect(udpSocket, &QUdpSocket::readyRead, this, &MainWindow::handleIncomingMessage);           //upate 'message when received
    connect(ui->buttonLog, &QPushButton::clicked, this, &MainWindow::onLogButtonClicked);           //logs message when LOG button clicked
    connect(ui->inputLocalPort, SIGNAL(valueChanged(int)), this, SLOT(updateLocalPort(int)));       //read 'Local Port' control when changed

    //bind secondary UDP to port 4592, used to send EMT settings to port 4590
    //replace LocalHost by "192.168.1.2", port should be 4592
    //or use QHostAddress::LocalHost instead for offline testing
    if (udpSocketOut -> bind(QHostAddress("192.168.1.2"), 4592)){
        ui->outputMessageLog->append("Socket bound successfully to port: 4592");
    } else {
        ui->outputMessageLog->append("Binding failed: " + udpSocketOut->errorString());
    }

    connect(udpSocketOut, &QUdpSocket::readyRead, this, &MainWindow::handleDatagram);                                               //pass data to processingDataThread

    //connect the four buttons that make use of the secondary UDP socket
    connect(ui->buttonSendConfiguration, &QPushButton::clicked, this, &MainWindow::onbuttonSendConfigurationclicked);               //send configuration settings when SEND CONFIGURATION clicked
    connect(ui->buttonSendFrequency, &QPushButton::clicked, this, &MainWindow::onbuttonSendFrequencyclicked);                       //send frequency settings when SEND FREQUENCY CONFIGURATION clicked
    connect(ui->buttonSendSendingSequence, &QPushButton::clicked, this, &MainWindow::onbuttonSendSensingSequenceclicked);           //send sensing sequence when SEND SENSING clicked
    connect(ui->buttonSendExcitationSequence, &QPushButton::clicked, this, &MainWindow::onbuttonSendExcitationSequenceclicked);     //send excitation sequence when SEND EXCITATION clicked

    //update 'System Status' when DEVICE ENABLE changed
    connect(ui->inputDeviceEnable, SIGNAL(activated(int)), this, SLOT(oninputDeviceEnableactivated(int)));
    oninputDeviceEnableactivated(ui->inputDeviceEnable->currentIndex());
    connect(ui->buttonUpdate, SIGNAL(clicked(bool)),this, SLOT(onbuttonUpdateclicked()));                               //update sequence fields when UPDATE clicked
    connect(ui->buttonStopFinalData, &QPushButton::clicked, this, &MainWindow::onbuttonStopFinalDataclicked);           //redundant
    connect(ui->buttonStartFinalData, &QPushButton::clicked, this, &MainWindow::onbuttonStartFinalDataclicked);         //redundant
    connect(ui->buttonClearFinalData, &QPushButton::clicked, this, &MainWindow::onbuttonClearFinalDataclicked);         //redundant
    connect(ui->buttonSave, &QPushButton::clicked, this, &MainWindow::onbuttonSaveclicked);                             //prepares save file when SAVE clickd
    connect(ui->buttonSync, &QPushButton::clicked, this, &MainWindow::onbuttonSyncclicked);                             //reorders data when SYNC clicked

    sharedBuffer = new SharedBuffer();                                                                                  //to pass data between the two worker threads

    processingData = new ProcessingData(sharedBuffer);
    processingDataThread = new QThread(this);
    processingData -> moveToThread(processingDataThread);                                                               //creates processingDataThread
    connect(processingDataThread, &QThread::finished, processingData, &QObject::deleteLater);                           //ensures thread is deleted when terminated

    //various tasks carried out when different signals are emitted from the processingDataThread
    connect(processingData, &ProcessingData::booleanOTRUpdated, this, [this](const QString &status){
        ui->booleanOverRange->setText(status);
    });
    connect(processingData, &ProcessingData::numberADCUpdated, this, [this](const QString &modeADC){
        ui->outputADCLevel->setText(modeADC);
    });
    connect(processingData, &ProcessingData::samplesPacketUpdated, this, [this](const int &samplesPerPacket){
        ui->outputSamplesPackets->display(samplesPerPacket);
    });
    connect(processingData, &ProcessingData::rawDataUpdated, this, [this](const QString &rawDataStr){
        ui->outputRawData->append(rawDataStr);
    });

    processingDataThread->start();                                                                                      //strarts thread

    dataConsumer = new DataConsumer(sharedBuffer);
    dataConsumerThread = new QThread(this);
    dataConsumer->moveToThread(dataConsumerThread);                                                                     //creates dataConsiderThread
    connect(dataConsumerThread, &QThread::finished, dataConsumer, &QObject::deleteLater);                               //ensures thread is deleted when terminated

    QMetaObject::invokeMethod(dataConsumer, "processBuffers", Qt::QueuedConnection);                                    //explicitly starts the function by posting the queued event on other thread

    //various tasks carried out when different signals are emitted from the processingDataThread
    connect(dataConsumer, &DataConsumer::processedChunkResult, this, &MainWindow::onProcessedChunkResult);
    connect(dataConsumer, &DataConsumer::autoSyncUpdated, this, [this](const int &autoSyncValue){
        ui->outputAutoSync->display(autoSyncValue);
    });
    connect(dataConsumer, &DataConsumer::actualFrequencyUpdated, this, [this](const double &actualFrequencyVal){
        ui->outpuActualFrequency->display(actualFrequencyVal);
    });
    dataConsumerThread->start();
}

//Destructor: clean up allocated resources and terminate all threds to prevent crashes and dangling threads
MainWindow::~MainWindow()
{
    if(dataConsumer){
        dataConsumer->stop();
    }
    if(processingDataThread){
        processingDataThread->requestInterruption();
        processingDataThread->quit();
        processingDataThread->wait();
    }
    if (dataConsumerThread) {
        dataConsumerThread->requestInterruption();
        dataConsumerThread->quit();
        dataConsumerThread->wait();
    }
    delete sharedBuffer;
    delete ui;
}

/*
 * handleIncomingMessage()
 * ------------------------------
 * Called when messages arrives on primary UDP socket
 * Reads all pending messages and logs them to UI
 */
void MainWindow::handleIncomingMessage()
{
    messageReceivedFlag = true;

    while(udpSocket->hasPendingDatagrams()){                                                                                                //While there are pending datagrams, process each
        QByteArray buffer;
        buffer.resize(int(udpSocket->pendingDatagramSize()));                                                                               //Resize buffer to size of incoming datagram

        QHostAddress sender;                                                                                                                //Will hold sender's address
        quint16 senderPort;                                                                                                                 //Will hold sender's port

        udpSocket->readDatagram(buffer.data(),buffer.size(),&sender,&senderPort);                                                           //Read datagram into buffer
        ui->outputMessageLog->append("Received from " + sender.toString() + ":" + QString::number(senderPort) + "->" + QString(buffer));    //Log received message
    }
}

/*
 * onLogButtonClicked()
 * ------------------------------
 * Writes content of ouput message log to a file
 */
void MainWindow::onLogButtonClicked()
{
    //Set file path from UI
    QString filePath = ui->inputMessageLogFilePath->toPlainText().trimmed();
    if(filePath.isEmpty()){
        qDebug() << "Error: Log file path is empty.";
        return;
    }

    //Check if directory exists
    QFileInfo fileInfo(filePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()){
        qDebug() << "Error: Directory does not exists.";
        return;
    }

    //Retrieve current log message
    QString messageContent = ui->outputMessageLog->toPlainText();
    if(messageContent.isEmpty()){
        qDebug() << "Error: No message to log.";
        return;
    }

    //Open tile (create if necessary) and append log message
    QFile file(filePath);
    if(!file.exists()){
        if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
            qDebug() << "Error: Could not create file.";
            return;
        }
        file.close();   //Close file once created
    }
    if(!file.open(QIODevice::Append | QIODevice::Text)){
        qDebug() << "Error: Could not open file.";
            return;
    }

    QTextStream out(&file);
    out << messageContent << "\n";              //Write message followed by a newline
    file.close();                               //Close file after writing
    qDebug() << "Message logged successfully to:" << filePath;
}

/*
 * updateLocalPort()
 * ------------------------------
 * Called when user changes local port via UI
 * Update local port and rebinds primary UDP socket
 */
void MainWindow::updateLocalPort(int newPort)
{
    //Update local port value
    localPort = static_cast<quint16>(newPort);

    //Rebind primary UDP socket to new port
    udpSocket->close();                                                     //Close current binding
    //Replace LocalHost below with ("192.168.1.2"),
    if (udpSocket -> bind(QHostAddress("192.168.1.2"), localPort)){
        ui->outputMessageLog->append("Socket bound successfully! to port: " + QString::number(localPort));
    } else {
        ui->outputMessageLog->append("Binding failed: " + udpSocket->errorString());
    }
}

/*
 * onTimeout()
 * ------------------------------
 * Called when no UDP message is received within specified timeout
 * Redundant
 */
/*
void MainWindow::onTimeout()
{
    if(!messageReceivedFlag){
        qDebug() << "Timeout: No message received from instruement within 20 seconds";
    }
}
*/

/*
 * onbuttonSendConfigurationclicked()
 * ----------------------------------
 * Constructs configuration command string from UI input and sends it via UDP
 */
void MainWindow::onbuttonSendConfigurationclicked()
{
    //Configuration format: D1CXXG3H3PXIXXSXJXXX
    quint8 indexOne = 1;
    int filterWidth = ui->inputFilterWidth->value();                                //read from 'Filer Width' control
    filterWidth = qBound(4, filterWidth, 8192);                                     //filterWidth value must be between 4 and 8192
    QString filterWidthStr = QString::number(filterWidth);
    quint8 indexSix = 3;
    quint8 indexEight = 3;
    int samplesPerPeriod = ui->inputSamplePeriod->value();                          //read from 'Samples/Period' control
    samplesPerPeriod = qBound(0,samplesPerPeriod,255);                              //samplesPerPeriod value must be between 0 and 255
    QString samplesPerPeriodStr = QString::number(samplesPerPeriod);
    int iaGain = ui->inputIAGain->value();                                          //read from 'IA Gain' control
    iaGain = qBound(1, iaGain, 65535);                                              //iaGain value must be between 1 and 65535
    QString iaGainStr = QString::number(iaGain);
    int clockOutputValue = ui->buttonClockOutputTestSignal->isChecked() ? 1 : 0;    //read from 'Clock Output Test Signal' control
    int frequencyPeriod = ui->inputFrequencyPeriods->value();                       //read from 'Frequenc Period' control
    frequencyPeriod = qBound(0, frequencyPeriod, 255);
    QString frequencyPeriodStr = QString::number(frequencyPeriod);

    //Build configuration settings string
    QString configurationDataStr = QString("D%1C%2G%3H%4P%5I%6S%7J%8")
                                    .arg(QString::number(indexOne),
                                    filterWidthStr,
                                    QString::number(indexSix),
                                    QString::number(indexEight),
                                    samplesPerPeriodStr,
                                    iaGainStr,
                                    QString::number(clockOutputValue),
                                    frequencyPeriodStr);
    QByteArray data = configurationDataStr.toUtf8();                                //convert to required UDP type

    //replace IP by ("192.168.1.10")
    //send configuration command via UDP
    qint64 bytesSent = udpSocketOut->writeDatagram(data, QHostAddress("192.168.1.10"), 4590);
    if(bytesSent == -1){
        qDebug() << "Failed to send config data to port 4590:" << udpSocketOut->errorString();
    }
}

/*
 * onbuttonSendSensingSequenceclicked()
 * ----------------------------------
 * Sends sensing sequence command from UI via UDP
 */
void MainWindow::onbuttonSendSensingSequenceclicked()
{
    QString sequence = ui->inputSensingSequence->toPlainText();
    QByteArray data = sequence.toUtf8();
    //Replace IP by ("192.168.1.10")
    //Send sensing sequence data via UDP
    qint64 bytesSent = udpSocketOut->writeDatagram(data, QHostAddress("192.168.1.10"), 4590);
    if(bytesSent == -1){
        qDebug() << "Failed to send sensing data to port 4590:" << udpSocketOut->errorString();
    }

}

/*
 * onbuttonSendExcitationSequenceclicked()
 * ----------------------------------
 * Sends excitation sequence command from UI via UDP
 */
void MainWindow::onbuttonSendExcitationSequenceclicked()
{
    QString sequence = ui->inputExcitationSequence->toPlainText();
    QByteArray data = sequence.toUtf8();
    //Replace IP by ("192.168.1.10")
    //Send excitation sequence data via UDP
    qint64 bytesSent = udpSocketOut->writeDatagram(data, QHostAddress("192.168.1.10"), 4590);
    if(bytesSent == -1){
        qDebug() << "Failed to send excitation data to port 4590:" << udpSocketOut->errorString();
    }
}

/*
 * onbuttonSendFrequencyclicked()
 * ----------------------------------
 * Processes frequency configuration string, converts and join values
 * and sends final frequency command via UDP
 */
void MainWindow::onbuttonSendFrequencyclicked()
{
    //Read and split frequency configuration string
    QString frequencyConfigurationStr = ui->inputFrequencyConfiguration->toPlainText().trimmed();
    QStringList individualFrequenciesList = frequencyConfigurationStr.split(",", Qt::SkipEmptyParts);
    frequencyArray.clear();

    //Convert each frequency to a double and store in frequencyArray
    for (const QString &individualFrequency : individualFrequenciesList){
        bool ok;
        double frequencyValue = individualFrequency.trimmed().toDouble(&ok);
        if (ok)
            frequencyArray.append(frequencyValue);
        else
            qDebug() <<"Conversion failed for frequency: " + individualFrequency;
    }

    //Log input frequencies
    QStringList frequencyStrList;
    for (double d : frequencyArray)
        frequencyStrList << QString::number(d);
    //ui->outputMessageLog->append("Frequency Array: " + frequencyStrList.join(","));

    //Process each frequency: divide by 8, round, and convert to quint16
    QVector<quint16>frequencyArrayIntlo;
    for (double freq : frequencyArray){
        frequencyArrayIntlo.append(static_cast<quint16>(qRound(freq/8.0)));
    }

    //Log lo values
    QStringList loList;
    for (quint16 v : frequencyArrayIntlo)
        loList << QString::number(v);
    //ui->outputMessageLog->append("Processed Frequencies (lo values): " + loList.join(","));

    //Create joinedValueArray by joining each processed frequecy with its corresponding hi value
    //hi taken from phaseOffsetArray; if there are more frequencies than hi values,
    //only join as many as available in phaseOffsetArray
    joinedValueArray.clear();
    int joinCount = qMin(frequencyArrayIntlo.size(),phaseOffsetArray.size());
    for (int i = 0; i < joinCount; ++i){
        quint32 joined = (static_cast<quint16>(phaseOffsetArray.at(i)) << 16) | frequencyArrayIntlo.at(i);
        joinedValueArray.append(joined);
    }

    //Log joined values
    QStringList joinedList;
    for (quint32 j : joinedValueArray)
        joinedList << QString::number(j);
    //ui->outputMessageLog->append("Joined Frequency Array: " + joinedList.join(", "));

    //Build final message string in format "F 0 0 A B C D E"
    //Where A-E are the first five values from joinedValueArray, or "0" if missing
    QStringList finalValues;
    for (int i = 0; i < 5; ++i){
        finalValues << (i < joinedValueArray.size() ? QString::number(joinedValueArray.at(i)) : "0");
    }
    QString finalFrequencyMessage = "F 0 0 " + finalValues.join(" ");
    //ui->outputMessageLog->append("Final Message: " + finalFrequencyMessage);

    QByteArray data = finalFrequencyMessage.toUtf8();
    //Replace IP by ("192.168.1.10")
    //Send frequency config data via UDP
    qint64 bytesSent = udpSocketOut->writeDatagram(data, QHostAddress("192.168.1.10"), 4590);
    if(bytesSent == -1){
        qDebug() << "Failed to send frequency config data to port 4590:" << udpSocketOut->errorString();
    }
}

/*
 * oninputDeviceEnableactivated()
 * ----------------------------------
 * Updates device status in UI based on selection
 */
void MainWindow::oninputDeviceEnableactivated(int index)
{
    QString status = (index == 0) ? "ON" : "OFF";
    ui->statusSystemStatus->setText(status);
}

/*
 * onbuttonUpdateclicked()
 * ----------------------------------
 * Updates default sensing and excitation sequence fileds based on coil selection
 */
void MainWindow::onbuttonUpdateclicked()
{
    //Get current text from input8/16Coils combo box
    QString coilSelection = ui->input816Coils->currentText();
    if (coilSelection == "16") {
        ui->inputSensingSequence->setPlainText("S,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,3,4,5,6,7,8,9,10,11,12,13,14,15,16,4,5,6,7,8,9,10,11,12,13,14,15,16,5,6,7,8,9,10,11,12,13,14,15,16,6,7,8,9,10,11,12,13,14,15,16,7,8,9,10,11,12,13,14,15,16,8,9,10,11,12,13,14,15,16,9,10,11,12,13,14,15,16,10,11,12,13,14,15,16,11,12,13,14,15,16,12,13,14,15,16,13,14,15,16,14,15,16,15,16,16.");
        ui->inputExcitationSequence->setPlainText("E,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,10,10,10,10,10,10,11,11,11,11,11,12,12,12,12,13,13,13,14,14,15.");
    } else if (coilSelection == "8") {
        ui->inputSensingSequence->setPlainText("S,3,5,7,9,11,13,15,5,7,9,11,13,15,7,9,11,13,15,9,11,13,15,11,13,15,13,15,15.");
        ui->inputExcitationSequence->setPlainText("E,1,1,1,1,1,1,1,3,3,3,3,3,3,5,5,5,5,5,7,7,7,7,9,9,9,11,11,13.");
    }
}

/*
 * handleDatagram()
 * ----------------------------------
 * This function processes incoming datagrams from secondary UDP socket
 * This is essentially the data from the instruemtn waiting to be decoded and processed
 * Currently does the following:
 *      Reads datagrams and splits them into fixed-size chunks
 *      Exctracts individual fields from each chunk
 *      Builds formatted string from fields and accumulates them
 *      Computes OTR and ADC values, decimates data into 6 arrays, and proccesses frequency and other data
 *      Updates various UI elements
 */
void MainWindow::handleDatagram()
{
    QList<QByteArray> datagramList;
    while(udpSocketOut->hasPendingDatagrams()){
        qint64 pendingSize = udpSocketOut->pendingDatagramSize();
        int readSize = (pendingSize > 8192) ? 8192 : pendingSize;
        QByteArray buffer;
        buffer.resize(readSize);
        QHostAddress sender;        //Stores sender's address
        quint16 senderPort;         //Stores sender's port
        udpSocketOut->readDatagram(buffer.data(),buffer.size(),&sender,&senderPort);
        datagramList.append(buffer);

        QMetaObject::invokeMethod(processingData, "processDatagrams", Qt::QueuedConnection,Q_ARG(QList<QByteArray>, datagramList));    //Clear all processing containers
    }
}

    /*formattedChunks.clear();
    convertedIntegers.clear();

    //Temporary containers for ADC and OTR string values
    QStringList ADCListStr;
    QStringList OTRListStr;

    //Process all pending datagrams, one by one
    while(udpSocketOut->hasPendingDatagrams()){
        qint64 pendingSize = udpSocketOut->pendingDatagramSize();
        int readSize = (pendingSize > 8192) ? 8192 : pendingSize;
        QByteArray buffer;
        buffer.resize(readSize);

        QHostAddress sender;        //Stores sender's address
        quint16 senderPort;         //Stores sender's port
        udpSocketOut->readDatagram(buffer.data(),buffer.size(),&sender,&senderPort);

        //Convert datagram data to a QString for processing
        QString binaryString = QString(buffer);

        //Process string in 32-character segments
        for (int i = 0; i + 32 <= binaryString.length(); i += 32){
            QString chunk = binaryString.mid(i,32);

            //Extracts fields from the chunk
            QString FrequencyRas    = chunk.mid(0,4);
            QString SCoil           = chunk.mid(4,1);
            QString ECoil           = chunk.mid(5,1);
            QString ADC             = chunk.mid(6,1);
            ADCListStr.append(ADC); //Collect ADC values
            QString OTR             = chunk.mid(7,1);
            OTRListStr.append(OTR); //Collecr OTR values
            QString IData           = chunk.mid(8,8);
            QString FrequencyStand  = chunk.mid(16,8);
            QString QData           = chunk.mid(24,8);

        //Format fields into a single string
        QString formattedChunk = QString("%1,%2,%3,%4;%5,%6;")
                                .arg(FrequencyRas)
                                .arg(SCoil)
                                .arg(ECoil)
                                .arg(IData)
                                .arg(FrequencyStand)
                                .arg(QData);
        formattedChunks.append(formattedChunk);
    }

    //Calculate sum of OTR values
        quint32 sumOTR = 0;
        for (const QString &otrStr : OTRListStr){
            bool sumOTRok = false;
            quint32 value = otrStr.toUInt(&sumOTRok, 16);
            if(!sumOTRok){
                ui->outputMessageLog->append("Error: Invalid Hexadecimal OTR value: " + otrStr);
                return;
            }
            sumOTR += value;
        }
        if (sumOTR > 0){
            ui->booleanOverRange->setText("YES");
        } else {
            ui->booleanOverRange->setText("NO");
        }

        //Calculate mode of ADC values
        QHash<QString, int> adcCount;
        for (const QString &adcStr : ADCListStr){
            bool ADCok = false;
            quint32 value = adcStr.toUInt(&ADCok, 16);
            if(!ADCok){
                ui->outputMessageLog->append("Error: Invalid Hexadecimal ADC value: " + adcStr);
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
        ui->outputADCLevel->setText(QString::number(finalMode));

        //Reverse overall formatted string
        QString finalOutput = formattedChunks.join("");
        std::reverse(finalOutput.begin(),finalOutput.end());
        formattedRawData = finalOutput;
        ui->outputRawData->append(finalOutput);

        //Tokenise final output string using commas and semicolons
        static const QRegularExpression re("[,;]");
        QStringList tokens = finalOutput.split(re, Qt::SkipEmptyParts);
        outputTokens = tokens;
        ui->outputMessageLog->append("Tokens: ");
        for (const QString &token : tokens){
            ui->outputMessageLog->append(token);
        }

        //Convert each token (assumed hexadecimal) to a 32-bit integer
        for (const QString &token : outputTokens){
            bool ok = false;
            quint32 uValue = token.toUInt(&ok, 16);
            if (!ok) {
                ui->outputMessageLog->append("Error converting token to int: " + token);
                continue;
            }
            qint32 value = static_cast<qint32>(uValue);
            convertedIntegers.append(value);
        }
        ui->outputMessageLog->append("Converted Integers:");
        for (qint32 val : convertedIntegers) {
            ui->outputMessageLog->append(QString::number(val));
        }

        //Reverse list of converted integers
        //std::reverse(convertedIntegers.begin(),convertedIntegers.end());
        //ui->outputMessageLog->append("Converted Integers (Reverse): ");
        for (qint32 val : convertedIntegers){
            ui->outputMessageLog->append(QString::number(val));
        }

        //Decimate reversed integers into 6 separate arrays
        QList<qint32>decimated[6];
        for (int i = 0; i < convertedIntegers.size(); ++i){
            int index = i % 6;
            decimated[index].append(convertedIntegers[i]);
        }
        for (int j = 0; j < 6; ++j) {
            QString arrStr = QString("Array %1: ").arg(j + 1);  // Display as Array 1, Array 2, etc.
            for (qint32 num : decimated[j]) {
                arrStr += QString::number(num) + " ";
            }
            //ui->outputMessageLog->append(arrStr);
        }

        //Update outputSamplesPacket display
        int secondArraySize = decimated[1].size();
        ui->outputSamplesPackets->display(secondArraySize);

        //Process final frequency data fro "High Resolution" mode
        finalFrequency.clear();
        for(int i=0; i<decimated[0].size(); ++i){
            qint32 newVal = decimated[0].at(i) * 8;
            qint64 newValue = static_cast<qint64>(newVal);
            finalFrequency.append(newValue);
        }
        ui->outputMessageLog->append("finalFrequency array: ");
        QString freqFinalStr;
        for (qint32 val : finalFrequency){
                 freqFinalStr += QString::number(val) + " ";
             }
        ui->outputMessageLog->append(freqFinalStr);

        //Process fourth decimated array: convert to double and divide by 2^31
        QList<double>fourthArrayDivided;
        double divisor = qPow(2.0,31.0);
        for (int i = 0; i < decimated[3].size(); ++i){
            double dVal = static_cast<double>(decimated[3].at(i));
            double result = dVal / divisor;
            fourthArrayDivided.append(result);
        }

        ui->outputMessageLog->append("Fourth Array (Converted to Double and divided by 2^31):");
        for (double d : fourthArrayDivided) {
            ui->outputMessageLog->append(QString::number(d));}

        //Process sixth decimated array: convert to double and divide by 2^31
        QList<double>sixthArrayDivided;
        for (int i = 0; i < decimated[5].size(); ++i){
        double dVal = static_cast<double>(decimated[5].at(i));
        double result = dVal / divisor;
        sixthArrayDivided.append(result);
        }

        ui->outputMessageLog->append("Sixth Array (Converted to Double and divided by 2^31):");
        for (double d : sixthArrayDivided) {
            ui->outputMessageLog->append(QString::number(d));}

        //Global Buffering
        for (const qint64 &val : qAsConst(finalFrequency)) {
            bufferFinalFrequency.enqueue(val);
        }
        for (const qint32 &val : qAsConst(decimated[1])) {
            bufferDecimated1.enqueue(val);
        }
        for (const qint32 &val : qAsConst(decimated[2])) {
            bufferDecimated2.enqueue(val);
        }
        for (const double &d : qAsConst(fourthArrayDivided)) {
            bufferFourthArrayDivided.enqueue(d);
        }
        for (const double &d : qAsConst(sixthArrayDivided)) {
            bufferSixthArrayDivided.enqueue(d);
        }
        //Append processed arrays to their respective global buffers
        ui->outputMessageLog->append("Buffer Final Frequency:");
        for (const qint64 &val : qAsConst(bufferFinalFrequency)) {
            ui->outputMessageLog->append(QString::number(val));
        }
        ui->outputMessageLog->append("Buffer Decimated 1:");
        for (const qint32 &val : qAsConst(bufferDecimated1)) {
            ui->outputMessageLog->append(QString::number(val));
        }
        ui->outputMessageLog->append("Buffer Decimated 2:");
        for (const qint32 &val : qAsConst(bufferDecimated2)) {
            ui->outputMessageLog->append(QString::number(val));
        }
        ui->outputMessageLog->append("Buffer Fourth Array Divided:");
        for (const double &d : qAsConst(bufferFourthArrayDivided)) {
            ui->outputMessageLog->append(QString::number(d));
        }
        ui->outputMessageLog->append("Buffer Sixth Array Divided:");
        for (const double &d : qAsConst(bufferSixthArrayDivided)) {
            ui->outputMessageLog->append(QString::number(d));
        }
    }
}
void MainWindow::onProcessedDataReady(const QString &result)
{
    ui->outputFinalData->append(result);
}
void MainWindow::processBufferElements(int n)
{
    //ui->outputMessageLog->append("Processing buffers for " + QString::number(n) + " elements per buffer: ");

    QList<qint64> freqBuffer;
    QList<qint32> decimated1Buffer;
    QList<qint32> decimated2Buffer;
    QList<double> fourthArrayBuffer;
    QList<double> sixthArrayBuffer;

    QList<qint64> freqBufferDecimated;
    QList<qint32> decimated1BufferDecimated;
    QList<qint32> decimated2BufferDecimated;
    QList<double> fourthArrayBufferDecimated;
    QList<double> sixthArrayBufferDecimated;

    QList<qint32> headerArray;

    for (int i = 0; i < n; ++i){
        if (!bufferFinalFrequency.isEmpty())
            freqBuffer.append((bufferFinalFrequency.dequeue()));
        if (!bufferDecimated1.isEmpty())
            decimated1Buffer.append((bufferDecimated1.dequeue()));
        if (!bufferDecimated2.isEmpty())
            decimated2Buffer.append((bufferDecimated2.dequeue()));
        if (!bufferFourthArrayDivided.isEmpty())
            fourthArrayBuffer.append((bufferFourthArrayDivided.dequeue()));
        if (!bufferSixthArrayDivided.isEmpty())
            sixthArrayBuffer.append((bufferSixthArrayDivided.dequeue()));
    }

    ui->outputMessageLog->append("Dequed Final Frequency: ");
    for (const qint64 &val : freqBuffer){
        ui -> outputMessageLog->append(QString::number(val));
    }
    ui->outputMessageLog->append("Dequeued Decimated 1:");
    for (const qint32 &val : decimated1Buffer) {
        ui->outputMessageLog->append(QString::number(val));
    }
    ui->outputMessageLog->append("Dequeued Decimated 2:");
    for (const qint32 &val : decimated2Buffer) {
        ui->outputMessageLog->append(QString::number(val));
    }
    ui->outputMessageLog->append("Dequeued Fourth Array Divided:");
    for (const double &d : fourthArrayBuffer) {
        ui->outputMessageLog->append(QString::number(d));
    }
    ui->outputMessageLog->append("Dequeued Sixth Array Divided:");
    for (const double &d : sixthArrayBuffer) {
        ui->outputMessageLog->append(QString::number(d));
    }

    if(ui->buttonSync->isChecked()){
        qDebug() << "Executing";
        if(!decimated1Buffer.isEmpty()){
            int X = decimated1Buffer.at(0);
            bool unequalFlag = false;
            int iterationIndex = 0;
            int limit = qMin(5, decimated1Buffer.size());
            for (iterationIndex = 0; iterationIndex < limit; iterationIndex++){
                int Y = decimated1Buffer.at(iterationIndex);
                if (X != Y) {
                    unequalFlag = true;
                    break;
                }
                X = Y;
            }
            iterationIndex = qBound(0, iterationIndex, 4);
            if (unequalFlag){
                if (iterationIndex == 4)
                    autosync = 0;
                else
                    autosync = iterationIndex;
            }
        }
    }
    ui->outputAutoSync->display(autosync);

    //Rotate all five buffers by the autosync value
    if(autosync>0){
        if(!freqBuffer.isEmpty()){
            int k = autosync % freqBuffer.size();
            std::rotate(freqBuffer.begin(), freqBuffer.end() - k, freqBuffer.end());
        }
        if(!decimated1Buffer.isEmpty()){
            int k = autosync % decimated1Buffer.size();
            std::rotate(decimated1Buffer.begin(), decimated1Buffer.end() - k, decimated1Buffer.end());
        }
        if(!decimated2Buffer.isEmpty()){
            int k = autosync % decimated2Buffer.size();
            std::rotate(decimated2Buffer.begin(), decimated2Buffer.end() - k, decimated2Buffer.end());
        }
        if(!fourthArrayBuffer.isEmpty()){
            int k = autosync % fourthArrayBuffer.size();
            std::rotate(fourthArrayBuffer.begin(), fourthArrayBuffer.end() - k, fourthArrayBuffer.end());
        }
        if(!sixthArrayBuffer.isEmpty()){
            int k = autosync % sixthArrayBuffer.size();
            std::rotate(sixthArrayBuffer.begin(), sixthArrayBuffer.end() - k, sixthArrayBuffer.end());
        }
    }

    // Log the rotated buffers.
    ui->outputMessageLog->append("Rotated Final Frequency:");
    qDebug() << "Rotated Final Frequency:";
    for (const qint64 &val : freqBuffer){
        ui->outputMessageLog->append(QString::number(val));
        qDebug() << val;
    }
    ui->outputMessageLog->append("Rotated Decimated 1:");
    qDebug() << "Rotated Decimated 1:";
    for (const qint32 &val : decimated1Buffer){
        ui->outputMessageLog->append(QString::number(val));
        qDebug() << val;
    }
    ui->outputMessageLog->append("Rotated Decimated 2:");
    qDebug() << "Rotated Decimated 2:";
    for (const qint32 &val : decimated2Buffer){
        ui->outputMessageLog->append(QString::number(val));
        qDebug() << val;
    }
    ui->outputMessageLog->append("Rotated Fourth Array Divided:");
    qDebug() << "Rotated Fourth Array Divided:";
    for (const double &d : fourthArrayBuffer){
        ui->outputMessageLog->append(QString::number(d));
        qDebug() << d;
    }
    ui->outputMessageLog->append("Rotated Sixth Array Divided:");
    qDebug() << "Rotated Sixth Array Divided:";
    for (const double &d : sixthArrayBuffer){
        ui->outputMessageLog->append(QString::number(d));
        qDebug() << d;
    }
    for (int i = 3; i < freqBuffer.size(); i += 4){
        freqBufferDecimated.append(freqBuffer.at(i));
    }
    for (int i = 3; i < decimated1Buffer.size(); i += 4){
        decimated1BufferDecimated.append(decimated1Buffer.at(i));
    }
    for (int i = 3; i < decimated2Buffer.size(); i += 4){
        decimated2BufferDecimated.append(decimated2Buffer.at(i));
    }
    for (int i = 3; i < fourthArrayBuffer.size(); i += 4){
        fourthArrayBufferDecimated.append(fourthArrayBuffer.at(i));
    }
    for (int i = 3; i < sixthArrayBuffer.size(); i += 4){
        sixthArrayBufferDecimated.append(sixthArrayBuffer.at(i));
    }

    qDebug() << "Decimated Final Frequency (every 4th element):" << freqBufferDecimated;
    qDebug() << "Decimated Decimated 1 (every 4th element):" << decimated1BufferDecimated;
    qDebug() << "Decimated Decimated 2 (every 4th element):" << decimated2BufferDecimated;
    qDebug() << "Decimated Fourth Array Divided (every 4th element):" << fourthArrayBufferDecimated;
    qDebug() << "Decimated Sixth Array Divided (every 4th element):" << sixthArrayBufferDecimated;

    if (!freqBufferDecimated.isEmpty()){
        qDebug() << freqBufferDecimated.first();
        ui->outpuActualFrequency->display(static_cast<double>(freqBufferDecimated.first()));
    } else
        ui->outpuActualFrequency->display(0);

    int count = qMin(decimated1BufferDecimated.size(), decimated2BufferDecimated.size());
    for (int i = 0; i < count; ++i){
        qint32 S = decimated1BufferDecimated.at(i);
        qint32 E = decimated2BufferDecimated.at(i);
        qint32 Y = 0;
        if (S==0)
            S = 16;
        if (E==0)
            E = 16;
        if (S==E) {
            Y = 0;
        } else if (S < E) {
            Y = (E-1)+16*(S-1)-(((S*(S+1))/2)-1);
        } else { //S>E
            Y = 16*(E-1)-((((E-1)*E)/2)-1)+(S-E-1);
        }
        headerArray.append(Y);
    }

    int minSize =   qMin(headerArray.size(),
                    qMin(decimated2BufferDecimated.size(),
                    qMin(decimated1BufferDecimated.size(),
                    qMin(fourthArrayBufferDecimated.size(),
                    qMin(sixthArrayBufferDecimated.size(),freqBufferDecimated.size())))));

    for (int i = 0; i < minSize; ++i){
        global2DArray[0].append(static_cast<double>(headerArray.at(i)));
        global2DArray[1].append(static_cast<double>(decimated2BufferDecimated.at(i)));
        global2DArray[2].append(static_cast<double>(decimated1BufferDecimated.at(i)));
        global2DArray[3].append(fourthArrayBufferDecimated.at(i));
        global2DArray[4].append(sixthArrayBufferDecimated.at(i));
        global2DArray[5].append(static_cast<double>(freqBufferDecimated.at(i)));
    }

    printGlobal2DArrayToTextBrowser();

    if(clear2DArray){
        for (int i = 0; i < global2DArray.size(); ++i){
            global2DArray[i].clear();
        }
    }
    qDebug() << "Global 2D Array:" << global2DArray;

}*/

    void MainWindow::printGlobal2DArrayToTextBrowser()
{
    if(m_stopUpdates){
        return;
    }
    QString output;
    QStringList rowLabels = {"Header","S","E","Fourth","Sixth","Frequency"};
    for (int i = 0; i < global2DArray.size(); ++i) {
        output += rowLabels.at(i) + ":";
        for (int j = 0; j < global2DArray[i].size(); ++j) {
            output += QString::number(global2DArray[i][j]) + " ";
        }
        output += "\n";
    }
    ui->outputFinalData->setText(output);
}


void MainWindow::onbuttonStopFinalDataclicked()
{
    m_stopUpdates = true;
}


void MainWindow::onbuttonStartFinalDataclicked()
{
    m_stopUpdates = false;
}

void MainWindow::onbuttonClearFinalDataclicked()
{
    ui->outputFinalData->append("Clearing");
    ui->outputFinalData->clear();
}


void MainWindow::onbuttonSaveclicked()
{
    csvFilePath = ui->inputMeasurementFilePath -> toPlainText().trimmed();
    if (csvFilePath.isEmpty()){
        qDebug() << "Error: CSV file path is empty.";
        ui->buttonSave->setEnabled(true);
        clear2DArray = true;
        return;
    }

    if (csvFilePath != lastSavedFilePath){
        fileInitialised = false;
        lastSavedFilePath = csvFilePath;
    }

    QFileInfo fileInfo(csvFilePath);
    QDir dir = fileInfo.absoluteDir();
    if(!dir.exists()){
        if(!dir.mkpath(".")){
            qDebug()<<"Error:Could not create directory:"<<dir.absolutePath();
            ui->buttonSave->setEnabled(true);
            clear2DArray = true;
            return;
        }
    }

    bool overwrite = ui->buttonOverwriteFile->isChecked();
    QFile file(csvFilePath);

    if (!overwrite) {
        // Overwrite == false.
        // On the first click:
        if (!fileInitialised) {
            // If file exists preexisting, abort.
            if (file.exists()) {
                qDebug() << "File already exists and overwrite is not allowed. Aborting save.";
                ui->buttonSave->setEnabled(true);
                clear2DArray = true;
                return;
            }
            // Else, file doesn't exist: initialize it.
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qDebug() << "Error: Could not create CSV file." << file.errorString();
                ui->buttonSave->setEnabled(true);
                clear2DArray = true;
                return;
            }
            QTextStream out(&file);
            out << "State,Excitation Coil,Sensing Coil,Real(I),Imaginary(Q),Frequency\n";
            file.close();
            fileInitialised = true;
        }
        // If fileInitialized is true, proceed to append.
    } else {
        // Overwrite == true.
        // On the first click:
        if (!fileInitialised) {
            // Open in WriteOnly mode (this clears the file if it exists, or creates it)
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qDebug() << "Error: Could not open CSV file for writing." << file.errorString();
                ui->buttonSave->setEnabled(true);
                clear2DArray = true;
                return;
            }
            QTextStream out(&file);
            out << "State,Excitation Coil,Sensing Coil,Real(I),Imaginary(Q),Frequency\n";
            file.close();
            fileInitialised = true;
        }
        // If fileInitialized is true, proceed to append.
    }

    ui->buttonSave->setEnabled(false);
    clear2DArray = false;
    setFrames = ui->inputFrames->value();
    ui->outputSavedFrames->setText(QString::number(framesSaved));
}

void MainWindow::onProcessedChunkResult(const QVector<QVector<double> > &global2DArray)
{
    if (clear2DArray)
        return;

    if (framesSaved >= setFrames)
        return;

    if(!csvFilePath.isEmpty()){
        QFile file(csvFilePath);
        if (file.open(QIODevice::Append | QIODevice::Text)){
            QTextStream out(&file);
            int numElements = global2DArray[0].size();
            for (int col = 0; col < numElements; ++col){
                QStringList rowData;
                for (int row = 0; row < global2DArray.size(); ++row){
                    rowData << QString::number(global2DArray[row][col]);
                }
                out << rowData.join(",") << "\n";
            }
            file.close();
        } else {
            qDebug() << "Error: Could not open CSV file for appending.";
        }
    }
        framesSaved++;
        ui->outputSavedFrames->setText(QString::number(framesSaved));

        if (framesSaved >= setFrames){
            clear2DArray = true;
            framesSaved = 0;
            ui->buttonSave->setEnabled(true);
        }
}

void MainWindow::onbuttonSyncclicked()
{
    bool flag = true;
    dataConsumer->m_syncEnabled.storeRelease(flag);
    //qDebug() << flag;
    //qDebug() << "AutoSync button clicked";
}

