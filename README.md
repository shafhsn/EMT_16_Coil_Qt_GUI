# C++/Qt GUI for Electromagnetic Tomography System
**Developed for**: Third year final project on replacing an existing LabVIEW-based GUI with the C++/Qt framework.
The project repository is found in: https://github.com/shafhsn/EMT_16_Coil_Qt_GUI

## OVERVIEW
This repository consists of the all files required to download and run the newly developed Qt-based Graphical User Interface(GUI) for a 16-coil high-frequency Electromagnetic Tomography (EMT) system. This has been developed to replace the existing LabVIEW-based GUI which are resource inefficient.
The application is designed for real-time data acquistion (through UDP), processing, formatting, and saving of measurement data. However, it does not include any imaging plots at the moment. Responsiveness of the user interface is currently maintained by three thread (including the main thread) with three implementations of communications between them. 
The software improves on its predecessor by significantly reducing in CPU and memory usage, data saving time, and much better usability. 

## FILES DESCRIPTION
mainwindow.h, mainwindow.cpp, main.cpp - main thread to keep GUI functional.  
processingdata.h, processingdata.cpp - worker thread to process raw data.  
dataconsumer.h, dataconsumer.cpp - worker thread to format processed data.  
EMT_IP.pro - project, open this from the IDE.  
EMT_IP.pro.user - project file that's best not touched.  
mainwindow.ui - enables modification of interface elements.  
sharedbuffer.h, sharedbuffer.cpp - storage containers used for inter-thread communication.  
mainwindow_copy.ui, worker.h, worker.cpp - redundant but keep in project to avoid unexpected behaviour.  
**<ins>Please do not be selective, download all files</ins>.**

## INSTALLATION
The exact versions are recommended to avoid any issues running the project.   
**IDE**: Qt Creator 14.0.2   
**Build-kit**: Qt 5.15.2 MinGW 64-bit   

## BUILDING THE PROJECT
Launch Qt Creator.  
Browse and open **EMT_IP.pro**.  
If asked to choose build kit, use one specified in **INSTALLATION**.  
Build project and Run.  

## IMPORTANT USAGE NOTES
The GUI will fail to communicate with the project if ethernet settings are not configured properly (needs to be connected to instrument).  
If wanting to test offline (no instrument), open **mainwindow.cpp** and replace the _QHostAddress_ instances by _QHostAddress::LocalHost_  

## FUTURE IMPLEMENTATIONS
Inclusion of image reconstruction plots and visuals.   
Extenting support to 8-coil EMT systems.

## MODIFICATIONS
Without the author's autherisation, no one may modify or contribute to this project.  

