/*
 * USBEHCIHostController.cc
 *
 *  Created on: 03.02.2013
 *      Author: dbaldin
 */

#include "USBEHCIHostController.hh"
#include "kernel/Kernel.hh"
#include "inc/memtools.hh"

extern Kernel* theOS;
extern Kernel_ThreadCfdCl* pCurrentRunningThread;

#define GETBITS(a,UP,LOW) ((a & (( (1 << (UP - LOW + 1)) -1) << LOW)) >> LOW)
#define SETBITS(a,UP,LOW,val) a = ((a & ~(( (1 << (UP - LOW + 1)) -1) << LOW)) | ((val & ((1 << (UP - LOW + 1)) -1)) << LOW) )


#define ALIGN(x,a)              __ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))

#define PORT_CONNECTION 	0
#define PORT_ENABLE 		1
#define PORT_SUSPEND 		2
#define PORT_OVER_CURRENT 	3
#define PORT_RESET 			4
#define PORT_POWER 			0x8
#define PORT_LOW_SPEED 		9
#define C_PORT_CONNECTION 	16
#define C_PORT_ENABLE 		17
#define C_PORT_SUSPEND 		18
#define C_PORT_OVER_CURRENT 19
#define C_PORT_RESET 		20
#define PORT_TEST 			21
#define PORT_INDICATOR 		22

										//value.low, value.high, index.low, index.high, len.low, len.high
#define USB_GETHUB_DESCRIPTOR()	 			{0xa0,0x06,0x0,0x0,0x0,0x0,0x40,0x0 }
#define USB_GETHUB_STATUS()	 				{0xa0,0x00,0x0,0x0,0x0,0x0,0x4,0x0 }
#define USB_GETPORT_STATUS()	 			{0xa3,0x0,0x0,0x0,0x0,0x0,0x4,0x0 }
#define USB_SETPORT_FEATURE(feature)	 	{0x23,0x03,feature,0x0,0x0,0x0,0x0,0x0 }
#define USB_CLEARPORT_FEATURE(feature)	 	{0x23,0x01,feature,0x0,0x0,0x0,0x0,0x0 }

#define DEVICE_DESCRIPTOR 0x1
#define CONFIGURATION_DESCRIPTOR 0x2
#define STRING_DESCRIPTOR 0x3
#define INTERFACE_DESCRIPTOR 0x4
#define ENDPOINT_DESCRIPTOR 0x5

#define USB_DESCRIPTOR_REQUEST(type)	 	{0x80,0x06,0x0,type,0x0,0x0,0x40,0x0 }
#define USB_SETADDRESS_REQUEST(addr) 		{0x00,0x05,addr,0x0,0x0,0x0,0x0 ,0x0 }
#define USB_SETCONFIGURATION_REQUEST(id) 	{0x00,0x09,id, 0x0 ,0x0,0x0,0x0 ,0x0 }
#define USB_SETINTERFACE_REQUEST(id) 		{0x01,0x0b,0x0,0x0 ,0x0,id,0x0 ,0x0 }
#define USB_GETDEVICE_STATUS() 				{0x80,0x00,0x0,0x0 ,0x0,0x0,0x2 ,0x0 }
#define USB_CLEAR_FEATURE(feature,endpoint)	{0x02,0x01,feature,0x0,endpoint,0x0,0x0,0x0 }

#define USB_SET_INDEX_FIELD(req,index) *((unint2*)(&req[4])) = htons(index)
#define USB_SET_VALUE_FIELD(req,value) *((unint2*)(&req[2])) = htons(value)


const char* bDeviceClassStr[16] = {
		"Device",
		"Audio",
		"Communications and CDC Control",
		"HID (Human Interface Device)",
		"Unknown",
		"Physical",
		"Image",
		"Printer",
		"Mass Storage",
		"Hub",
		"CDC-Data",
		"Smart Card",
		"Content Security",
		"Video",
		"Personal Healthcare",
		"Audio/Video Devices"
};

const char* bEndpointDirectionStr[2] = {
		"Out",
		"In"
};

const char* bmTransferTypeStr[4] = {
		"Control",
		"Isochronous",
		"Bulk",
		"Interrupt"
};


extern void kwait(int millseconds);

// The main QH we are enqueing to
volatile QH QHmain __attribute__((aligned(32))) ATTR_CACHE_INHIBIT;

#define FRAME_LIST_SIZE 256
unint4 framelist[FRAME_LIST_SIZE] __attribute__((aligned(0x1000))) ATTR_CACHE_INHIBIT;

static unint1 recv_buf[256] __attribute__((aligned(4))) ATTR_CACHE_INHIBIT;
volatile static qTD qtds[8] __attribute__((aligned(32))) ATTR_CACHE_INHIBIT;
static int ATTR_CACHE_INHIBIT qtdnum = 0 ;


USB_EHCI_Host_Controller::USB_EHCI_Host_Controller(unint4 ehci_dev_base) : GenericDeviceDriver(true,"EHCI_HC") {

	this->operational = false;
	this->async_qh_reg = 0;

	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller() creating HC at addr: %x",ehci_dev_base));

	hc_base = ehci_dev_base;

	// Initialize the periodic list
	for (int i = 0; i < FRAME_LIST_SIZE; i++) {
		framelist[i] = QT_NEXT_TERMINATE;
	}

	for (int i=0; i < 10; i++) {
		registered_devices[i] = 0;
	}

	memset((void*)(&QHmain),0,sizeof(QH));
	QHmain.qh_endpt1 = QH_ENDPT1_H(1) | QH_ENDPT1_I(1);
	QHmain.qh_curtd = QT_NEXT_TERMINATE;
	QHmain.qh_overlay.qt_next = QT_NEXT_TERMINATE;
	QHmain.qh_overlay.qt_altnext = QT_NEXT_TERMINATE;
	QH_LINK(&QHmain, &QHmain);

	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller() QHMain: 0x%x",&QHmain));

}

