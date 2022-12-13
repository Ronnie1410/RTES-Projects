.. _trace_app:

Periodic Threads in Zephyr RTOS
##################################
Author:- Roshan Raj Kanakaiprath
ASU ID:- 1222478062
##################################

Overview
********

A simple zephyr application that demonstartes the concepts of real-time
scheduling of periodic tasks and resource sharing using a multi-threaded program.

Building and Running
********************

Building:
   : On Linux Terminal change directory to trace_app folder.
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
   : On Linux terminal(trace_app directory) use the following command to flash the build
      "west flash"
   : Wait for flashing to get over.
   : On the PuTTY session check if the following lines are dispalyed and the cursor is at 
      the end of uart:~$
      
      "***Booting Zephyr OS build zephyr-v2.6.0 ***"

      uart:~$
      
   : Type "activate" and press Enter
   : Verify the session prints "task## missed deadline" and 
      stops printing after "Exit from Main" is printed.


Sample Output
=============

.. /dev/ttyACM0 PuTTY:: console

   task00 missed deadline
   task11 missed deadline
   task22 missed deadline
   task11 missed deadline
   .
   .
   .
   .
   Exit from Main

