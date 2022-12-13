.. _trace_app:

Assignment 4
##################################
Author:- Roshan Raj Kanakaiprath
ASU ID:- 1222478062
##################################

Overview
********

A simple zephyr application that implements the polling server to process aperiodic requests
from a global queue which receives requests at random interval with random computation.

Building and Running
********************

Building:
   : On Linux Terminal change directory to project_4 folder.
   : Use the following command to build the application
      "west build -b mimxrt1050_evk"
   : 'build' folder should be present in trace_app now.

Running:
   : Connect the "mimxrt1050_evkb" board to the system using USB
   : Open PuTTY SSH Client
   : On the PuTTY Configuration window perfom the following steps
      : Enter "/dev/ttyACM0" in the Serial line box
      : Enter "115200" in the Speed box
      : Select "serial" as connection type
      : Click "Open" to start a session.
   : On Linux terminal(project_4 directory) use the following command to flash the build
      "west flash"
   : Wait for flashing to get over.
   : On the PuTTY session check if the average response time and total requests processed are displayed
   : Execution is done when it prints "Exiting from main"

To get the response time with Background Server, at line no. 52 in task_model_p4_new.h
change #define POLL_PRIO   6  to #define POLL_PRIO   14


Average response time of polling server is 79 ms
Average response time of background server is 150 ms. 