ErrorT USB_EHCI_Host_Controller::Init(unint4 priority) {


	// try read out the control register length field
	unint1 length = INB(hc_base + CAPLENGTH_OFFSET);

	this->operational_register_base = hc_base + length;
	this->async_qh_reg = this->operational_register_base + ASYNCLISTADDR_OFFSET;

	// try read out the ehci version
	unint1 version = INB(hc_base + HCIVERSION_OFFSET);
	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller() EHCI version of HC: %x",length));

	// read and analyze the hc structural paramteres
	unint4 hcsparams = INW(hc_base + HCSPARAMS_OFFSET);
	unint4 num_ports = GETBITS(hcsparams,3,0);

	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller() HC Structural Parameters:",version));
	LOG(ARCH,INFO,(ARCH,INFO,"    Supports Port Indicators: %d",GETBITS(hcsparams,16,16) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Companion Controllers   : %d",GETBITS(hcsparams,15,12) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Ports per CC            : %d",GETBITS(hcsparams,11,8) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Ports Routing           : %d",GETBITS(hcsparams,7,7) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Number of Ports         : %d",GETBITS(hcsparams,3,0) ));

	unint1 power_control = (unint1) (GETBITS(hcsparams,4,4));
	LOG(ARCH,INFO,(ARCH,INFO,"    Port Power Control      : %d",power_control ));


	// read and analyze the hc configuration parameters
	unint4 hccparams = INW(hc_base + HCCPARAMS_OFFSET);


	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller() HC Configuration Parameters:",version));
	LOG(ARCH,INFO,(ARCH,INFO,"    Extended Capabilities   : %x",GETBITS(hccparams,15,8) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Scheduling Threshold    : %d",GETBITS(hccparams,7,4) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Park Mode Support       : %d",GETBITS(hccparams,2,2) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Programmable Frame List : %x",GETBITS(hccparams,1,1) ));
	LOG(ARCH,INFO,(ARCH,INFO,"    Supports 64bit Address  : %x",GETBITS(hccparams,0,0) ));


	// setup the asynclist and periodic frame list
	unint4 usbcmd_val = INW(operational_register_base + USBCMD_OFFSET);


	if (GETBITS(usbcmd_val,0,0) == 1) {
		// we need to stop this hc
		LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() HC running .. stopping now:"));

		// set run/stop bit to 0
		SETBITS(usbcmd_val,0,0,0);

		OUTW(operational_register_base + USBCMD_OFFSET,usbcmd_val);

		// wait until stopped, upto 16 microframes = 2 ms
		volatile unint4 timeout = 1000;
		while (((INW(operational_register_base + USBSTS_OFFSET) & (1 << 12)) == 0) && timeout) {timeout--; kwait_us(2);}
		if (timeout == 0) {
			LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller::Init() Timeout on stopping HC.."));
			return (cError);
		}
	}

	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() resetting HC.."));

	usbcmd_val = 0;
	SETBITS(usbcmd_val,1,1,1);  // start hcreset
	SETBITS(usbcmd_val,4,4,0);  // stop periodic schedule
	SETBITS(usbcmd_val,5,5,0);  // stop asynch schedule
	SETBITS(usbcmd_val,6,6,0);  // stop asynch schedule

	OUTW(operational_register_base + USBCMD_OFFSET,usbcmd_val);

	// wait for reset
	volatile unint4 timeout = 100000;
	while ((INW(operational_register_base + USBCMD_OFFSET) & (1<<1)) && timeout) {timeout--;}
	if (timeout == 0) {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller::Init() Timeout on resetting HC.."));
		return (cError);
	}

	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() reset successful.."));

	/* register ourself at the IRQ Manager */
	theOS->getInterruptManager()->registerIRQ(EHCI_IRQ,this,priority);

	// now initialize the aperiodic and periodic frame space
	// set all frame list entries to invalid
	// initialize the working queues
	OUTW(operational_register_base + PERIODICLISTBASE_OFFSET,(unint4) &framelist[0]);

	OUTW(operational_register_base + ASYNCLISTADDR_OFFSET, (unint4) &QHmain);

	// disableinterrupts
	OUTW(operational_register_base + USBINTR_OFFSET,0x0);

	// start the HC + enable asynchronous park mode so we can transfer multiple packets per frame
	OUTW(operational_register_base + USBCMD_OFFSET,0x080b09); // 256 frame list elements
	//OUTW(operational_register_base + USBCMD_OFFSET,0x080b01); // 1024 frame list elements

	OUTW(operational_register_base + USBSTS_OFFSET, 0x3f);

	// route everything to us!
	OUTW(operational_register_base + CONFIGFLAG_OFFSET,1);


	// Initalize power of each port!
	if (power_control) {
		LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() Setting Ports power to ON"));

		for (unint1 i = 0; i<num_ports; i++ )  {
			unint4 portsc = INW(operational_register_base + PORTSC1_OFFSET + 4*i);
			LOG(ARCH,TRACE,(ARCH,TRACE,"USB_EHCI_Host_Controller::Init() Port %d Status: %x", i+1, portsc));

			// we need to power on the ports.. otherwise bus stays powered down!
			OUTW(operational_register_base + PORTSC1_OFFSET + 4*i,portsc | 1 <<12 );
		}
	}


	// give ports a chance to power on
	timeout = 20;
	while (timeout) {timeout--; kwait(1);}

	unint4 usb_cmd = INW(operational_register_base + USBCMD_OFFSET);
	SETBITS(usb_cmd,5,5,1);
	OUTW(operational_register_base + USBCMD_OFFSET, usb_cmd);

	// wait for ports to get connected..
	// register those connected as we can communicate over them!
	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() Scanning for devices.."));

	for (unint1 i = 0; i<num_ports; i++ )  {
		ports[i].status = 0;

		unint4 portsc = INW(operational_register_base + PORTSC1_OFFSET + 4*i);

		LOG(ARCH,TRACE,(ARCH,TRACE,"USB_EHCI_Host_Controller::Init() Port %d Status: %x", i+1, portsc));
		unint1 line_status = (unint1) (GETBITS(portsc,11,10));
		unint1 connect = GETBITS(portsc,0,0);

		LOG(ARCH,TRACE,(ARCH,TRACE,"USB_EHCI_Host_Controller::Init()   line_status : %x", line_status));
		LOG(ARCH,TRACE,(ARCH,TRACE,"USB_EHCI_Host_Controller::Init()       connect : %x", connect));

		if (connect == 1) {
			// device connected to port!
			// check for high speed device to perform reset!
			if (line_status != 1) {
				// perform reset!
				SETBITS(portsc,8,8,1); // start port reset
				SETBITS(portsc,1,1,1); // clear connect status change
				OUTW(operational_register_base + PORTSC1_OFFSET + 4*i,portsc);

				// ensure bit is set long enough to perform reset
				//volatile unint4 timeout;
				//timeout = 10000;
				//while (timeout) {timeout--;}
				kwait(2);

				// set port reset bit to 0 and wait for port enable!
				SETBITS(portsc,8,8,0); // start port reset
				OUTW(operational_register_base + PORTSC1_OFFSET + 4*i,portsc);

				// wait for port to be enabled!
				// must happen within 2 ms!

				portsc = INW(operational_register_base + PORTSC1_OFFSET + 4*i);

				while (! (portsc & (1<<2))) {
					portsc = INW(operational_register_base + PORTSC1_OFFSET + 4*i);
				}

				LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() [PORT %d] HS-Device (480 Mbit/s) enabled..", i));
				ports[i].status = portsc;
				ports[i].root_device = new USBDevice(this,0,i,2);

				OUTW(operational_register_base + USBSTS_OFFSET, 0x4);
				// wait for status to settle
				kwait(1);
				// try to enumerate the port
				// this initializes all devices
				enumerateDevice(ports[i].root_device);

			} else {
				LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller::Init() [PORT %d] LS-Device detected. Releasing ownership.", i));

				SETBITS(portsc,13,13,1); // release port ownership
				OUTW(operational_register_base + PORTSC1_OFFSET + 4*i,portsc);
			}

		} // port connected
	} // for all ports

	// clear the port status changed bit
	OUTW(operational_register_base + USBSTS_OFFSET,1 << 2);

	// wait for some time to ensure reset recovery  (USB 2.0 spec, 7.1.7.3, TRSTRCY)
	timeout = 10000;
	while (timeout) {timeout--;}

	// if all succeeded we are operational
	this->operational = true;
	return (cOk);
}




int USB_EHCI_Host_Controller::USBBulkMsg(USBDevice *dev, unint1 endpoint, unint1 direction, unint2 data_len, unint1 *data) {

	// finally link the queue head to the qtd chain
	volatile QH* qh = (QH*) dev->endpoints[endpoint].queue_head;
	if (qh == 0) return (cError);

	this->accessControl->acquire();

	// get some memory for first qtd and last qtd
	volatile qTD* qtd  		=  &qtds[(qtdnum++) & 0x7];
	volatile qTD* qtd_last;

	// maximum packet size 4096 .. we could go up to 5*4096
	unint4 length = 4096;
	if (data_len < length) length = data_len;

	unint1 dir = QT_TOKEN_PID_IN;
	if (direction == USB_DIR_OUT) dir = QT_TOKEN_PID_OUT;


	unint1 toggle = dev->endpoints[endpoint].data_toggle;
	unint4 qtd_data_len = length;

	// FIRST PACKET
	qtd->qt_next 		= QT_NEXT_TERMINATE;
	qtd->qt_altnext 	= QT_NEXT_TERMINATE;
	qtd->qt_token 		= QT_TOKEN_DT(toggle) | QT_TOKEN_CERR(3) | QT_TOKEN_IOC(0) | QT_TOKEN_PID(dir)
				 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(qtd_data_len);
	qtd->qt_buffer[0] 	= (unint4) data; // set data to send
	qtd->qt_buffer_hi[0]= 0x0;
	qtd->qt_buffer[1] 	= (unint4) alignCeil((char*)data,4096); // align next part of the data
	qtd->qt_buffer_hi[1]= 0x0;

	qtd_last = qtd;

#if USB_BULK_MULTIPLE_QTDS
	data += qtd_data_len;
	length -= qtd_data_len;
	unint4 num_qtds = 1;

	// create a chain of qtds for the data to be transferred

	while (length > 0) {
		volatile qTD *qtd2 = &qtds[num_qtds]; //(qTD*) theOS->getMemManager()->alloc(32,1,32);
		num_qtds++;

		toggle = toggle ^ 1;

		qtd_data_len = length;

		qtd2->qt_next		= QT_NEXT_TERMINATE;
		qtd2->qt_altnext 	= QT_NEXT_TERMINATE;
		qtd2->qt_token 		= QT_TOKEN_DT(toggle) | QT_TOKEN_CERR(3) | QT_TOKEN_IOC(0) | QT_TOKEN_PID(dir)
					 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(qtd_data_len);
		qtd2->qt_buffer[0] 	= (unint4) data; // set data to send
		qtd2->qt_buffer[0] 	= (unint4) alignCeil((char*)data,4096);  // align next part of the data

		QT_LINK(qtd_last,qtd2);

		qtd_last = qtd2;

		data += qtd_data_len;
		length -= qtd_data_len;
	}
#endif

	OUTW(operational_register_base + USBSTS_OFFSET, 0x3a);

	qh->qh_curtd = 0;
	qh->qh_overlay.qt_token = 0;
	qh->qh_overlay.qt_altnext = QT_NEXT_TERMINATE;

	// execute!
	// set next qtd in qh overlay to activate transfer
	qh->qh_overlay.qt_next = (unint4) qtd;

	QH_LINK(qh,&QHmain);
	QH_LINK(&QHmain,qh);

	// wait for completion of usb transaction
	volatile unint4 timeout = 2000;
	while (( QT_TOKEN_GET_STATUS(qtd_last->qt_token) == 0x80 ) && timeout) {timeout--;  kwait_us(1);};

	// get number of bytes transferred
	unint4 num = 0;
	if (data_len > 0) num = data_len - QT_TOKEN_GET_TOTALBYTES(qtd->qt_token);

	// stop execution of this queue head
	if (timeout == 0 || ( (QT_TOKEN_GET_STATUS(qtd_last->qt_token) & 0x7c )!= 0x0))  {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller::send() error on bulk packet.."));
		LOG(ARCH,ERROR,(ARCH,ERROR,"timeout: %d",timeout));
		LOG(ARCH,ERROR,(ARCH,ERROR,"USBSTS: %x",INW(operational_register_base + USBSTS_OFFSET)));
		LOG(ARCH,ERROR,(ARCH,ERROR,"qtd     \t(%08x)\tstatus: %x", (unint4) qtd,qtd->qt_token));
		LOG(ARCH,ERROR,(ARCH,ERROR,"qtd_last\t(%08x)\tstatus: %x", (unint4) qtd_last,qtd_last->qt_token));
		LOG(ARCH,ERROR,(ARCH,ERROR,"AsyncListAddr: 0x%x",INW(operational_register_base + ASYNCLISTADDR_OFFSET)));
		LOG(ARCH,ERROR,(ARCH,ERROR,"qH: 0x%x",dev->endpoints[endpoint].queue_head));

		memdump((unint4) qtd,5);
		// debug the asynchronous list
		memdump((unint4) qh,8);
		//return as we can not use this port

		// try to unstall the endpoint
		num = -1;
		dev->endpoints[endpoint].data_toggle = 1;

		//this->accessControl->release();

		//unint1 msg[8] = USB_CLEAR_FEATURE(0x0,endpoint);
		//this->sendUSBControlMsg(dev,0x0,(unint1*) &msg);


	}

	// unlink data transfer
	QH_LINK(&QHmain,&QHmain);
	qh->qh_overlay.qt_next = 0xdead0000;
	// on success toggle dt bit
	dev->endpoints[endpoint].data_toggle = toggle ^ 1;

	this->accessControl->release();

	return (num);	// return number of bytes transferred

}

unint1 controlMsg[8] __attribute__((aligned(32))) ATTR_CACHE_INHIBIT;

int USB_EHCI_Host_Controller::sendUSBControlMsg(USBDevice *dev, unint1 endpoint, unint1 *control_msg,unint1 direction, unint1 data_len, unint1 *data) {

	// finally link the queue head to the qtd chain
	volatile QH* qh = (QH*) dev->endpoints[endpoint].queue_head;
	if (qh == 0) return (cError);

	this->accessControl->acquire();

	// be sure control message is in cache inhibit memory for EHCI to access it.
	memcpy(controlMsg,control_msg,8);

	// get some memory for qtds
	volatile qTD* qtd  = &qtds[(qtdnum++) & 0x7];
	volatile qTD* qtd2 = &qtds[(qtdnum++) & 0x7];
	volatile qTD* qtd3 = &qtds[(qtdnum++) & 0x7];
	volatile qTD* lastqtd = qtd2;

	// SETUP TOKEN
	QT_LINK(qtd,qtd2);

	// SETUP STAGE
	qtd->qt_altnext 	= QT_NEXT_TERMINATE;
	qtd->qt_token 		= QT_TOKEN_DT(0) | QT_TOKEN_CERR(3) | QT_TOKEN_IOC(0) | QT_TOKEN_PID(QT_TOKEN_PID_SETUP)
				 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(8);
	qtd->qt_buffer[0] 	= (unint4) controlMsg; // set control message to send
	qtd->qt_buffer[1] 	= (unint4) alignCeil((char*)controlMsg,4096); // set control message to send

	unint1 dir = QT_TOKEN_PID_IN;
	if (direction == USB_DIR_OUT) dir = QT_TOKEN_PID_OUT;

	// IN Packet
	qtd2->qt_next 		= QT_NEXT_TERMINATE;
	qtd2->qt_altnext	= QT_NEXT_TERMINATE;
	qtd2->qt_token 		= QT_TOKEN_DT(1) | QT_TOKEN_CERR(3) | QT_TOKEN_IOC(0) | QT_TOKEN_PID(dir)
				 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(data_len);
	qtd2->qt_buffer[0] 	= (unint4) data; // set data buffer
	qtd2->qt_buffer[1] 	= (unint4) alignCeil((char*)data,4096); // set control message to send

	if (data_len > 0 && direction == USB_DIR_IN) {
		QT_LINK(qtd2,qtd3);
		qtd3->qt_next 		= QT_NEXT_TERMINATE;
		qtd3->qt_altnext	= QT_NEXT_TERMINATE;
		qtd3->qt_token 		= QT_TOKEN_DT(1) | QT_TOKEN_CERR(3) | QT_TOKEN_IOC(0) | QT_TOKEN_PID(QT_TOKEN_PID_OUT)
					 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(0);
		lastqtd = qtd3;
	}

	// clear status bits
	OUTW(operational_register_base + USBSTS_OFFSET, 0x3a);

	qh->qh_curtd = 0;
	qh->qh_overlay.qt_token = 0;
	qh->qh_overlay.qt_altnext = QT_NEXT_TERMINATE;

	// execute!
	// set next qtd in qh overlay to activate transfer
	qh->qh_overlay.qt_next = (unint4) qtd;

	QH_LINK(qh,&QHmain);
	QH_LINK(&QHmain,qh);

	volatile unint4 timeout = 400;
	while (( QT_TOKEN_GET_STATUS(INW(&lastqtd->qt_token)) == 0x80  ) && timeout) {
		timeout--;
		/* control messages are much slower */
		kwait(1);
	};

	// get number of bytes transferred
	unint4 num = 0;
	if (data_len > 0) num = data_len - QT_TOKEN_GET_TOTALBYTES(qtd2->qt_token);

	// stop execution of this queue head
	if (timeout == 0 || ( QT_TOKEN_GET_STATUS(lastqtd->qt_token) != 0x0))  {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller::send() error on control packet.."));
		LOG(ARCH,ERROR,(ARCH,ERROR,"timeout: %d",timeout));
		LOG(ARCH,ERROR,(ARCH,ERROR,"lastqtd \t %08x",(unint4)lastqtd));
		LOG(ARCH,ERROR,(ARCH,ERROR,"qtd1    \t(%08x)\tstatus: %x", (unint4) qtd,qtd->qt_token));
		LOG(ARCH,ERROR,(ARCH,ERROR,"qtd2    \t(%08x)\tstatus: %x", (unint4) qtd2,qtd2->qt_token));
		LOG(ARCH,ERROR,(ARCH,ERROR,"qtd3    \t(%08x)\tstatus: %x", (unint4) qtd3,qtd3->qt_token));
		LOG(ARCH,ERROR,(ARCH,ERROR,"AsyncListAddr: 0x%x",INW(operational_register_base + ASYNCLISTADDR_OFFSET)));
		memdump((unint4) qh,8);
		//return as we can not use this port
		num = -1;
	}

	QH_LINK(&QHmain,&QHmain);
	// invalidate qtd transfer anyway
	qh->qh_overlay.qt_next = 0xdead0000;

	this->accessControl->release();

	return (num);
}


ErrorT USB_EHCI_Host_Controller::enumerateDevice(USBDevice *dev) {


	unint1 msg[8] = USB_DESCRIPTOR_REQUEST(DEVICE_DESCRIPTOR);
	int4 ret_len = this->sendUSBControlMsg(dev,0,(unint1*) &msg, USB_DIR_IN, 0x40,(unint1*) &recv_buf);

	if (ret_len >= 0) {
		unint1 bdevLength = recv_buf[0];
		LOG(ARCH,TRACE,(ARCH,TRACE,"USB_EHCI_Host_Controller: Device Descriptor Length %d:",bdevLength));

		if (bdevLength > 0 && bdevLength <= sizeof(DeviceDescriptor)) {
			/* copy received device descriptor so we can reference it later on */
			memcpy(&dev->dev_descr,&recv_buf,bdevLength);
			/* Analyze device descriptor */

			LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller: Device at Port %d:",dev->port));
			LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller:  USB %d.%d Device",dev->dev_descr.v_usbhigh,dev->dev_descr.v_usblow));
			if (dev->dev_descr.bDeviceClass > 15) dev->dev_descr.bDeviceClass = 4;
			LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller:  Class: %x (%s)",dev->dev_descr.bDeviceClass, bDeviceClassStr[dev->dev_descr.bDeviceClass] ));
			LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller:  SubClass: %x Proto: %x",dev->dev_descr.bDeviceSubClass,dev->dev_descr.bDeviceProtocol));
			LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller:  Vendor: %4x - Product: %4x",dev->dev_descr.idVendor,dev->dev_descr.idProduct));

		} else {
			return (cError);
		}
	} else {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Getting Device Descriptor failed."));
		return (cError);
		// failed .. handle error
	}

	kwait(1);

	/* reset the port again
	   some device only react on the following messages after resetting the port two times */
	if (dev->parent == 0) {
		unint4 portsc = INW(operational_register_base + PORTSC1_OFFSET + 4* dev->port);
		SETBITS(portsc,8,8,1); // start port reset
		OUTW(operational_register_base + PORTSC1_OFFSET + 4* dev->port,portsc);

		/* give some time to the device to change its address */
		kwait(5);

		portsc = INW(operational_register_base + PORTSC1_OFFSET + 4*dev->port);
		SETBITS(portsc,8,8,0); // start port reset
		OUTW(operational_register_base + PORTSC1_OFFSET + 4*dev->port,portsc);

		unint4 timeout = 400;
		while ((!(INW(operational_register_base + PORTSC1_OFFSET + 4*dev->port) & (1 << 2))  ) && timeout) {
			timeout--;
			kwait_us(10);
		};

		if (timeout == 0) return (cError);

	} else {
		// parent must be a hub
		// TODO: send a port reset feature request
	}

	unint1 next_device_addr = (unint1) ((unint4)USBDevice::freeDeviceIDs->removeHead());
	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller: Setting Device Address: %d",next_device_addr));

	unint4 cap_token = INW(dev->endpoints[0].queue_head+4);
	SETBITS(cap_token,26,16,dev->dev_descr.bMaxPacketSize);
	OUTW(dev->endpoints[0].queue_head+4,cap_token);

	dev->endpoints[0].interrupt_receive_size = dev->dev_descr.bMaxPacketSize;

	// set the device address!
	int4 error = dev->setAddress(next_device_addr);
	if (error != cOk) {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Setting Device Address failed"));
		return (cError);
	}

	// give some time to the device to change its address
	kwait(10);

	memset(&recv_buf,0,0x40);
	unint1 msg3[8] = USB_DESCRIPTOR_REQUEST(CONFIGURATION_DESCRIPTOR);
	error = this->sendUSBControlMsg(dev,0,(unint1*) &msg3,USB_DIR_IN,0x40,(unint1*) &recv_buf);
	if (error < 0) return (cError);

	ConfigurationDescriptor* cConf = (ConfigurationDescriptor*) &recv_buf;
	unint4 total_length = cConf->wtotalLength;

	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller: Configuration %d [%x]:",cConf->bConfigurationValue, cConf->bmAttributes));
	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller:   NumInterfaces: %d maxPower: %d mA",cConf->bNumInterfaces, cConf->bMaxPower*2));
	InterfaceDescriptor* iDescr = (InterfaceDescriptor*) (cConf+1);

	unint4 bytes_read = sizeof(ConfigurationDescriptor);

	// store interface descriptor for driver
	memcpy(&dev->if_descr,iDescr,sizeof(InterfaceDescriptor));

	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller: Interface %d :",iDescr->bInterfaceNumber));
	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller:  NumEndpoints: %d", iDescr->bNumEndpoints));
	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller:  Class: %s", bDeviceClassStr[iDescr->bInterfaceClass]));
	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller:  Subclass: %x Protocol: %x", iDescr->bInterfaceSubClass, iDescr->bInterfaceProtocol));

	bytes_read += iDescr->bLength;

	EndpointDescriptor *descr = (EndpointDescriptor*) (iDescr +1);

	// some devices place some custom descriptors in between? stupid devices
	while (descr->btype != 5 && bytes_read < total_length) {
		LOG(ARCH,WARN,(ARCH,WARN,"USB_EHCI_Host_Controller: Ignoring unknown descriptor type: %x ",descr->btype));
		bytes_read += descr->bLength;
		descr = (EndpointDescriptor*) (((unint4)descr) + descr->bLength);
	}

	dev->numEndpoints = iDescr->bNumEndpoints;

	// get all endpoints
	for (int i = 0; i < iDescr->bNumEndpoints; i++ ) {

		// saftey check.. some usb devices might not send enough data ..
		if ( bytes_read > total_length) {
			LOG(ARCH,WARN,(ARCH,WARN,"USB_EHCI_Host_Controller: break: bytes_read: %d, total: %d ",bytes_read, total_length));
			break;
		}

		memcpy(&dev->endpoints[i+1].descriptor, descr, sizeof(EndpointDescriptor));

		if (descr->btype != 0x5) {
			LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Endpoint Descriptor failure.. bType != 0x5: %x",i+1,descr->btype));
			memdump((unint4) &dev->endpoints[i+1].descriptor,3);
			return (cError);
		}

		// set transferdirection
		if (((descr->bEndpointAddress & 0x80) >> 7) == 0) dev->endpoints[i+1].direction = Out;
		else dev->endpoints[i+1].direction = In;

		if (descr->bLength > 5)
			dev->endpoints[i+1].interrupt_receive_size = descr->wMaxPacketSize;

		dev->endpoints[i+1].address			= descr->bEndpointAddress & 0xf;

		LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller: Endpoint Adress: %d, Dir: %d",dev->endpoints[i+1].address,dev->endpoints[i+1].direction));

		//if (dev->endpoints[i+1].max_packet_size  < 0x40) dev->endpoints[i+1].max_packet_size  = 40;

		switch (descr->bmAttributes & 0x3) {
			case 0: dev->endpoints[i+1].type = Control; break;
			case 1: dev->endpoints[i+1].type = Isochronous; break;
			case 2: dev->endpoints[i+1].type = Bulk; break;
			case 3: {
				dev->endpoints[i+1].type = Interrupt;
				dev->endpoints[i+1].poll_frequency =  descr->bInterval;
				break;
			}
		}

		bytes_read += descr->bLength;
		descr = (EndpointDescriptor*) (((unint4)descr) + descr->bLength);
	}

	// Done Enumeration. Now set the configuration

	unint1 msg4[8] = USB_SETCONFIGURATION_REQUEST(1);
	error = this->sendUSBControlMsg(dev,0,(unint1*) &msg4);

	if (error < 0) {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Setting Configuration failed.."));
		return (cError);
	}

	// Setting the interface is not supported by all devices
	// As we are always using the first default interface we omit
	// setting this explicitly
	/*
	unint1 msg8[8] = USB_SETINTERFACE_REQUEST(iDescr->bInterfaceNumber);
	error = this->sendUSBControlMsg(dev,0,(unint1*) &msg8);

	if (error < 0) {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Setting Interface failed.."));
		return cError;
	}


	memset(&recv_buf,0,0x40);
	unint1 msg5[8] = USB_GETDEVICE_STATUS();
	error = this->sendUSBControlMsg(dev,0,(unint1*) &msg5,USB_DIR_IN,8,(unint1*) &recv_buf);
	*/


	// if this device is a hub
	if (dev->dev_descr.bDeviceClass == 0x9) {
			// set the configuration directly and perform a hub enumeration
			dev->endpoints[1].type = Interrupt;
			// overwrite poll frequency.. we are polling hubs less often
			dev->endpoints[1].poll_frequency = 27;  // 27 ms
			dev->activateEndpoint(1);	// activate it.. will generate interrupt queue heads

			// be sure periodic list is activated
			unint4 usb_cmd = INW(operational_register_base + USBCMD_OFFSET);
			SETBITS(usb_cmd,4,4,1);
			OUTW(operational_register_base + USBCMD_OFFSET, usb_cmd);

			LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller: Enumerating Hub.."));
			// create new DeviceDriver for this hub
			USBHub *hub = new USBHub(dev,this);
			dev->driver = hub;
			// register it as it can produce interrupts
			registerDevice(hub);
			// enumerate it
			hub->enumerate();

	} else {
		// load the device driver if existend
		LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller: Trying to load driver for new device"));

		// driver needs to activate the endpoints and do the device specific stuff
		// like registering at the OS as a char/comm or whatever device

		// lets try to find a driver for this usbdevice
		USBDeviceDriverFactory* driverfactory = theOS->getUSBDriverLibrary()->getDriverFor(dev);

		if (driverfactory != 0) {
			// get a driver instance and initialize it
			USBDeviceDriver *driver = driverfactory->getInstance(dev);
			// keep track of driver so we can delete it on device detach
			dev->driver = driver;
			registerDevice(driver);
			driver->initialize();
		} else {
			LOG(ARCH,WARN,(ARCH,WARN,"USB_EHCI_Host_Controller: No driver found for device..."));
			return (cError);
		}

		return (cOk);
	}

	// enable usb interrupts

	OUTW(operational_register_base + USBSTS_OFFSET, 0x3a);
	OUTW(operational_register_base + USBINTR_OFFSET, 0x1);

	return (cOk);
}



void USB_EHCI_Host_Controller::insertPeriodicQH(QH* qh, int poll_rate) {

	qh->qh_link = QH_LINK_TERMINATE;

	for (int i = poll_rate; i < FRAME_LIST_SIZE; i+= poll_rate) {

		if (framelist[i] != QH_LINK_TERMINATE) {
			// insert into list close behind this element
			// we keep at maximum one element per periodic entry and do
			// not create a list as we would have to create lots of
			// additional queue heads and track them
			for (int j = 1; j < poll_rate; j++) {
				if (framelist[i+j] != QH_LINK_TERMINATE) {
					framelist[i+j] = (unint4) qh | QH_LINK_TYPE_QH;
					break;
				}
			}

		} else {
			framelist[i] = (unint4) qh | QH_LINK_TYPE_QH;

		}
	}
}

void USB_EHCI_Host_Controller::removefromPeriodic(QH* qh, int poll_rate) {

	for (int i = poll_rate; i < FRAME_LIST_SIZE; i++) {

		if (framelist[i] != QH_LINK_TERMINATE) {

			QH* current_qh = (QH*) (framelist[i] & ~0x1f);

			if (current_qh == qh) {
				// remove this qh reference here
				framelist[i] = QH_LINK_TERMINATE;
			}

		}
	}

}


USB_EHCI_Host_Controller::~USB_EHCI_Host_Controller() {
	// deletion not supported
}


void USB_EHCI_Host_Controller::registerDevice(USBDeviceDriver* drv) {
	if (drv == 0) return;

	for (int i=0; i < 10; i++) {
		if (registered_devices[i] == 0) {
			registered_devices[i] = drv;
			return;
		}
	}
}

void USB_EHCI_Host_Controller::unregisterDevice(USBDeviceDriver* drv) {
	if (drv == 0) return;

	for (int i=0; i < 10; i++) {
		if (registered_devices[i] == drv) {
			registered_devices[i] = 0;
			return;
		}
	}
}


ErrorT USB_EHCI_Host_Controller::handleIRQ() {

	LOG(ARCH,DEBUG,(ARCH,DEBUG,"EHCI Interrupt"));

	//printf("USB INT_STS: %x\r",INW(operational_register_base + USBSTS_OFFSET));
	//printf("USB USBCMD: %x\r",INW(operational_register_base + USBCMD_OFFSET));
	//printf("USB USBINTR: %x\r",INW(operational_register_base + USBINTR_OFFSET));
//	OUTW(operational_register_base + USBSTS_OFFSET, 0x3f);

	clearIRQ();

	for (int i=0; i < 10; i++) {
		if (registered_devices[i] != 0) {
			registered_devices[i]->handleInterrupt();
		}
	}

	if (INW(operational_register_base + USBSTS_OFFSET) & 0x1)
		interruptPending = true;
	else
		interruptPending = false;

	return (cOk);
}

ErrorT USB_EHCI_Host_Controller::enableIRQ() {
	OUTW(operational_register_base + USBINTR_OFFSET, 0x1);
	return (cOk);
}

ErrorT USB_EHCI_Host_Controller::disableIRQ() {
	OUTW(operational_register_base + USBINTR_OFFSET, 0x0);
	return (cOk);
}

ErrorT USB_EHCI_Host_Controller::clearIRQ() {
	OUTW(operational_register_base + USBSTS_OFFSET, 0x3f);
	return (cOk);
}

/**********************************************************************
 *
 *					USB HUB Implementation
 *
 **********************************************************************/

/*!
 * Hub Enumeration Method. Starts the Enumeration Process on a newly connected
 * Hub. State changes are check (20 ms delay) and devices activated if found.
 */
ErrorT USBHub::enumerate() {

	// be sure we are comunicating to a hub.
	if (dev->dev_descr.bDeviceClass != 0x9) return (cError);

	// first get the hub descriptor to see how many ports we need to activate and check for connected device
	memset(&recv_buf,0,0x40);

	unint1 msg[8] = USB_GETHUB_DESCRIPTOR();
	int4 error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg,USB_DIR_IN,0x40,(unint1*) &recv_buf);
	if (error < 0) {
		return (cError);
	}

	memcpy(&this->hub_descr,&recv_buf,sizeof(HubDescriptor));

	LOG(ARCH,INFO,(ARCH,INFO,"USB_EHCI_Host_Controller: Hub Ports: %d, Characteristics: %x ",hub_descr.bnrPorts,hub_descr.wHubCharacteristics));

	LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_EHCI_Host_Controller: Powering Ports"));
	unint1 port_power_control = (hub_descr.wHubCharacteristics & 0x3);

	// power on ports..
	for (unint1 i= 1; i <= hub_descr.bnrPorts; i++) {
		// individual power control... power all ports
		unint1 msg2[8] = USB_SETPORT_FEATURE(PORT_POWER);
		msg2[4] = i;
		error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg2,USB_DIR_IN,0,(unint1*) &recv_buf);
		if (error < 0) {
			LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Hub Port %d PowerOn failed.. ",i));
		} else {
			// we may stop if port power is ganged as all other ports are now also powered
			if (port_power_control == 0) break;
		}
	}

	OUTW(controller->operational_register_base + USBSTS_OFFSET, 0x3f);

	// activate interrupts
	dev->activateEndpoint(1);

	// try to directly enumerate the hub as it may already have connected devices
	// this is a performance optimization as we do not need to wait for the actual
	// interrupt to happen
	// give the interrupt request some time to occur
	volatile unint4 timeout = 50;
	while (timeout) {timeout--; kwait(1);};

	if (dev->endpoints[1].recv_buffer[0] != 0) {
		this->handleStatusChange();
		// activate interrupt transfer again so we detect further changes
		dev->activateEndpoint(1);
	}

	return (cOk);
}


