
**********************CSE522-Real Time Embedded Systems*******
*****************************Assignment-2*****************
Name - Roshan Raj Kanakaiprath
ASU ID - 1222478062

Description : 

In this assignment, an application program is developed which implements several components/applications to demonstrate 
the operations and communication of IoT devices in Zephyr RTOS running on MIMXRT1050-EVKB board. The application serves 
as a COAP server using Zephyr's network stack. The server interacts with Cu4Cr Coap user agent which provides methods to browse and 
interact with devices via COAP protocol.


******************************************************************************

*****************Connection Steps*******************************************

1. Connect RGB Led pins as per the assignment Document.
2. Connect HCSR-04 sensor as per the Assignment document. Use 5V for Vcc.
3. Connect the MIMXRT1050K board's ethernet to a router/lan port on the computer.
4. Obtain the ip address of the board-device.

******************************************************************************


*****************Steps to compile and execute the code*****************


1. Unzip the RTES-Kanakaiprath-RoshanRaj_03.zip in the zephyrproject directory.

2. Make the necessary changes to zephyr source tree as per Assignment Document.(port the distance sensor driver to zephyr source tree)

2. To build, run west build -b mimxrt1050_evk project_3.

3. Run west flash.
	
4. Open putty and select port /dev/ttyACM0 and enter baud rate 115200 and start a new serial session

5. Wait for the server to complete initialization.

6. On Google chrome click on Cu4Cr extension(follow steps in Assignment document to install the Cu4Cr extension). 

7. Enter the ip address from step#4(Connection Steps) and press enter.

8. Click on Discover. Resources should be visible on the left panel.

9. Interact with each resource as per the methods given in Assignment document.

10. For 'put' method enter the value in the outgoing box.

11. For 'get' and observe watch for the values in incoming box.



	



