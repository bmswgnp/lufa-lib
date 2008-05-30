/*
             MyUSB Library
     Copyright (C) Dean Camera, 2008.
              
  dean [at] fourwalledcubicle [dot] com
      www.fourwalledcubicle.com

 Released under the LGPL Licence, Version 3
*/

/*
	Keyboard demonstration application by Denver Gingerich.

	This example is based on the MyUSB Mouse demonstration application,
	written by Dean Camera.
*/

/*
	Keyboard demonstration application, using endpoint interrupts. This
	gives a simple reference application for implementing a USB Keyboard
	using the basic USB HID drivers in all modern OSes (i.e. no special
	drivers required).
	
	On startup the system will automatically enumerate and function
	as a keyboard when the USB connection to a host is present. To use
	the keyboard example, manipulate the joystick to send the letters
	a, b, c, d and e. See the USB HID documentation for more information
	on sending keyboard event and keypresses.
*/

/*
	USB Mode:           Device
	USB Class:          Human Interface Device (HID)
	USB Subclass:       Keyboard
	Relevant Standards: USBIF HID Standard
	                    USBIF HID Usage Tables 
	Usable Speeds:      Low Speed Mode, Full Speed Mode
*/

#include "KeyboardViaInt.h"

/* Project Tags, for reading out using the ButtLoad project */
BUTTLOADTAG(ProjName,     "MyUSB KeyboardI App");
BUTTLOADTAG(BuildTime,    __TIME__);
BUTTLOADTAG(BuildDate,    __DATE__);
BUTTLOADTAG(MyUSBVersion, "MyUSB V" MYUSB_VERSION_STRING);

/* Scheduler Task List */
TASK_LIST
{
	{ Task: USB_USBTask          , TaskStatus: TASK_STOP },
};

/* Global Variables */
USB_KeyboardReport_Data_t KeyboardReportData = {Modifier: 0, KeyCode: 0};


int main(void)
{
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable Clock Division */
	SetSystemClockPrescaler(0);

	/* Hardware Initialization */
	Joystick_Init();
	LEDs_Init();
	
	/* Indicate USB not ready */
	LEDs_SetAllLEDs(LEDS_LED1 | LEDS_LED3);
	
	/* Initialize Scheduler so that it can be used */
	Scheduler_Init();

	/* Initialize USB Subsystem */
	USB_Init();
	
	/* Scheduling - routine never returns, so put this last in the main function */
	Scheduler_Start();
}

EVENT_HANDLER(USB_Connect)
{
	/* Start USB management task */
	Scheduler_SetTaskMode(USB_USBTask, TASK_RUN);

	/* Indicate USB enumerating */
	LEDs_SetAllLEDs(LEDS_LED1 | LEDS_LED4);
}

EVENT_HANDLER(USB_Disconnect)
{
	/* Stop running keyboard reporting and USB management tasks */
	Scheduler_SetTaskMode(USB_USBTask, TASK_STOP);

	/* Indicate USB not ready */
	LEDs_SetAllLEDs(LEDS_LED1 | LEDS_LED3);
}

EVENT_HANDLER(USB_ConfigurationChanged)
{
	/* Setup Keyboard Keycode Report Endpoint */
	Endpoint_ConfigureEndpoint(KEYBOARD_EPNUM, EP_TYPE_INTERRUPT,
		                       ENDPOINT_DIR_IN, KEYBOARD_EPSIZE,
	                           ENDPOINT_BANK_SINGLE);

	/* Enable the endpoint IN interrupt ISR for the report endpoint */
	USB_INT_Enable(ENDPOINT_INT_IN);

	/* Setup Keyboard LED Report Endpoint */
	Endpoint_ConfigureEndpoint(KEYBOARD_LEDS_EPNUM, EP_TYPE_INTERRUPT,
		                       ENDPOINT_DIR_OUT, KEYBOARD_EPSIZE,
	                           ENDPOINT_BANK_SINGLE);

	/* Enable the endpoint OUT interrupt ISR for the LED report endpoint */
	USB_INT_Enable(ENDPOINT_INT_OUT);

	/* Indicate USB connected and ready */
	LEDs_SetAllLEDs(LEDS_LED2 | LEDS_LED4);
}