static unint1 u_recv_buf[20] ATTR_CACHE_INHIBIT;
unint1 port_status[4] ATTR_CACHE_INHIBIT;

/*!
 * Generic Handler for Hub Port State Changes.
 *
 * Detects newly connected devices. Cleans up devices on disconnect.
 */
void USBHub::handleStatusChange() {
	ErrorT error;

	for (unint1 i = 1; i <= hub_descr.bnrPorts; i++) {
		if ((dev->endpoints[1].recv_buffer[0] &  (1 << i)) != 0)  {

			// get status of port
			unint1 msg4[8] = USB_GETPORT_STATUS();
			msg4[4] = i;
			port_status[0] = 0;
			port_status[1] = 0;
			port_status[2] = 0;
			port_status[3] = 0;

			error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg4,USB_DIR_IN,4,(unint1*) &port_status);

			// check reason of status change
			if (port_status[2] & 1) {

				LOG(ARCH,DEBUG,(ARCH,DEBUG,"USBHub: Port %d Connection Status changed..",i));

 			    // clear the status change bit of this port
				unint1 msg1[8] = USB_CLEARPORT_FEATURE(C_PORT_CONNECTION);
				msg1[4] = i;
				error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg1);
				if (error < 0) {
					LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Clearing Port Feature failed.. "));
					return;
				}

				kwait(2);

				if (port_status[0] & 1) {
					LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Device attached on port %d..",i));

					// device on this hub port!
					// reset port and enumerate the device
					unint1 msg3[8] = USB_SETPORT_FEATURE(PORT_RESET);
					msg3[4] = i;

					// wait until port is enabled
					error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg3);
					if (error < 0) {
						LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Resetting hub port %d failed.. ",i));
						return;
					}

					volatile unint1 enabled = 0;
					volatile int4 timeout = 100;

					while (enabled == 0) {
						msg4[4] = i;

						memset(&u_recv_buf,0,20);
						error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg4,USB_DIR_IN,20,(unint1*) &u_recv_buf);
						enabled = u_recv_buf[0] & (1 << 1);
						timeout--;

						if (timeout < 0) {
							LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Enabling port failed.. "));
							return;
						}
					}

					unint1 msg5[8] = USB_CLEARPORT_FEATURE(C_PORT_RESET);
					msg5[4] = i;
					error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg5);
					if (error < 0) {
						LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Clearing Port Feature failed.. "));
						return;
					}

					LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Port enabled.. Enumerating "));

					unint1 speed = 2;
					if (u_recv_buf[1] & ( 1 << 1)) {
						speed = 1;
						LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Low-Speed Device attached."));
					}
					else
					if (u_recv_buf[1] & ( 1 << 2)) {
						speed = 2;
						LOG(ARCH,INFO,(ARCH,INFO,"USBHub: High-Speed Device attached."));
					}
					else {
						speed = 0;
						LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Full-Speed Device attached."));
					}

					// check if there is a device present which is just resetting
					if (this->portDevices[i] != 0) {
						LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Deactivating Device.."));

						// did we create a device on this port?
						if (this->portDevices[i] != 0) {
							this->portDevices[i]->deactivate();

							// remove from registered devices list
							controller->unregisterDevice(portDevices[i]->driver);

							delete portDevices[i];
							this->portDevices[i] = 0;
						}
					}

					USBDevice *newdev 		= new USBDevice(controller,dev,i,speed);  // deletion 10 lines down
					this->portDevices[i]	= newdev;

					// try to enumerate the port
					// this initializes all devices
					controller->enumerateDevice(newdev);
				} else {
					LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Device detached..",i));


					if (this->portDevices[i] != 0) {
						LOG(ARCH,INFO,(ARCH,INFO,"USBHub: Deactivating Device.."));

						// did we create a device on this port?
						if (this->portDevices[i] != 0) {
							this->portDevices[i]->deactivate();

							// remove from registered devices list
							controller->unregisterDevice(portDevices[i]->driver);

							delete portDevices[i];
							this->portDevices[i] = 0;
						}
						return;
					}


				}
			} // PORT_CONNECTION BIT

			if (port_status[2] & (1 << 2)) {
				unint1 msg5[8] = USB_CLEARPORT_FEATURE(C_PORT_SUSPEND);
				msg5[4] = i;
				error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg5);
				if (error < 0) {
					LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Clearing Port Feature failed.. "));
					return;
				}

			} // PORT_SUSPEND

			if (port_status[2] & (1 << 4)) {

				unint1 msg5[8] = USB_CLEARPORT_FEATURE(C_PORT_RESET);
				msg5[4] = i;
				error = controller->sendUSBControlMsg(dev,0,(unint1*) &msg5);
				if (error < 0) {
					LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Clearing Port Feature failed.. "));
					return;
				}

			}
			if (port_status[2] & (1 << 3)) {
				// unknown port status change
				LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Port Overpower!.. "));

				// TODO: handle by clearing stall
				//while(1);
			}
			if (port_status[2] & (1 << 1)) {
				// unknown port status change
				LOG(ARCH,ERROR,(ARCH,ERROR,"USBHub: Port Disabled!.. "));

				// TODO: handle by clearing stall
				//while(1);
			}
		}
	}
}

