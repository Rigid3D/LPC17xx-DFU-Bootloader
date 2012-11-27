#include "usbcore.h"

#include "usbhw.h"
#include "lpc17xx_usb.h"
#include "dfu.h"
#include "descriptor.h"

#include <stdio.h>

#ifndef NULL
#define NULL ((void *) 0)
#endif

CONTROL_TRANSFER control;

void DFU_EP0in(void);
void DFU_EP0out(void);

uint8_t control_buffer[64];

usbdesc_base *descriptors;

void usb_provideDescriptors(void *d)
{
	descriptors = (usbdesc_base *) d;
}

void requestGetStatus()
{
	control_buffer[0] = control_buffer[1] = 0;
	control.bufferlen = 2;
}

void requestGetDescriptor()
{
	uint8_t dType = control.setup.wValue >> 8;
	uint8_t dIndex = control.setup.wValue & 0xFF;

	usbdesc_base *d = descriptors;

	int i = 0;

	while (d->bLength > 0)
	{
		if (d->bDescType == dType)
		{
			switch (d->bDescType)
			{
				case DT_DEVICE:
					dIndex = i = 0;
					break;
				case DT_CONFIGURATION:
					dIndex = i = ((usbdesc_configuration *) d)->bConfigurationValue;
					break;
				case DT_INTERFACE:
					i = ((usbdesc_interface *) d)->bInterfaceNumber;
					break;
				case DT_ENDPOINT:
					i = ((usbdesc_endpoint *) d)->bEndpointAddress;
					break;
			}
			if (i == dIndex)
			{
				control.buffer = d;
				if (dType == DT_CONFIGURATION)
				{
					control.bufferlen = ((usbdesc_configuration *) d)->wTotalLength;
				}
				else
				{
					control.bufferlen = d->bLength;
				}
				if (control.bufferlen > control.setup.wLength)
					control.bufferlen = control.setup.wLength;
// 				printf("FOUND descriptor 0x%x:0x%x with length %d\n", dType, dIndex, control.bufferlen);
				return;
			}
			i++;
		}

		d = (usbdesc_base *) (((uint8_t *) d) + d->bLength);
	}
// 	printf("descriptor 0x%x:0x%x NOT FOUND\n", dType, dIndex);
	control.bufferlen = 0;
	control.zlp = 1;
}

void requestSetConfiguration()
{
	SIE_ConfigureDevice(1);
}

void requestGetConfiguration()
{
	control_buffer[0] = 1;
	control.bufferlen = 1;
}

void EP0Complete()
{
	printf("Complete\n");
	if ((control.setup.bmRequestType & 0x7C) == 0)
	{
	}
	else
	{
		DFU_transferComplete(&control);
	}
}

void EP0setup()
{
	printf("SETUP\n");

	int l;

	if ((l = usb_read_packet(EP0OUT, &control.setup, 8)) == 8)
	{
		control.complete = 0;
		control.buffer = control_buffer;
		control.bufferlen = control.setup.wLength;

		printf("bmRequestType: 0x%x\n", control.setup.bmRequestType);
		printf("bRequest     : 0x%x\n", control.setup.bRequest);
		printf("wValue       : 0x%x\n", control.setup.wValue);
		printf("wIndex       : 0x%x\n", control.setup.wIndex);
		printf("wLength      : 0x%x\n", control.setup.wLength);

		if ((control.setup.bmRequestType & 0x7C) == 0)
		{
			switch(control.setup.bRequest)
			{
				case REQ_GET_STATUS:
 					requestGetStatus();
					break;
				case REQ_CLEAR_FEATURE:
					break;
				case REQ_SET_FEATURE:
					break;
				case REQ_SET_ADDRESS:
					SIE_SetAddress(control.setup.wValue);
					printf("USB: Got USB Address %d\n", control.setup.wValue);
					usb_write_packet(EP0IN, NULL, 0);
					break;
				case REQ_GET_DESCRIPTOR:
					requestGetDescriptor();
					break;
				case REQ_SET_DESCRIPTOR:
					break;
				case REQ_GET_CONFIGURATION:
					requestGetConfiguration();
					break;
				case REQ_SET_CONFIGURATION:
					requestSetConfiguration();
					break;
				default:
					usb_ep0_stall();
					break;
			}
		}
		else
		{
			DFU_controlTransfer(&control);
		}
	}
// 	else
// 	{
// 		printf("Got a setup packet with size %d\n", l);
// 	}
}

void EP0in()
{
	printf("EP0IN %d (%d)\n", control.complete, control.bufferlen);
	if (control.complete == 0)
	{
		if (control.setup.bmRequestType_Data_Transfer_Direction == DATA_DIRECTION_DEVICE_TO_HOST)
		{
			if (control.bufferlen)
			{
				int l = control.bufferlen;
				if (l > sizeof(control_buffer))
					l = sizeof(control_buffer);
				l = usb_write_packet(EP0IN, control.buffer, l);
				printf("W:%d\n", l);
				control.bufferlen -= l;
				control.buffer += l;
				if (control.bufferlen == 0)
				{
					if (l == 64)
						control.zlp = 1;
				}
			}
			else if (control.zlp)
			{
				usb_write_packet(EP0IN, NULL, 0);
				control.zlp = 0;
				printf("sent ZLP\n");
			}
		}
		else if (control.bufferlen == 0)
		{
			printf("Sent ACK\n");
			usb_write_packet(EP0IN, NULL, 0);
			control.complete = 1;
			EP0Complete();
		}
	}
}

void EP0out()
{
	printf("EP0OUT %d (%d)\n", control.complete, control.bufferlen);
	if (control.complete == 0)
	{
		if (control.setup.bmRequestType_Data_Transfer_Direction == DATA_DIRECTION_HOST_TO_DEVICE)
		{
			if (control.bufferlen)
			{
				int l;
				l = usb_read_packet(EP0OUT, control.buffer, control.bufferlen);

				if (l <= control.bufferlen)
				{
					control.bufferlen -= l;
					control.buffer += l;
					return;
				}
			}
			else
			{
				int l = usb_read_packet(EP0OUT, NULL, 0);
				if (l == 0)
					return;
			}
		}
		else if (control.bufferlen == 0)
		{
			int l = usb_read_packet(EP0OUT, NULL, 0);
			if (l == 0)
			{
				control.complete = 1;
				EP0Complete();
				return;
			}
		}
		usb_ep0_stall();
		return;
	}
	usb_read_packet(EP0OUT, control_buffer, sizeof(control_buffer));
}

/* */