HANDLES_EVENT(USB_UnhandledControlPacket)
{
	/* Handle HID Class specific requests */
	switch (Request)
	{
		case REQ_GetReport:
			if (RequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				/* Ignore report type and ID number value */
				Endpoint_Ignore_Word();
				
				/* Ignore unused Interface number value */
				Endpoint_Ignore_Word();

				/* Read in the number of bytes in the report to send to the host */
				uint16_t BytesToSend     = Endpoint_Read_Word_LE();
				
				/* Get a pointer to the HID IN report */
				uint8_t* ReportPointer   = (uint8_t*)&KeyboardReportData;
				uint16_t ReportSize      = sizeof(KeyboardReportData);
				
				bool     SendZLP;

				Endpoint_ClearSetupReceived();
	
				/* If trying to send more bytes than exist to the host, clamp the value at the report size */
				if (BytesToSend > ReportSize)
				  BytesToSend = ReportSize;
	
				/* If the number of bytes to send will be an integer multiple of the control endpoint size,
				   a zero length packet must be sent afterwards to terminate the transfer */
				SendZLP = !(BytesToSend % USB_ControlEndpointSize);
				
				/* Loop while still bytes to send and the host hasn't aborted the transfer (via an OUT packet) */
				while (BytesToSend && (!(Endpoint_IsSetupOUTReceived())))
				{
					/* Wait until endpoint is ready for an IN packet */
					while (!(Endpoint_IsSetupINReady()));
					
					/* Write out one packet's worth of data to the endpoint, until endpoint full or all data written */
					while (BytesToSend && (Endpoint_BytesInEndpoint() < USB_ControlEndpointSize))
					{
						Endpoint_Write_Byte(pgm_read_byte(ReportPointer++));
						BytesToSend--;
					}
					
					/* Send the endpoint packet to the host */
					Endpoint_ClearSetupIN();
				}
				
				/* Check if the host aborted the transfer prematurely with an OUT packet */
				if (Endpoint_IsSetupOUTReceived())
				{
					/* Clear the OUT packet, abort any further communications for the request */
					Endpoint_ClearSetupOUT();
					return;
				}
				
				/* If a zero length packet needs to be sent, send it now */
				if (SendZLP)
				{
					while (!(Endpoint_IsSetupINReady()));
					Endpoint_ClearSetupIN();
				}

				/* Wait until host acknowledges the transfer */
				while (!(Endpoint_IsSetupOUTReceived()));
				  Endpoint_ClearSetupOUT();
			}
		
			break;
	}
}

ISR(ENDPOINT_PIPE_vect)
{
	/* Save previously selected endpoint before selecting a new endpoint */
	uint8_t PrevSelectedEndpoint = Endpoint_GetCurrentEndpoint();

	/* Check if keyboard endpoint has interrupted */
	if (Endpoint_HasEndpointInterrupted(KEYBOARD_EPNUM))
	{
		uint8_t JoyStatus_LCL = Joystick_GetStatus();

		if (JoyStatus_LCL & JOY_UP)
		  KeyboardReportData.KeyCode = 0x04; // A
		else if (JoyStatus_LCL & JOY_DOWN)
		  KeyboardReportData.KeyCode = 0x05; // B

		if (JoyStatus_LCL & JOY_LEFT)
		  KeyboardReportData.KeyCode = 0x06; // C
		else if (JoyStatus_LCL & JOY_RIGHT)
		  KeyboardReportData.KeyCode = 0x07; // D

		if (JoyStatus_LCL & JOY_PRESS)
		  KeyboardReportData.KeyCode = 0x08; // E

		/* Clear the Keyboard Report endpoint interrupt and select the endpoint */
		Endpoint_ClearEndpointInterrupt(KEYBOARD_EPNUM);
		Endpoint_SelectEndpoint(KEYBOARD_EPNUM);

		/* Write Keyboard Report Data */
		Endpoint_Write_Stream_LE(&KeyboardReportData, sizeof(KeyboardReportData));

		/* Handshake the IN Endpoint - send the data to the host */
		Endpoint_ClearCurrentBank();
			
		/* Clear the report data afterwards */
		KeyboardReportData.Modifier = 0;
		KeyboardReportData.KeyCode  = 0;
		
		/* Clear the endpoint IN interrupt flag */
		USB_INT_Clear(ENDPOINT_INT_IN);
	}

	/* Check if Keyboard LED status Endpoint has interrupted */
	if (Endpoint_HasEndpointInterrupted(KEYBOARD_LEDS_EPNUM))
	{
		/* Clear the endpoint OUT interrupt flag */
		USB_INT_Clear(ENDPOINT_INT_OUT);

		/* Clear the Keyboard LED Report endpoint interrupt and select the endpoint */
		Endpoint_ClearEndpointInterrupt(KEYBOARD_LEDS_EPNUM);
		Endpoint_SelectEndpoint(KEYBOARD_LEDS_EPNUM);

		/* Read in the LED report from the host */
		uint8_t LEDStatus = Endpoint_Read_Byte();
		uint8_t LEDMask   = LEDS_LED2;
		
		if (LEDStatus & 0x01) // NUM Lock
		  LEDMask |= LEDS_LED1;
		
		if (LEDStatus & 0x02) // CAPS Lock
		  LEDMask |= LEDS_LED3;

		if (LEDStatus & 0x04) // SCROLL Lock
		  LEDMask |= LEDS_LED4;

		/* Set the status LEDs to the current Keyboard LED status */
		LEDs_SetAllLEDs(LEDMask);

		/* Handshake the OUT Endpoint - clear endpoint and ready for next report */
		Endpoint_ClearCurrentBank();
	}
	
	/* Restore previously selected endpoint */
	Endpoint_SelectEndpoint(PrevSelectedEndpoint);
}