USBHub::USBHub(USBDevice *p_dev, USB_EHCI_Host_Controller *p_controller) :  USBDeviceDriver() {
	this->dev = p_dev;
	this->controller = p_controller;
	for (int i = 0; i < 6; i++) {
		this->portDevices[i] = 0;
	}
}


USBHub::~USBHub() {

	/* delete all devices as they are no longer there */
	for (int i = 0; i < 6; i++) {
		if (this->portDevices[i] != 0) delete this->portDevices[i];
	}

}


/*!
 * Interrupt Handler for USB Hubs.
 */
ErrorT USBHub::handleInterrupt() {

	QH* qh 		= dev->endpoints[1].queue_head;

	if ((QT_TOKEN_GET_STATUS(qh->qh_overlay.qt_token) != 0x80)) {

		LOG(ARCH,DEBUG,(ARCH,DEBUG,"USB_Hub: Interrupt Request finished, status: %x",dev->endpoints[1].recv_buffer[0]));

		// get status of port
		this->handleStatusChange();
		dev->endpoints[1].recv_buffer[0] = 0;

		// reactivate the interrupt transfer so we get interrupted again
		dev->activateEndpoint(1);

		return (cOk);
	}

	return (cError);
}



/**********************************************************************
 *
 *					USB Device Implementation
 *
 **********************************************************************/

