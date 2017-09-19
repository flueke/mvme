/***************************************************************************/
/*                                                                         */
/*  Filename: sis3153eth.h                                                 */
/*                                                                         */
/*  Funktion:                                                              */
/*                                                                         */
/*  Autor:                TH                                               */
/*  date:                 20.07.2017                                       */
/*  last modification:    03.08.2017                                       */
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
/*  © 2017                                                                 */
/*                                                                         */
/***************************************************************************/


#define SIS3153ETH_CONTROL_STATUS					0x0	 
#define SIS3153ETH_MODID_VERSION					0x1	 
#define SIS3153ETH_SERIAL_NUMBER_REG				0x2	 
#define SIS3153ETH_LEMO_IO_CTRL_REG					0x3	 

#define SIS3153ETH_UDP_PROTOCOL_CONFIG				0x4

#define SIS3153ETH_VME_MASTER_CONTROL_STATUS		0x10	 
#define SIS3153ETH_VME_MASTER_CYCLE_STATUS			0x11	 
#define SIS3153ETH_VME_INTERRUPT_STATUS				0x12

#define SIS3153ETH_KEY_RESET_ALL 					0x0100 



#define SIS3153ETH_STACK_LIST1_CONFIG				0x01000000  
#define SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE		0x01000001  

#define SIS3153ETH_STACK_LIST2_CONFIG				0x01000002  
#define SIS3153ETH_STACK_LIST2_TRIGGER_SOURCE		0x01000003  

#define SIS3153ETH_STACK_LIST3_CONFIG				0x01000004  
#define SIS3153ETH_STACK_LIST3_TRIGGER_SOURCE		0x01000005 

#define SIS3153ETH_STACK_LIST4_CONFIG				0x01000006  
#define SIS3153ETH_STACK_LIST4_TRIGGER_SOURCE		0x01000007  

#define SIS3153ETH_STACK_LIST5_CONFIG				0x01000008  
#define SIS3153ETH_STACK_LIST5_TRIGGER_SOURCE		0x01000009  

#define SIS3153ETH_STACK_LIST6_CONFIG				0x0100000A  
#define SIS3153ETH_STACK_LIST6_TRIGGER_SOURCE		0x0100000B  

#define SIS3153ETH_STACK_LIST7_CONFIG				0x0100000C  
#define SIS3153ETH_STACK_LIST7_TRIGGER_SOURCE		0x0100000D  

#define SIS3153ETH_STACK_LIST8_CONFIG				0x0100000E  
#define SIS3153ETH_STACK_LIST8_TRIGGER_SOURCE		0x0100000F  


#define SIS3153ETH_STACK_LIST_CONTROL				0x01000010  
#define SIS3153ETH_STACK_LIST_TRIGGER_CMD			0x01000011  
#define SIS3153ETH_STACK_LIST_SHORT_PACKAGE			0x01000012  

#define SIS3153ETH_STACK_LIST_TIMER1				0x01000014  
#define SIS3153ETH_STACK_LIST_TIMER2				0x01000015  


#define SIS3153ETH_STACK_RAM_START_ADDR				0x01800000	// 32-bit word addressing -> 0,1,2,  .. (SIS3153ETH_STACK_RAM_LENGTH-1)
#define SIS3153ETH_STACK_RAM_LENGTH					0x2000		// 8K x 32 

