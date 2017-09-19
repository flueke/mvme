/***************************************************************************/
/*                                                                         */
/*  Filename: sis3153usb.h                                                 */
/*                                                                         */
/*  Funktion:                                                              */
/*                                                                         */
/*  Autor:                TH                                               */
/*  date:                 22.10.2013                                       */
/*  last modification:    22.10.2013                                       */
/*                                                                         */
/* ----------------------------------------------------------------------- */
/*                                                                         */
/*  SIS  Struck Innovative Systeme GmbH                                    */
/*                                                                         */
/*  Harksheider Str. 102A                                                  */
/*  22399 Hamburg                                                          */
/*                                                                         */
/*  Tel. +49 (0)40 60 87 305 0                                             */
/*  Fax  +49 (0)40 60 87 305 20                                            */
/*                                                                         */
/*  http://www.struck.de                                                   */
/*                                                                         */
/*  © 2013                                                                 */
/*                                                                         */
/***************************************************************************/


#define SIS3153USB_CONTROL_STATUS					0x0	 
#define SIS3153USB_MODID_VERSION					0x1	 
#define SIS3153USB_SERIAL_NUMBER_REG				0x2	 
#define SIS3153USB_LEMO_IO_CTRL_REG					0x3	 


#define SIS3153USB_VME_MASTER_CONTROL_STATUS		0x10	 
#define SIS3153USB_VME_MASTER_CYCLE_STATUS			0x11	 
#define SIS3153USB_VME_INTERRUPT_STATUS				0x12

#define SIS3153USB_KEY_RESET_ALL 					0x0100 