ArrayDatabase* USBDevice::freeDeviceIDs;

ErrorT USBDevice::setAddress(unint1 u_addr) {

	// got device descriptor ! set address
	this->addr = u_addr;

	unint1 msg2[8] = USB_SETADDRESS_REQUEST((unint1)u_addr);
	unint4 error = controller->sendUSBControlMsg(this,0,(unint1*) &msg2);

	// keep communicating with address 0 until we set the configuration and ask for the status!
	if (error < 0) {
		LOG(ARCH,ERROR,(ARCH,ERROR,"USB_EHCI_Host_Controller: Setting Device Address failed.."));
		return (cError);
	}

	// adapt the queue head of this device endpoints
	for (int i = 0; i< 4; i++) {
		if (endpoints[i].queue_head != 0) {
			SETBITS(endpoints[i].queue_head->qh_endpt1,6,0,this->addr);
		}
	}
	return (cOk);
}

ErrorT USBDevice::deactivate() {

	for (int i = 0; i <= this->numEndpoints; i++) {

		// remove the allocated queue heads
		if (endpoints[i].type == Interrupt) {
			LOG(ARCH,INFO,(ARCH,INFO,"USB_Device: Stopping Periodic Transfer for Endpoint %d.",i));

			this->controller->removefromPeriodic(endpoints[i].queue_head,endpoints[i].poll_frequency);
		}

		// delete its allocated receive buffer
		if (endpoints[i].recv_buffer != 0) delete endpoints[i].recv_buffer;
		if (endpoints[i].queue_head != 0) 	delete endpoints[i].queue_head;
		if (endpoints[i].q_int_transfer != 0) 	delete endpoints[i].q_int_transfer;

		// can only be removed if the associated device driver is removed from controller
		endpoints[i].queue_head = 0;
		endpoints[i].q_int_transfer = 0;


	}

	return (cOk);
}

