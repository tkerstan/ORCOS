/*
 * AM335xGPIO.cc
 *
 *  Created on: 16.01.2015
 *    Copyright & Author: dbaldin
 */

#include "AM335xGPIO.hh"
#include "inc/memio.h"
#include "kernel/Kernel.hh"
#include "syscalls/syscalls.hh"

// Keep consistent with sys/gpio.h defines
#define IOCTL_GPIO_SET_DIR 0x0
#define IOCTL_GPIO_GET_DIR 0x1
#define IOCTL_GPIO_ENABLE_IRQ 0x2
#define IOCTL_GPIO_GET_IRQ_STATUS 0x3
#define IOCTL_GPIO_IRQ_RISING_EDGE 0x4
#define IOCTL_GPIO_IRQ_FALLING_EDGE 0x5

extern Kernel* theOS;


AM335xGPIO::AM335xGPIO(T_AM335xGPIO_Init *init) :
        CharacterDevice(true, init->Name) {
    this->baseAddress = init->Address;
    this->irqNum = init->INTC_IRQ;
    int revision = INW(this->baseAddress + AM335X_GPIO_REVISION);

    LOG(ARCH, INFO, "AM335xGPIO: creating '%s' [0x%x]. Rev: %u.%u, RTLv.%u",
        init->Name,
        init->Address,
        (revision >> 8) & 0x7,
        (revision >> 0) & 0x3f,
        (revision >> 11) & 0x1f);

    OUTW(this->baseAddress + AM335X_GPIO_SYSCONFIG, 1 << 1);
    if (TIMEOUT_WAIT(INW(this->baseAddress + AM335X_GPIO_SYSCONFIG) & (1 << 1), 1000)) {
        LOG(ARCH, INFO, "Timeout resetting AM335xGPIO");
        return;
    }

    /* register the interrupt handler for the GPIO... no need to use a thread here */
    theOS->getInterruptManager()->registerIRQ(irqNum, this, init->Priority, IRQ_NOTHREAD);

    irqStatus = 0;

    /* clear all irq enable bits */
    OUTW(this->baseAddress + AM335X_GPIO_IRQSTATUS_CLR_0, 0xffffffff);

    // set direction
    OUTW(this->baseAddress + AM335X_GPIO_OE, (unint4) init->DirectionBits);

    unint4 val = init->ValueBits;

    // set data out for all bits
    OUTW(this->baseAddress + AM335X_GPIO_DATA_OUT, val);
    // clear data out for all 0 bits
    //OUTW(this->baseAddress + AM335X_GPIO_CLEARDATAOUT, ~val);
}

AM335xGPIO::~AM335xGPIO() {
}


/*****************************************************************************
 * Method: AM335xGPIO::readBytes(char *bytes, unint4 &length)
 *
 * @description
 *  Reads the GPIO state. Length must be 4 bytes for the 32 bit GPIO
 * @params
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::readBytes(char* bytes, unint4& length) {
    if (length < 4)
        return (cError);
    // check alignment
    if ((((unint4) bytes) & 0x3) != 0) {
        return (cWrongAlignment);
    }

    OUTW(bytes, INW(this->baseAddress + AM335X_GPIO_DATA_IN));
    length = 4;
    return (cOk);
}

/*****************************************************************************
 * Method: AM335xGPIO::writeBytes(const char *bytes, unint4 length)
 *
 * @description
 *  Sets the GPIO output state. Length must be 4 bytes.
 * @params
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::writeBytes(const char* bytes, unint4 length) {
    if (length != 4)
        return (cError);
    // check alignment
    if ((((unint4) bytes) & 0x3) != 0) {
        return (cWrongAlignment);
    }

    unint4 val = *((const unint4*) bytes);

    // set data out for all bits
    OUTW(this->baseAddress + AM335X_GPIO_DATA_OUT, val);

    return (cOk);
}


/*****************************************************************************
 * Method: AM335xGPIO::handleIRQ()
 *
 * @description
 *  Handles IRQs generated by GPIO pins
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::handleIRQ(int irq) {
    /* Nothing to do here.
     * User Space thread may be doing something.
     */
    /* accumulate the irq bits until read by user thread */
    irqStatus |= INW(this->baseAddress + AM335X_GPIO_IRQSTATUS0);
    return (cOk);
}

/*****************************************************************************
 * Method: AM335xGPIO::enableIRQ()
 *
 * @description
 * enables the hardware interrupts of this device
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::enableIRQ(int irq) {
    //OUTW(MPU_INTCPS_ILR(36 + gpionum), (INW(MPU_INTCPS_ILR(36 + gpionum)) & ~0x1));// normal irq
    theOS->getBoard()->getInterruptController()->setIRQPriority(irqNum, 1);  /* low priority! */
    theOS->getBoard()->getInterruptController()->unmaskIRQ(irqNum);
    return (cOk);
}

/*****************************************************************************
 * Method: AM335xGPIO::disableIRQ()
 *
 * @description
 * disables all interrupts of this device (does not clear them!)
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::disableIRQ(int irq) {
    theOS->getBoard()->getInterruptController()->maskIRQ(irqNum);
    return (cOk);
}


/*****************************************************************************
 * Method: AM335xGPIO::clearIRQ()
 *
 * @description
 * clears all interrupts of this device
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::clearIRQ(int irq) {
    /* clear all bits */
    OUTW(this->baseAddress + AM335X_GPIO_IRQSTATUS0, 0xffffffff);
    return (cNotImplemented);
}

/*****************************************************************************
 * Method: AM335xGPIO::ioctl(int request, void* args)
 *
 * @description
 *  IOCTL commands to:
 *   Sets the direction of the GPIO pins. 0 = Output, 1 = Input
 *
 * @returns
 *  int         Error Code
 *******************************************************************************/
ErrorT AM335xGPIO::ioctl(int request, void* args) {
    // handle the io control request
    if (request == IOCTL_GPIO_SET_DIR) {
        OUTW(this->baseAddress + AM335X_GPIO_OE, (unint4) args);
        return (cOk);
    }
    if (request == IOCTL_GPIO_GET_DIR) {
        VALIDATE_IN_PROCESS(args);
        OUTW(args, INW(this->baseAddress + AM335X_GPIO_OE));
        return (cOk);
    }
    if (request == IOCTL_GPIO_ENABLE_IRQ) {
        OUTW(this->baseAddress + AM335X_GPIO_IRQSTATUS_SET_0, (unint4) args);

        if (args != 0) {
            enableIRQ(irqNum);
        } else {
            disableIRQ(irqNum);
        }
        return (cOk);
    }
    if (request == IOCTL_GPIO_GET_IRQ_STATUS) {
        VALIDATE_IN_PROCESS(args);
        OUTW(args, irqStatus);
        irqStatus = 0;
        return (cOk);
    }
    if (request == IOCTL_GPIO_IRQ_RISING_EDGE) {
        OUTW(this->baseAddress + AM335X_GPIO_RISINGDETECT, (unint4) args);
        return (cOk);
    }
    if (request == IOCTL_GPIO_IRQ_FALLING_EDGE) {
        OUTW(this->baseAddress + AM335X_GPIO_FALLINGDETECT, (unint4) args);
        return (cOk);
    }

    return (cInvalidArgument);
}
