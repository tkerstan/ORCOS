/*
 * OmapGPIO.hh
 *
 *  Created on: 02.08.2013
 *   Copyright &  Author: dbaldin
 */

#ifndef OMAPGPIO_HH_
#define OMAPGPIO_HH_

#include "hal/CharacterDevice.hh"
#include "hal/GenericDeviceDriver.hh"

#define GPIO_CLEARIRQENABLE1        0x60
#define GPIO_SETIRQENABLE1          0x64
#define GPIO_CLEARWKUENA            0x80
#define GPIO_CLEARDATAOUT           0x90
#define GPIO_SETDATAOUT             0x94

#define GPIO_FALLINGDETECT          0x4c
#define GPIO_RISINGDETECT           0x48

#define GPIO_OE                     0x34

#define GPIO_IRQSTATUS1             0x18

/*

 GPIO1 Module
 GPIO1_MPU_IRQ M_IRQ_29 Destination is the MPU subsystem interrupt controller.
 GPIO1_IVA2_IRQ IVA2_IRQ[28] Destination is the IVA2.2 subsystem interrupt controller.
 GPIO2 Module
 GPIO2_MPU_IRQ M_IRQ_30 Destination is the MPU subsystem interrupt controller.
 GPIO2_IVA2_IRQ IVA2_IRQ[29] Destination is the IVA2.2 subsystem interrupt controller.
 GPIO3 Module
 GPIO3_MPU_IRQ M_IRQ_31 Destination is the MPU subsystem interrupt controller.
 GPIO3_IVA2_IRQ IVA2_IRQ[30] Destination is the IVA2.2 subsystem interrupt controller.
 GPIO4 Module
 GPIO4_MPU_IRQ M_IRQ_32 Destination is the MPU subsystem interrupt controller.
 GPIO4_IVA2_IRQ IVA2_IRQ[31] Destination is the IVA2.2 subsystem interrupt controller.
 GPIO5 Module
 GPIO5_MPU_IRQ M_IRQ_33 Destination is the MPU subsystem interrupt controller.
 GPIO5_IVA2_IRQ IVA2_IRQ[32] Destination is the IVA2.2 subsystem interrupt controller.
 GPIO6 Module
 GPIO6_MPU_IRQ M_IRQ_34 Destination is the MPU subsystem interrupt controller.
 GPIO6_IVA2_IRQ IVA2_IRQ[43] Destination is the IVA2.2 subsystem interrupt controller.

 GPIOi.GPIO_IRQENABLE1 is for MPU IRQ line (microprocessor)
 GPIOi.GPIO_IRQENABLE2 is for IVA IRQ line (image video and audio)
 *
 */

/*!
 * \brief Simple GPIO Driver for the OMAP SOC
 *
 */
class OmapGPIO: public CharacterDevice {
private:
    /* The base address of this GPIO device*/
    int4 baseAddress;

    /* The GPIO number (1-6) of this device*/
    unint1 gpionum;

    unint4 irqStatus;

public:
    /*****************************************************************************
     * Method: OmapGPIO(T_OmapGPIO_Init *init)
     *
     * @description
     *  Constructor called by board initialization
     *******************************************************************************/
    explicit OmapGPIO(T_OmapGPIO_Init *init);

    virtual ~OmapGPIO();


    /*****************************************************************************
     * Method: readBytes(char *bytes, unint4 &length)
     *
     * @description
     *  Reads the GPIO state. Length must be 4 bytes for the 32 bit GPIO
     * @params
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT readBytes(char *bytes, unint4 &length);

    /*****************************************************************************
     * Method: writeBytes(const char *bytes, unint4 length)
     *
     * @description
     *  Sets the GPIO output state. Length must be 4 bytes.
     * @params
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT writeBytes(const char *bytes, unint4 length);

    /*****************************************************************************
     * Method: handleIRQ()
     *
     * @description
     *  Handles IRQs generated by GPIO pins
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT handleIRQ(int irq);

    /*****************************************************************************
     * Method: probe()
     *
     * @description
     *  Checks if this device is valid.
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT probe() {
        if (gpionum == -1)
            return (cError);
        return (cOk);
    }


    /*****************************************************************************
     * Method: enableIRQ()
     *
     * @description
     * enables the hardware interrupts of this device
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT enableIRQ(int irq);

    /*****************************************************************************
     * Method: disableIRQ()
     *
     * @description
     * disables all interrupts of this device (does not clear them!)
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT disableIRQ(int irq);

    /*****************************************************************************
     * Method: clearIRQ()
     *
     * @description
     * clears all interrupts of this device
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT clearIRQ(int irq);


    /*****************************************************************************
     * Method: ioctl(int request, void* args)
     *
     * @description
     *  IOCTL commands to:
     *   Sets the direction of the GPIO pins. 0 = Output, 1 = Input
     *
     * @returns
     *  int         Error Code
     *******************************************************************************/
    ErrorT ioctl(int request, void* args);
};

#endif /* OMAPGPIO_HH_ */