ErrorT USBDevice::reactivateEp(int num) {
	// reactivation only plausible for interrupt transfers
	if (endpoints[num].type != Interrupt) return (cError);

	QH* qh = endpoints[num].queue_head;
	qTD* qtd2 = endpoints[num].q_int_transfer;

	if ((QT_TOKEN_GET_STATUS(qh->qh_overlay.qt_token) != 0x80)) {
		// set qtd back to active
		qtd2->qt_token = QT_TOKEN_DT(endpoints[num].data_toggle) | QT_TOKEN_CERR(0) | QT_TOKEN_IOC(1) | QT_TOKEN_PID(QT_TOKEN_PID_IN)
						 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(endpoints[num].interrupt_receive_size);

		qtd2->qt_buffer[0] = (unint4) endpoints[num].recv_buffer;
		qtd2->qt_buffer[1] = (unint4) alignCeil((char*) endpoints[num].recv_buffer,4096);

		qh->qh_curtd = QT_NEXT_TERMINATE;
		qh->qh_overlay.qt_token = 0;
		qh->qh_overlay.qt_altnext = QT_NEXT_TERMINATE;
		qh->qh_link = QT_NEXT_TERMINATE;
		qh->qh_overlay.qt_next = (unint4) qtd2;
		return (cOk);
	}

	return (cError);
}

ErrorT USBDevice::activateEndpoint(int num, QH* pqh, qTD* pqtd) {
	if (num > 4) return (cError);

	if (endpoints[num].queue_head != 0) return (reactivateEp(num));

	// create a queue head and insert it into the list
	// generate a queue head for this device inside the asynchronous list
	QH* queue_head_new = pqh;
	if (pqh == 0)
	  queue_head_new =  (QH*) theOS->getMemoryManager()->alloci(48,1,32);	// delete @ USBDevice::deactivate

	// store address and clear area
	endpoints[num].queue_head = queue_head_new;
	memset((void*) queue_head_new,0,48);

	// hs : QH_ENDPT1_EPS_HS
	queue_head_new->qh_endpt1 = QH_ENDPT1_DEVADDR(this->addr) | QH_ENDPT1_MAXPKTLEN(endpoints[num].descriptor.wMaxPacketSize)
								| QH_ENDPT1_EPS(this->speed) | QH_ENDPT1_ENDPT(endpoints[num].address) | QH_ENDPT1_DTC(QH_ENDPT1_DTC_DT_FROM_QTD);

	// allow 1 packet per microframe
	if (this->speed == 2)
		queue_head_new->qh_endpt2 = QH_ENDPT2_MULT(1);
	else {
		queue_head_new->qh_endpt2 = QH_ENDPT2_MULT(1) | QH_ENDPT2_HUBADDR(this->parent->addr) | QH_ENDPT2_PORTNUM(this->port);
		if ( num == 0)
			queue_head_new->qh_endpt1 |= QH_ENDPT1_C(1);
	}

	// deactivate qtd transfer
	queue_head_new->qh_overlay.qt_next = 0xdead0000;

	if (this->endpoints[num].type != Interrupt) {
		// let this new queue head be the head of the async list
		//queue_head_new->qh_endpt1 |= QH_ENDPT1_H(1);
		queue_head_new->qh_link = QH_LINK_TYPE_QH | ((unint4) queue_head_new);

	} else {
		// Interrupt Endpoint
		// generate one qtd for the polling transfer
		qTD* qtd  = pqtd;
		if (pqtd == 0)
			qtd  = (qTD*) theOS->getMemoryManager()->alloci(32,1,32);		// delete @ USBDevice::deactivate

		queue_head_new->qh_overlay.qt_next = (unint4) qtd;
		queue_head_new->qh_endpt2 |= num; // S-Mask
		queue_head_new->qh_link = QT_NEXT_TERMINATE;

		qtd->qt_next = QT_NEXT_TERMINATE;
		qtd->qt_altnext = QT_NEXT_TERMINATE;

		queue_head_new->qh_overlay.qt_token = 0;
		queue_head_new->qh_overlay.qt_altnext = QT_NEXT_TERMINATE;

		// get interrupt receive buffer
		endpoints[num].recv_buffer =  (unint1*) theOS->getMemoryManager()->alloci(endpoints[num].interrupt_receive_size,1,4); // delete @ USBDevice::deactivate
		memset(endpoints[num].recv_buffer,0,endpoints[num].interrupt_receive_size);

		qtd->qt_buffer[0] = (unint4) endpoints[num].recv_buffer;
		qtd->qt_buffer[1] = (unint4) alignCeil((char*) endpoints[num].recv_buffer,4096);

		// setup interrupt transfer qtd .. try to receive interrupt_receive_size bytes
		qtd->qt_token = QT_TOKEN_DT(1) | QT_TOKEN_CERR(0) | QT_TOKEN_IOC(1) | QT_TOKEN_PID(QT_TOKEN_PID_IN)
						 			 | QT_TOKEN_STATUS_ACTIVE | QT_TOKEN_TOTALBYTES(endpoints[num].interrupt_receive_size);

		endpoints[num].q_int_transfer = qtd;

		if (endpoints[num].poll_frequency <=1)
			endpoints[num].poll_frequency = (unint1) (22 + num + this->addr);

		// insert qh in periodic list
		controller->insertPeriodicQH(queue_head_new,endpoints[num].poll_frequency);

	}
	return (cOk);

}

USBDevice::USBDevice(USB_EHCI_Host_Controller *p_controller, USBDevice *p_parent, unint1 u_port, unint1 u_speed) {

	this->controller 	= p_controller;
	this->parent 		= p_parent;
	this->port 			= u_port;
	this->dev_priv 		= 0;
	this->numEndpoints	= 0;
	this->driver		= 0;
	this->speed 		= u_speed;

	// new devices need can only be access by control port 0 first
	// an address needs to be set after successfull configuration
	addr = 0;

	for (int i = 0; i< 4; i++) {
		endpoints[i].queue_head 	= 0;
		endpoints[i].q_int_transfer = 0;
		endpoints[i].data_toggle 	= 0;
		endpoints[i].recv_buffer 	= 0;
		endpoints[i].type 			= UnknownTT;
		endpoints[i].direction 		= UnknownDir;
		// intitialize with some valid values before we have received
		// the endpoint descriptor
		if (speed == 1) {
			endpoints[i].interrupt_receive_size = 8;
			endpoints[i].descriptor.wMaxPacketSize =  8;
		}
		else {
			endpoints[i].interrupt_receive_size = 64;
			endpoints[i].descriptor.wMaxPacketSize =  64;
		}

	}

	endpoints[0].type 		= Control;
	endpoints[0].direction 	= Both;
	endpoints[0].address	= 0;


	if (this->activateEndpoint(0) != cOk) {
		this->endpoints[0].queue_head = 0;
		LOG(ARCH,ERROR,(ARCH,ERROR,"USBDevice: Activating Controller Endpoint failed.."));
	}


}

USBDevice::~USBDevice() {
	if (driver != 0) delete driver;
	USBDevice::freeDeviceIDs->addTail((DatabaseItem*) (unint4) this->addr);
}
