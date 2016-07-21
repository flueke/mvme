// Using libusb-win32 (http://libusb-win32.sourceforge.net) version 0.1.12.0.
// Libusb-win32 is a library that allows userspace application to access USB
// devices on Windows operation systems (Win98SE, WinME, Win2k, WinXP).
// It is derived from and fully API compatible to libusb available at
// http://libusb.sourceforge.net.
// http://libusb-win32.sourceforge.net
// http://sourceforge.net/projects/libusb-win32
/*
 03/09/06 Release 3.00 changes
 07/28/06	correction CAMAC write for F to be in range 16...23
->New Version for use with LIBUSB version 1.12
  Andreas Ruben,									03/12/07

->Corrections by Jan Toke					03/20/2007
  1.  Internal buffer length for xxusb_stack_write corrected. It was
  2000 bytes and did not accommodate the full stack I made it 2100 to be
  on safe side. 2048 would be sufficient.
  2. Internal buffer length for xxusb_stack_read. It was 1600 bytes and now it is 2100.
  3. xxusb_stack_execute modified such that now it has 100 ms timeout
  for stacks with just one entry. This improves response of xxusbwin when
  starting with CC-USB in P position.

->Corrections by Andreas Ruben		06/22/2007
  1. CAMAC_Read Q and X response and F-filter corrected for control codes without data return
  2. CAMAC DGG corrected 06/25/07
  3. VME and CAMAC_scaler_settings added06/26/07

->Change by Andreas Ruben		06/22/2007
  1. 8 bit VME Read added, may need check which byte should be read, not fully tested yet!!!

->Change by Andreas Ruben		06/24/2008
  1. bug fix in CAMAC read for return -1 in case not executed

->Change by Andreas Ruben		08/24/2009
    1. CAMACDGG: Added firmware version detection for proper setting of outputs with FW 5.01

->Change by Andreas Ruben		04/20/2010
  1.CAMAC_block-read16(usb_dev_handle *hdev, int N, int A, int F, int loops, int *Data)added

-> Add-on from Jan Tokes Library (libusb1.2.6 compatible) 08/25/2012
  1. ccusb_longstackexecute
  2. xxusb_init

*/
// libxxusb.cpp : Defines the entry point for the DLL application.
//

#include <string.h>
#include <malloc.h>
#include "libxxusb.h"
#include <time.h>
#include <QDebug>

/*
******** xxusb_longstack_execute ************************

  Executes stack array passed to the function and returns the data read from the VME bus

  Paramters:
    hdev: USB device handle returned from an open function
    DataBuffer: pointer to the dual use buffer
    when calling , DataBuffer contains (unsigned short) stack data, with first word serving
    as a placeholder
    upon successful return, DataBuffer contains (unsigned short) VME data
    lDataLen: The number of bytes to be fetched from VME bus - not less than the actual number
    expected, or the function will return -5 code. For stack consisting only of write operations,
    lDataLen may be set to 1.
    timeout: The time in ms that should be spent tryimg to write data.

  Returns:
    When Successful, the number of bytes read from xxusb.
    Upon failure, a negative number

  Note:
  The function must pass a pointer to an array of unsigned integer stack data, in which the first word
  is left empty to serve as a placeholder.
  The function is intended for executing long stacks, up to 4 MBytes long, both "write" and "read"
  oriented, such as using multi-block transfer operations.
  Structure upon call:
    DataBuffer(0) = 0(don't care place holder)
    DataBuffer(1) = (unsigned short)StackLength bits 0-15
    DataBuffer(2) = (unsigned short)StackLength bits 16-20
    DataBuffer(3 - StackLength +2)  (unsigned short) stack data
      StackLength represents the number of words following DataBuffer(1) word, thus the total number
    of words is StackLength+2
  Structure upon return:
    DataBuffer(0 - (ReturnValue/2-1)) - (unsigned short)array of returned data when ReturnValue>0
*/

int  xxusb_longstack_execute(usb_dev_handle *hDev, void *DataBuffer, int lDataLen, int timeout)
{
    int ret;
    char *cbuf;
  unsigned short *usbuf;
    int bufsize;

  cbuf = (char *)DataBuffer;
  usbuf = (unsigned short *)DataBuffer;
    cbuf[0]=12;
    cbuf[1]=0;
  bufsize = 2*(usbuf[1]+0x10000*usbuf[2])+4;
    ret=usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, cbuf, bufsize, timeout);
    if (ret>0)
        ret=usb_bulk_read(hDev, XXUSB_ENDPOINT_IN, cbuf, lDataLen, timeout);

    return ret;
}


int  ccusb_longstack_execute(usb_dev_handle *hDev, void *DataBuffer, int lDataLen, int timeout)
{
    int ret;
    char *cbuf;
  unsigned short *usbuf;
    int bufsize;

  cbuf = (char *)DataBuffer;
  usbuf = (unsigned short *)DataBuffer;
    cbuf[0]=12;
    cbuf[1]=0;
  bufsize = 2*usbuf[1]+4;
    ret=usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, cbuf, bufsize, timeout);
    if (ret>0)
        ret=usb_bulk_read(hDev, XXUSB_ENDPOINT_IN, cbuf, lDataLen, timeout);

    return ret;
}


/*
******** xxusb_bulk_read ************************

  Reads the content of the usbfifo whenever "FIFO full" flag is set,
    otherwise times out.

  Paramters:
    hdev: USB device handle returned from an open function
    DataBuffer: pointer to an array to store data that is read from the VME bus;
    the array may be declared as byte, unsigned short, or unsigned long
    lDatalen: The number of bytes to read from xxusb
    timeout: The time in ms that should be spent waiting for data.

  Returns:
    When Successful, the number of bytes read from xxusb.
    Upon failure, a negative number

  Note:
  Depending upon the actual need, the function may be used to return the data in a form
  of an array of bytes, unsigned short integers (16 bits), or unsigned long integers (32 bits).
  The latter option of passing a pointer to an array of unsigned long integers is meaningful when
  xxusb data buffering option is used (bit 7=128 of the global register) that requires data
  32-bit data alignment.

*/
int  xxusb_bulk_read(usb_dev_handle *hDev, void *DataBuffer, int lDataLen, int timeout)
{
int ret;
char *cbuf;
cbuf = (char *)DataBuffer;
    ret = usb_bulk_read(hDev, XXUSB_ENDPOINT_IN, cbuf, lDataLen, timeout);
    return ret;
}

/*
******** xxusb_bulk_write ************************

  Writes the content of an array of bytes, unsigned short integers, or unsigned long integers
  to the USB port fifo; times out when the USB fifo is full (e.g., when xxusb is busy).

  Paramters:
    hdev: USB device handle returned from an open function
    DataBuffer: pointer to an array storing the data to be sent;
    the array may be declared as byte, unsigned short, or unsigned long
    lDatalen: The number of bytes to to send to xxusb
    timeout: The time in ms that should be spent waiting for data.

  Returns:
    When Successful, the number of bytes passed to xxusb.
    Upon failure, a negative number

  Note:
  Depending upon the actual need, the function may be used to pass to xxusb the data in a form
  of an array of bytes, unsigned short integers (16 bits), or unsigned long integers (32 bits).
*/
int  xxusb_bulk_write(usb_dev_handle *hDev, void *DataBuffer, int lDataLen, int timeout)
{
int ret;
char *cbuf;
cbuf = (char *)DataBuffer;
    ret = usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, cbuf, lDataLen, timeout);
    return ret;
}

/*
******** xxusb_usbfifo_read ************************

  Reads data stored in the xxusb fifo and packs them in an array of long integers.

  Paramters:
    hdev: USB device handle returned from an open function
    DataBuffer: pointer to an array of long to store data that is read
    the data occupy only the least significant 16 bits of the 32-bit data words
    lDatalen: The number of bytes to read from the xxusb
    timeout: The time in ms that should be spent waiting for data.

  Returns:
    When Successful, the number of bytes read from xxusb.
    Upon failure, a negative number

  Note:
  The function is not economical as it wastes half of the space required for storing
  the data received. Also, it is relatively slow, as it performs extensive data repacking.
  It is recommended to use xxusb_bulk_read with a pointer to an array of unsigned short
  integers.
*/
int  xxusb_usbfifo_read(usb_dev_handle *hDev, int *DataBuffer, int lDataLen, int timeout)
{
int ret;
char *cbuf;
unsigned short *usbuf;
int i;

cbuf = (char *)DataBuffer;
usbuf = (unsigned short *)DataBuffer;

    ret = usb_bulk_read(hDev, XXUSB_ENDPOINT_IN, cbuf, lDataLen, timeout);
    if (ret > 0)
        for (i=ret/2-1; i >= 0; i=i-1)
    {
      usbuf[i*2]=usbuf[i];
      usbuf[i*2+1]=0;
    }
    return ret;
}


//******************************************************//
//******************* GENERAL XX_USB *******************//
//******************************************************//
// The following are functions used for both VM_USB & CC_USB


/*
******** xxusb_register_write ************************

  Writes Data to the xxusb register selected by RedAddr.  For
    acceptable values for RegData and RegAddr see the manual
    the module you are using.

  Parameters:
    hdev: usb device handle returned from open device
    RegAddr: The internal address if the xxusb
    RegData: The Data to be written to the register

  Returns:
    Number of bytes sent to xxusb if successful
    0 if the register is write only
    Negative numbers if the call fails
*/
short  xxusb_register_write(usb_dev_handle *hDev, short RegAddr, long RegData)
{
    long RegD;
    char buf[8]={5,0,0,0,0,0,0,0};
    int ret;
    int lDataLen;
    int timeout;
    if ((RegAddr==0) || (RegAddr==12) || (RegAddr==15))
        return 0;
    buf[2]=(char)(RegAddr & 15);
    buf[4]=(char)(RegData & 255);

    RegD = RegData >> 8;
    buf[5]=(char)(RegD & 255);
    RegD = RegD >>8;
    if (RegAddr==8)
    {
        buf[6]=(char)(RegD & 255);
        lDataLen=8;
    }
    else
        lDataLen=6;
    timeout=100;

    ret=xxusb_bulk_write(hDev, buf, lDataLen, timeout);
    return ret;
}

/*
******** xxusb_stack_write ************************

  Writes a stack of VME/CAMAC calls to the VM_USB/CC_USB
    to be executed upon trigger.

  Parameters:
    hdev: usb device handle returned from an open function
    StackAddr: internal register to which the stack should be written
    lpStackData: Pointer to an array holding the stack

  Returns:
    The number of Bytes written to the xxusb when successful
    A negative number upon failure
*/
short  xxusb_stack_write(usb_dev_handle *hDev, short StackAddr, long *intbuf)
{
  int timeout;
    short ret;
    short lDataLen;
    char buf[2100];
    short i;
    int bufsize;

    buf[0]=(char)((StackAddr & 51) + 4);
    buf[1]=0;
    lDataLen=(short)(intbuf[0] & 0xFFF);
    buf[2]=(char)(lDataLen & 255);
    lDataLen = lDataLen >> 8;
    buf[3] = (char)(lDataLen & 255);
    bufsize=intbuf[0]*2+4;
    if (intbuf[0]==0)
      return 0;
    for (i=1; i <= intbuf[0]; i++)
      {
        buf[2+2*i] = (char)(intbuf[i] & 255);
        buf[3+2*i] = (char)((intbuf[i] >>8) & 255);
      }
    timeout=100;
    ret=usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, buf, bufsize, timeout);
    return ret;
}

/*
******** xxusb_stack_execute **********************

  Writes, executes and returns the value of a DAQ stack.

  Parameters:
    hdev: USB device handle returned from an open function
    intbuf: Pointer to an array holding the values stack.  Upon return
            Pointer value is the Data returned from the stack.

  Returns:
    When successful, the number of Bytes read from xxusb
    Upon Failure, a negative number.
*/
short  xxusb_stack_execute(usb_dev_handle *hDev, long *intbuf)
{
  int timeout;
  short ret;
  short lDataLen;
  char buf[26700];
  short i;
  int bufsize;
  int ii = 0;

  // word 0: write bit and VCG bit set; (1<<2) | (1<<3)
  buf[0]=12;
  buf[1]=0;

  lDataLen=(short)(intbuf[0] & 0xFFF);
  buf[2]=(char)(lDataLen & 255);
  lDataLen = lDataLen >> 8;
  buf[3] = (char)(lDataLen & 15);

  bufsize=intbuf[0]*2+4;
  if (intbuf[0]==0)
    return 0;
  for (i=1; i <= intbuf[0]; i++)
  {
    buf[2+2*i] = (char)(intbuf[i] & 255);
    buf[3+2*i] = (char)((intbuf[i] >>8) & 255);
  }
  if (intbuf[0]==1)
    timeout=100;
  else
    timeout=2000;

  /*
  qDebug("xxusb_stack_execute(): stack:");

  for (int bufferIndex=0; bufferIndex<bufsize/4; ++bufferIndex)
  {
      qDebug("  0x%08lx", ((quint32 *)buf)[bufferIndex]);
  }
  */


  ret=usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, buf, bufsize, timeout);


  //qDebug("xxusb_stack_execute(): write returned %d", ret);

  if (ret>0)
  {
    lDataLen=26700;
    if (intbuf[0]==1)
      timeout=100;
    else
      timeout=6000;

    ret=usb_bulk_read(hDev, XXUSB_ENDPOINT_IN, buf, lDataLen, timeout);

    //qDebug("xxusb_stack_execute(): read returned %d", ret);

    if (ret>0)
      for (i=0; i < ret; i=i+2)
        intbuf[ii++]=(UCHAR)(buf[i]) +(UCHAR)( buf[i+1])*256;
  }
  return ret;
}

/*
******** xxusb_stack_read ************************

  Reads the current DAQ stack stored by xxusb

  Parameters:
    hdev: USB device handle returned by an open function
    StackAddr: Indicates which stack to read, primary or secondary
    intbuf: Pointer to a array where the stack can be stored

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  xxusb_stack_read(usb_dev_handle *hDev, short StackAddr, long *intbuf)
{
  int timeout;
    short ret;
    short lDataLen;
    short bufsize;
    char buf[2100];
    int i;

    buf[0]=(char)(StackAddr & 51);
    buf[1]=0;
    lDataLen = 2;
    timeout=100;
    ret=usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, buf, lDataLen, timeout);
    if (ret < 0)
        return ret;
    else
      bufsize=1600;
    int ii=0;
      {
        ret=usb_bulk_read(hDev, XXUSB_ENDPOINT_IN, buf, bufsize, timeout);
        if (ret>0)
        for (i=0; i < ret; i=i+2)
          intbuf[ii++]=(UCHAR)(buf[i]) + (UCHAR)(buf[i+1])*256;
        return ret;

      }
}

/*
******** xxusb_register_read ************************

  Reads the current contents of an internal xxusb register

  Parameters:
    hdev: USB device handle returned from an open function
    RegAddr: The internal address of the register from which to read
    RegData: Pointer to a long to hold the data.

  Returns:
    When Successful, the number of bytes read from xxusb.
    Upon failure, a negative number
*/
short  xxusb_register_read(usb_dev_handle *hDev, short RegAddr, long *RegData)
{
    //long RegD;
    int timeout;
    char buf[]={1,0,0,0,0,0,0,0};
    int ret;
    int lDataLen;

    buf[2]=(char)(RegAddr & 15);
    timeout=100;
    lDataLen=4;
    ret=xxusb_bulk_write(hDev, buf, lDataLen, timeout);
    if (ret < 0)
        return (short)ret;
    else
    {
        lDataLen=8;
        timeout=100;
        ret=xxusb_bulk_read(hDev, buf, lDataLen, timeout);
        if (ret<0)
            return (short)ret;
        else
        {
            *RegData=(UCHAR)(buf[0])+256*(UCHAR)(buf[1]);
            if (ret==4)
                *RegData=*RegData+0x10000*(UCHAR)(buf[2]);
            return (short)ret;
        }
    }

    return 0;
}




/*
******** xxusb_reset_toggle ************************

  Toggles the reset state of the FPGA while the xxusb in programming mode

  Parameters
    hdev: US B device handle returned from an open function

  Returns:
    Upon failure, a negative number
*/
short  xxusb_reset_toggle(usb_dev_handle *hDev)
{
  short ret;
  char buf[2] = {(char)255,(char)255};
  int lDataLen=2;
  int timeout=10;
  ret = usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, buf,lDataLen, timeout);
  return (short)ret;
}
/*
******** xxusb_find ************************

  Performs usb_init
  added 08/25/2012

*/

void  xxusb_init()
{
  usb_init();
}
/*
******** xxusb_devices_find ************************

  Determines the number and parameters of all xxusb devices attched to
    the computer.

  Parameters:
    xxdev: pointer to an array on which the device parameters are stored

  Returns:
    Upon success, returns the number of devices found
    Upon Failure returns a negative number
*/
short  xxusb_devices_find(xxusb_device_type *xxdev)
{
    short DevFound = 0;
    usb_dev_handle *udev;
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_bus *usb_busses_;
    char string[256];
    short ret;
    usb_init();
    usb_find_busses();
    usb_busses_=usb_get_busses();
    usb_find_devices();
    for (bus=usb_busses_; bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev= dev->next)
        {
            if (dev->descriptor.idVendor==XXUSB_WIENER_VENDOR_ID)
            {
                udev = usb_open(dev);
                if (udev)
                {
                    ret = usb_get_string_simple(udev, dev->descriptor.iSerialNumber, string, sizeof(string));
                    if (ret >0 )
                    {
                        xxdev[DevFound].usbdev=dev;
                        strcpy(xxdev[DevFound].SerialString, string);
                        DevFound++;
                    }
                    usb_close(udev);
                }
                else return -1;
            }
        }
    }
    return DevFound;
}

/*
******** xxusb_device_close ************************

  Closes an xxusb device

  Parameters:
    hdev: USB device handle returned from an open function

  Returns:  1
*/
short  xxusb_device_close(usb_dev_handle *hDev)
{
    short ret;
    ret=usb_release_interface(hDev,0);
    usb_close(hDev);
    return 1;
}

/*
******** xxusb_device_open ************************

  Opens an xxusb device found by xxusb_device_find

  Parameters:
    dev: a usb device

  Returns:
    A USB device handle
*/
usb_dev_handle*  xxusb_device_open(struct usb_device *dev)
{
    usb_dev_handle *udev;
    udev = usb_open(dev);

    if (!udev) return NULL;

    int res = usb_set_configuration(udev,1);

    if (res < 0)
    {
        qDebug("usb_set_configuration failed");
        usb_close(udev);
        return NULL;
    }

    res = usb_claim_interface(udev,0);

    if (res < 0)
    {
        qDebug("usb_claim_interface failed");
        usb_close(udev);
        return NULL;
    }

    // RESET USB (added 10/16/06 Andreas Ruben)
    res = xxusb_register_write(udev, 10, 0x04);

    return udev;
}

/*
******** xxusb_flash_program ************************

    --Untested and therefore uncommented--
*/
short  xxusb_flash_program(usb_dev_handle *hDev, char *config, short nsect)
{
  int i=0;
  int k=0;
  short ret=0;
  time_t t1,t2;

  char *pconfig;
  char *pbuf;
  pconfig=config;
  char buf[518] ={(char)0xAA,(char)0xAA,(char)0x55,(char)0x55,(char)0xA0,(char)0xA0};
  while (*pconfig++ != -1);
  for (i=0; i<nsect; i++)
  {
    pbuf=buf+6;
    for (k=0; k<256; k++)
    {
      *(pbuf++)=*(pconfig);
      *(pbuf++)=*(pconfig++);
    }
    ret = usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, buf, 518, 2000);
    if (ret<0)
      return ret;
    t1=clock()+(time_t)(0.03*CLOCKS_PER_SEC);
    while (t1>clock());
    t2=clock();
  }
  return ret;
}

/*
******** xxusb_flashblock_program ************************

      --Untested and therefore uncommented--
*/
short  xxusb_flashblock_program(usb_dev_handle *hDev, UCHAR *config)
{
  int k=0;
  short ret=0;

  UCHAR *pconfig;
  char *pbuf;
  pconfig=config;
  char buf[518] ={(char)0xAA,(char)0xAA,(char)0x55,(char)0x55,(char)0xA0,(char)0xA0};
  pbuf=buf+6;
  for (k=0; k<256; k++)
  {
    *(pbuf++)=(UCHAR)(*(pconfig));
    *(pbuf++)=(UCHAR)(*(pconfig++));
  }
  ret = usb_bulk_write(hDev, XXUSB_ENDPOINT_OUT, buf, 518, 2000);
  return ret;
}

/*
******** xxusb_serial_open ************************

  Opens a xxusb device whose serial number is given

  Parameters:
    SerialString: a char string that gives the serial number of
                  the device you wish to open.  It takes the form:
                  VM0009 - for a vm_usb with serial number 9 or
                  CC0009 - for a cc_usb with serial number 9

  Returns:
    A USB device handle
*/
usb_dev_handle*  xxusb_serial_open(char *SerialString)
{
  short DevFound = 0;
  usb_dev_handle *udev = NULL;
  struct usb_bus *bus;
  struct usb_device *dev;
struct usb_bus *usb_busses_;
char string[7];
  short ret;
 usb_set_debug(1000);
  usb_init();
   usb_find_busses();
  usb_busses_=usb_get_busses();
   usb_find_devices();
   for (bus=usb_busses_; bus; bus = bus->next)
   {
       for (dev = bus->devices; dev; dev= dev->next)
       {
       if (dev->descriptor.idVendor==XXUSB_WIENER_VENDOR_ID)
       {
           udev = xxusb_device_open(dev);
         if (udev)
               {
       ret = usb_get_string_simple(udev, dev->descriptor.iSerialNumber, string, sizeof(string));
          if (ret >0 )
            {
            if (strcmp(string,SerialString)==0)
                return udev;
            }
            usb_close(udev);
        }
     }
       }
    }
   udev = NULL;
   return udev;
}


//******************************************************//
//****************** EZ_VME Functions ******************//
//******************************************************//
// The following are functions used to perform simple
// VME Functions with the VM_USB

/*
******** VME_write_32 ************************

  Writes a 32 bit data word to the VME bus

  Parameters:
    hdev: USB devcie handle returned from an open function
    Address_Modifier: VME address modifier for the VME call
    VME_Address: Address to write the data to
    Data: 32 bit data word to be written to VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_write_32(usb_dev_handle *hdev, short Address_Modifier, long VME_Address, long Data)
{
    long intbuf[1000];
    short ret;
    intbuf[0]=7;
    intbuf[1]=0;
    intbuf[2]=Address_Modifier;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff);
    intbuf[5]=((VME_Address >>16) & 0xffff);
    intbuf[6]=(Data & 0xffff);
    intbuf[7]=((Data >> 16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    return ret;
}

/*
******** VME_read_32 ************************


  Reads a 32 bit data word from a VME address

  Parameters:
    hdev: USB devcie handle returned from an open function
    Address_Modifier: VME address modifier for the VME call
    VME_Address: Address to read the data from
    Data: 32 bit data word read from VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_read_32(usb_dev_handle *hdev, short Address_Modifier, long VME_Address, long *Data)
{
    long intbuf[1000];
    short ret;
    intbuf[0]=5;
    intbuf[1]=0;
    intbuf[2]=Address_Modifier +0x100;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff);
    intbuf[5]=((VME_Address >>16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    *Data=intbuf[0] + (intbuf[1] * 0x10000);
    return ret;
}

/*
******** VME_write_16 ************************

  Writes a 16 bit data word to the VME bus

  Parameters:
    hdev: USB devcie handle returned from an open function
    Address_Modifier: VME address modifier for the VME call
    VME_Address: Address to write the data to
    Data: word to be written to VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_write_16(usb_dev_handle *hdev, short Address_Modifier, long VME_Address, long Data)
{
    long intbuf[1000];
    short ret;
    intbuf[0]=7;
    intbuf[1]=0;
    intbuf[2]=Address_Modifier;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff)+ 0x01;
    intbuf[5]=((VME_Address >>16) & 0xffff);
    intbuf[6]=(Data & 0xffff);
    intbuf[7]=0;
    ret = xxusb_stack_execute(hdev, intbuf);
    return ret;
}

/*
******** VME_read_16 ************************

  Reads a 16 bit data word from a VME address

  Parameters:
    hdev: USB devcie handle returned from an open function
    Address_Modifier: VME address modifier for the VME call
    VME_Address: Address to read the data from
    Data: word read from VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_read_16(usb_dev_handle *hdev,short Address_Modifier, long VME_Address, long *Data)
{
    long intbuf[1000];
    short ret;
    intbuf[0]=5;
    intbuf[1]=0;
    intbuf[2]=Address_Modifier +0x100;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff)+ 0x01;
    intbuf[5]=((VME_Address >>16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    *Data=intbuf[0];
    return ret;
}

/*
******** VME_read_16 ************************

  Reads a 16 bit data word from a VME address

  Parameters:
    hdev: USB devcie handle returned from an open function
    Address_Modifier: VME address modifier for the VME call
    VME_Address: Address to read the data from
    Data: word read from VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_read_8(usb_dev_handle *hdev,short Address_Modifier, long VME_Address, long *Data)
{
    long intbuf[1000];
    short ret;
    intbuf[0]=5;
    intbuf[1]=0;
    intbuf[2]= (Address_Modifier & 0x3f) + 0x40;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff)+ 0x02;
    intbuf[5]=((VME_Address >>16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    *Data=intbuf[0];
    return ret;
}

/*
******** VME_BLT_read_32 ************************

  Performs block transfer of 32 bit words from a VME address

  Parameters:
    hdev: USB devcie handle returned from an open function
    Address_Modifier: VME address modifier for the VME call
    count: number of data words to read
    VME_Address: Address to read the data from
    Data: pointer to an array to hold the data words

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_BLT_read_32(usb_dev_handle *hdev, short Adress_Modifier, int count, long VME_Address, long Data[])
{
    long intbuf[1000];
    short ret;
    int i=0;
    if (count > 255) return -1;
    intbuf[0]=5;
    intbuf[1]=0;
    intbuf[2]=Adress_Modifier +0x100;
    intbuf[3]=(count << 8);
    intbuf[4]=(VME_Address & 0xffff);
    intbuf[5]=((VME_Address >>16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    int j=0;
    for (i=0;i<(2*count);i=i+2)
    {
  Data[j]=intbuf[i] + (intbuf[i+1] * 0x10000);
  j++;
    }
    return ret;
}

//******************************************************//
//****************** VM_USB Registers ******************//
//******************************************************//
// The following are functions used to set the registers
// in the VM_USB

/*
******** VME_register_write ************************

  Writes to the vmusb registers that are accessible through
  VME style calls

  Parameters:
    hdev: USB devcie handle returned from an open function
    VME_Address: The VME Address of the internal register
    Data: Data to be written to VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_register_write(usb_dev_handle *hdev, long VME_Address, long Data)
{
    long intbuf[1000];
    short ret;

    intbuf[0]=7;
    intbuf[1]=0;
    intbuf[2]=0x1000;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff);
    intbuf[5]=((VME_Address >>16) & 0xffff);
    intbuf[6]=(Data & 0xffff);
    intbuf[7]=((Data >> 16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    return ret;
}

/*
******** VME_register_read ************************

  Reads from the vmusb registers that are accessible trough VME style calls

  Parameters:
    hdev: USB devcie handle returned from an open function
    VME_Address: The VME Address of the internal register
    Data: Data read from VME_Address

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_register_read(usb_dev_handle *hdev, long VME_Address, long *Data)
{
    long intbuf[1000];
    short ret;

    intbuf[0]=5;
    intbuf[1]=0;
    intbuf[2]=0x1100;
    intbuf[3]=0;
    intbuf[4]=(VME_Address & 0xffff);
    intbuf[5]=((VME_Address >>16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);
    *Data=intbuf[0] + (intbuf[1] * 0x10000);
    return ret;
}

/*
******** VME_LED_settings ************************

  Sets the vmusb LED's

  Parameters:
    hdev: USB devcie handle returned from an open function
    LED: The number which corresponds to an LED values are:
            0 - for Top YELLOW LED
      1 - for RED LED
            2 - for GREEN LED
            3 - for Bottom YELLOW LED
    code: The LED aource selector code, valid values for each LED
          are listed in the manual
    invert: to invert the LED lighting
    latch: sets LED latch bit

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_LED_settings(usb_dev_handle *hdev, int LED, int code, int invert, int latch)
{
    short ret;
//    long internal;
    long Data;

    if( (LED <0) ||(LED > 3) || (code < 0) || (code > 7))  return -1;

    VME_register_read(hdev,0xc,&Data);
    if(LED == 0)
    {
        Data = Data & 0xFFFFFF00;
        Data = Data | code;
        if (invert == 1 && latch == 1)   Data = Data | 0x18;
        if (invert == 1 && latch == 0)   Data = Data | 0x08;
        if (invert == 0 && latch == 1)   Data = Data | 0x10;
    }
    if(LED == 1)
    {
        Data = Data & 0xFFFF00FF;
        Data = Data | (code * 0x0100);
        if (invert == 1 && latch == 1)  Data = Data | 0x1800;
        if (invert == 1 && latch == 0)  Data = Data | 0x0800;
        if (invert == 0 && latch == 1)  Data = Data | 0x1000;
    }
    if(LED == 2)
    {
        Data = Data & 0xFF00FFFF;
        Data = Data | (code * 0x10000);
        if (invert == 1 && latch == 1) Data = Data | 0x180000;
        if (invert == 1 && latch == 0) Data = Data | 0x080000;
        if (invert == 0 && latch == 1) Data = Data | 0x100000;
    }
    if(LED == 3)
    {
        Data = Data & 0x00FFFFFF;
        Data = Data | (code * 0x10000);
        if (invert == 1 && latch == 1)  Data = Data | 0x18000000;
        if (invert == 1 && latch == 0)  Data = Data | 0x08000000;
        if (invert == 0 && latch == 1)  Data = Data | 0x10000000;
    }
    ret = VME_register_write(hdev, 0xc, Data);
    return ret;
}

/*
******** VME_DGG ************************

  Sets the parameters for Gate & Delay channel A of vmusb

  Parameters:
    hdev: USB devcie handle returned from an open function
    channel: Which DGG channel to use Valid Values are:
              0 - For DGG A
        1 - For DGG B
    trigger:  Determines what triggers the start of the DGG Valid values are:
              0 - Channel disabled
              1 - NIM input 1
              2 - NIM input 2
              3 - Event Trigger
              4 - End of Event
              5 - USB Trigger
              6 - Pulser
    output: Determines which NIM output to use for the channel, Vaild values are:
              0 - for NIM O1
              1 - for NIM O2
    delay: 32 bit word consisting of
      lower 16 bits: Delay_fine in steps of 12.5ns between trigger and start of gate
      upper 16 bits: Delay_coarse in steps of 81.7us between trigger and start of gate
    gate: the time the gate should  stay open in steps of 12.5ns
    invert: is 1 if you wish to invert the DGG channel output
    latch: is 1 if you wish to run the DGG channel latched

  Returns:
    Returns 1 when successful
    Upon failure, a negative number
*/
short  VME_DGG(usb_dev_handle *hdev, unsigned short channel, unsigned short trigger, unsigned short output,
            long delay, unsigned short gate, unsigned short invert, unsigned short latch)
{
    long Data, DGData, Delay_ext;
    long internal;
    short ret;


    ret = VME_register_read(hdev, 0x10,  &Data);
    // check and correct values
    if(ret<=0) return -1;

    if(channel >1) channel =1;
    if(invert >1) invert =1;
    if(latch >1) latch =1;
  if(output >1) output =1;
  if(trigger >6) trigger =0;

    // define Delay and Gate data
    DGData = gate * 0x10000;
    DGData += (unsigned short) delay;

  // Set channel, output, invert, latch
  if (output == 0)
    {
    Data = Data & 0xFFFFFF00;
    Data += 0x04 + channel +0x08*invert + 0x10*latch;
    }
  if (output == 1)
    {
    Data = Data & 0xFFFF00FF;
    Data += (0x04 + channel +0x08*invert + 0x10*latch)*0x100;
    }

    // Set trigger, delay, gate

    if(channel ==0) // CHANNEL DGG_A
    {
      internal = (trigger * 0x1000000) ;
      Data= Data & 0xF0FFFFFF;
      Data += internal;
      ret = VME_register_write(hdev,0x10,Data);
    if(ret<=0) return -1;
    ret=VME_register_write(hdev,0x14,DGData);
    if(ret<=0) return -1;
    // Set coarse delay in DGG_Extended register
      ret = VME_register_read(hdev,0x38,&Data);
    Delay_ext= (Data & 0xffff0000);
    Delay_ext+= ((delay/0x10000) & 0xffff);
      ret = VME_register_write(hdev,0x38,Delay_ext);
  }
  if( channel ==1)  // CHANNEL DGG_B
  {
    internal = (trigger * 0x10000000) ;
    Data= Data & 0x0FFFFFFF;
    Data += internal;
      ret = VME_register_write(hdev,0x10,Data);
    if(ret<=0) return -1;
    ret=VME_register_write(hdev,0x18,DGData);
    if(ret<=0) return -1;
    // Set coarse delay in DGG_Extended register
      ret = VME_register_read(hdev,0x38,&Data);
    Delay_ext= (Data & 0x0000ffff);
    Delay_ext+= (delay & 0xffff0000);
      ret = VME_register_write(hdev,0x38,Delay_ext);
    }
  return 1;

}

/*
******** VME_Output_settings ************************

  Sets the vmusb NIM output register

  Parameters:
    hdev: USB devcie handle returned from an open function
    Channel: The number which corresponds to an output:
            1 - for Output 1
            2 - for Output 2
    code: The Output selector code, valid values
          are listed in the manual
    invert: to invert the output
    latch: sets latch bit

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_Output_settings(usb_dev_handle *hdev, int Channel, int code, int invert, int latch)
{

    short ret;
//    long internal;
    long Data;

    if( (Channel <1) ||(Channel > 2) || (code < 0) || (code > 7)) return -1;
    VME_register_read(hdev,0x10,&Data);
    if(Channel == 1)
    {
        Data = Data & 0xFFFF00;
        Data = Data | code;
        if (invert == 1 && latch == 1)   Data = Data | 0x18;
        if (invert == 1 && latch == 0)   Data = Data | 0x08;
        if (invert == 0 && latch == 1)   Data = Data | 0x10;
    }
    if(Channel == 2)
    {
        Data = Data & 0xFF00FF;
        Data = Data | (code * 0x0100);
        if (invert == 1 && latch == 1)   Data = Data | 0x1800;
        if (invert == 1 && latch == 0)   Data = Data | 0x0800;
        if (invert == 0 && latch == 1)   Data = Data | 0x1000;
    }
    ret = VME_register_write(hdev, 0x10, Data);
    return ret;
}
/*
******** VME_scaler_settings ************************

  Configures the internal VM-USB scaler (SelSource register)

  Parameters:
    hdev: USB devcie handle returned from an open function
    Channel: The number which corresponds to an output:
            0 - for Scaler A
            1 - for Scaler B
    code: The Output selector code, valid values
          are listed in the manual
    enable: =1 enables scaler
    latch: =1 resets the scaler at time of call

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  VME_scaler_settings(usb_dev_handle *hdev, short channel, short trigger, int enable, int reset)
{
  long Data, W_Data;
  short ret;
  ret = VME_register_read(hdev,16,&Data);
  if (channel ==0)
  {
    Data = Data & 0xfff0ffff;
    W_Data = ((reset & 0x01)*0x8 + (enable & 0x01)*0x4 + trigger)<<16;
    W_Data = Data | W_Data;
    ret = VME_register_write(hdev, 16, W_Data);
  }
  else
  {
    Data = Data & 0xff0fffff;
    W_Data = ((reset & 0x01)*0x8 + (enable & 0x01)*0x4 + trigger)<<20;
    W_Data = Data | W_Data;
    ret = VME_register_write(hdev, 16, W_Data);
   }
return ret;
}
//******************************************************//
//****************** CC_USB Registers ******************//
//******************************************************//
// The following are functions used to set the registers
// in the CAMAC_USB

/*
******** CAMAC_register_write *****************

  Performs a CAMAC write to CC_USB register

  Parameters:
    hdev: USB device handle returned from an open function
    A: CAMAC Subaddress of Register
    Data: data to be written

  Returns:
    Number of bytes written to xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_register_write(usb_dev_handle *hdev, int A, long Data)
{
    int F = 16;
    int N = 25;
    long intbuf[4];
    int ret;

    intbuf[0]=1;
    intbuf[1]=(long)(F+A*32+N*512 + 0x4000);
    intbuf[0]=3;
    intbuf[2]=(Data & 0xffff);
    intbuf[3]=((Data >>16) & 0xffff);
    ret = xxusb_stack_execute(hdev, intbuf);

    return ret;
}

/*
******** CAMAC_register_read ************************

  Performs a CAMAC read from CC_USB register

  Parameters:
    hdev: USB device handle returned from an open function
    A: CAMAC Subaddress of Register
    Data: data read from the register (32 bit)

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_register_read(usb_dev_handle *hdev, int A, long *Data)
{
    int F = 0;
    int N = 25;
    long intbuf[4];
    int ret;

    intbuf[0]=1;
    intbuf[1]=(long)(F+A*32+N*512 + 0x4000);
    ret = xxusb_stack_execute(hdev, intbuf);
    *Data=intbuf[0] + (intbuf[1] * 0x10000);

    return ret;
}


/*
******** CAMAC_DGG ************************

  Sets the parameters for Gate & Delay channel A of CC-USB

  Parameters:
    hdev: USB devcie handle returned from an open function
    channel: Which DGG channel to use Valid Values are:
              0 - For DGG A
        1 - For DGG B
    trigger:  Determines what triggers the start of the DGG Valid values are:
              0 - Channel disabled
              1 - NIM input 1
              2 - NIM input 2
              3 - Event Trigger
        4 - End of Event
        5 - USB Trigger
        7 - Pulser
    output: Determines which NIM output to use for the channel, Vaild values are:
              1 - for NIM O1
              2 - for NIM O2
              3 - for NIM O3
    delay: Delay in steps of 12.5ns between trigger and start of gate
    gate: the time the gate should  stay open in steps of 12.5ns
    invert: is 1 if you wish to invert the DGG channel output
    latch: is 1 if you wish to run the DGG channel latched

  Returns:
    Returns 1 when successful
    Upon failure, a negative number
*/
short  CAMAC_DGG(usb_dev_handle *hdev, short channel, short trigger, short output,
                              int delay, int gate, short invert, short latch)

{
   long Data, SL_Data;
    long internal;
    short ret, code, FW5flag=0;
  long Delay_ext;
// Added 08/2009 firmware version detection for proper setting of outputs with FW 5.01
// Read Firmware version
    ret = CAMAC_register_read(hdev,0,&Data);
  Data = Data & 0xfff;
  if (Data > 0x499)FW5flag = 1;

  if (FW5flag < 1 )
// Original pre FW 5.0 coding of channel and output:
    code = 2 + output + channel;
  else
// FW 5.0 and higher coding of channel and output, only Output 1 and 2 supported:
    code = 2 + channel;
//
// Get content o NIM output selector register
    ret = CAMAC_register_read(hdev,5,&Data);
  internal = 0xffffff^(0xff << ((output-1)*8));
//	internal = 0xffffff^internal;
  Data = Data & internal;
  SL_Data = 0xfffffff & ((code +invert*8 + latch*16)<< ((output-1)*8));
  SL_Data = Data | SL_Data;
  ret=CAMAC_register_write(hdev, 5, SL_Data);

// Write Delay and Gate length and set trigger
// Set coarse delay in DGG_Extended register for DGGA only
    ret = CAMAC_register_read(hdev,6,&SL_Data);
  if (channel ==0) // DGGA with coarse gain
  {
//		ret = CAMAC_register_read(hdev,7,&Data);
    Delay_ext = ((delay/0x10000) & 0xffff);
    Data = (gate<<16) + (delay & 0xffff);
    ret=CAMAC_register_write(hdev,7,Data);
    ret=CAMAC_register_write(hdev,13,Delay_ext);
      SL_Data = SL_Data & 0xff00ffff;
    Data = (trigger << 16);
    SL_Data = Data | SL_Data;
    ret = CAMAC_register_write(hdev, 6, SL_Data);
  }
  else // DGGB without corse gain
  {
    Data = (gate<<16) + (delay & 0xffff);
    ret=CAMAC_register_write(hdev,8,Data);
      SL_Data = SL_Data & 0x00ffffff;
    Data = (trigger << 24);
    SL_Data = Data | SL_Data;
    ret = CAMAC_register_write(hdev, 6, SL_Data);
    }
    return ret;
}

/*
******** CAMAC_LED_settings ************************

  Writes a data word to the CC-USB LED register

  Parameters:
    hdev: USB devcie handle returned from an open function
    LED: The number which corresponds to an LED values are:
            1 - for RED LED
            2 - for GREEN LED
            3 - for Yellow LED
    code: The LED aource selector code, valid values for each LED
          are listed in the manual
    invert: to invert the LED lighting
    latch: sets LED latch bit

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_LED_settings(usb_dev_handle *hdev, int LED, int code, int invert, int latch)
{
    short ret;
    long internal;
    long Data, W_Data;
//Set Output and options
    ret = CAMAC_register_read(hdev,4,&Data);
    internal = 0xffffff^(0xff << ((LED-1)*8));
//	internal = 0xffffff^internal;
    Data = Data & internal;
    W_Data = 0xfffffff & ((code +invert*8 + latch*16)<< ((LED-1)*8));
    W_Data = Data | W_Data;
    ret = CAMAC_register_write(hdev, 4, W_Data);
    return ret;
}

/*
******** CAMAC_Output_settings ************************

  Writes a data word to the CC-USB LED register

  Parameters:
    hdev: USB devcie handle returned from an open function
    Channel: The number which corresponds to an output:
            1 - for Output 1
            2 - for Output 2
            3 - for Output 3
    code: The Output selector code, valid values
          are listed in the manual
    invert: to invert the output
    latch: sets latch bit

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_Output_settings(usb_dev_handle *hdev, int output, int code, int invert, int latch)
{
    short ret;
    long internal;
    long Data, W_Data;
//Set Output and options
    ret = CAMAC_register_read(hdev,5,&Data);
    internal = 0xffffff^(0xff << ((output-1)*8));
    Data = Data & internal;
    W_Data = 0xfffffff & ((code +invert*8 + latch*16)<< ((output-1)*8));
    W_Data = Data | W_Data;
    ret=CAMAC_register_write(hdev, 5, W_Data);
    return ret;
}

/*
******** CAMAC_scaler_settings ************************

  Configures the internal CC-USB scaler (SelSource register)

  Parameters:
    hdev: USB devcie handle returned from an open function
    Channel: The number which corresponds to an output:
            0 - for Scaler A
            1 - for Scaler B
    code: The Output selector code, valid values
          are listed in the manual
    enable: =1 enables scaler
    latch: =1 resets the scaler at time of call

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_scaler_settings(usb_dev_handle *hdev, short channel, short trigger, int enable, int reset)
{
  long Data, W_Data;
  short ret;
  ret = CAMAC_register_read(hdev,6,&Data);
  if (channel ==0)
  {
    Data = Data & 0xffffff00;
    W_Data = (reset & 0x01)*0x20 + (enable & 0x01)*0x10 + trigger;
    W_Data = Data | W_Data;
    ret = CAMAC_register_write(hdev, 6, W_Data);
  }
  else
  {
    Data = Data & 0xffff00ff;
    W_Data = (reset & 0x01)*0x20 + (enable & 0x01)*0x10 + trigger;
    W_Data = Data | (W_Data << 8);
    ret = CAMAC_register_write(hdev, 6, W_Data);
    }
return ret;
}



/*
******** CAMAC_write_LAM_mask ************************

  Writes the data word to the CC-USB LAM mask register

  Parameters:
    hdev: USB devcie handle returned from an open function
    Data: LAM mask to write

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_write_LAM_mask(usb_dev_handle *hdev, long Data)
{
    short ret;
    ret = CAMAC_register_write(hdev, 9, Data);

    return ret;
}

/*
******** CAMAC_read_LAM_mask ************************

  Reads the data word from the LAM mask register

  Parameters:
    hdev: USB devcie handle returned from an open function
    Data: LAM mask to write

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_read_LAM_mask(usb_dev_handle *hdev, long *Data)
{
    long intbuf[4];
    int ret;
    int N = 25;
    int F = 0;
    int A = 9;

    // CAMAC direct read function
    intbuf[0]=1;
    intbuf[1]=(long)(F+A*32+N*512 + 0x4000);
    ret = xxusb_stack_execute(hdev, intbuf);
    *Data=intbuf[0] + (intbuf[1] & 255) * 0x10000;
  return ret;
}


//******************************************************//
//**************** EZ_CAMAC Functions ******************//
//******************************************************//
// The following are functions used to perform simple
// CAMAC Functions with the CC_USB


/*
******** CAMAC_write ************************

  Performs a CAMAC write using NAF commands

  Parameters:
    hdev: USB device handle returned from an open function
    N: CAMAC Station Number
    A: CAMAC Subaddress
    F: CAMAC Function (16...23)
    Q: The Q response from the CAMAC dataway
    X: The comment accepted response from CAMAC dataway

  Returns:
    Number of bytes written to xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_write(usb_dev_handle *hdev, int N, int A, int F, long Data, int *Q, int *X)
{
    long intbuf[4];
  int ret =-1;
// CAMAC direct write function
    intbuf[0]=1;
    intbuf[1]=(long)(F+A*32+N*512 + 0x4000);
    if ((F > 15) && (F < 24))
    {
  intbuf[0]=3;
  intbuf[2]=(Data & 0xffff);
  intbuf[3]=((Data >>16) & 255);
  ret = xxusb_stack_execute(hdev, intbuf);
  *Q = (intbuf[0] & 1);
  *X = ((intbuf[0] >> 1) & 1);
    }
    return ret;
}

/*
******** CAMAC_read ************************

  Performs a CAMAC read using NAF commands

  Parameters:
    hdev: USB device handle returned from an open function
    N: CAMAC Station Number
    A: CAMAC Subaddress
    F: CAMAC Function (F<16 or F>23)
    Q: The Q response from the CAMAC dataway
    X: The comment accepted response from CAMAC dataway

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_read(usb_dev_handle *hdev, int N, int A, int F, long *Data, int *Q, int *X)
{
  long intbuf[4];
  int ret =-1;
  *Data=0;
  if ((F < 16) || (F >23))
    {
      // CAMAC direct read function
      intbuf[0]=1;
      intbuf[1]=(long)(F+A*32+N*512 + 0x4000);
      ret = xxusb_stack_execute(hdev, intbuf);
      if (ret ==2)
      {
        *Q = (intbuf[0] & 1);
        *X = ((intbuf[0] >> 1) & 1);
      }
      if (ret ==4)
      {
        *Data=intbuf[0] + (intbuf[1] & 255) * 0x10000;   //24-bit word
        *Q = ((intbuf[1] >> 8) & 1);
        *X = ((intbuf[1] >> 9) & 1);
      }
    }
    return ret;
}

/*
******** CAMAC_Z ************************

  Performs a CAMAC init

  Parameters:
    hdev: USB device handle returned from an open function

  Returns:
    Number of bytes written to xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_Z(usb_dev_handle *hdev)
{
    long intbuf[4];
    int  ret;
//  CAMAC Z = N(28) A(8) F(29)
    intbuf[0]=1;
    intbuf[1]=(long)(29+8*32+28*512 + 0x4000);
    ret = xxusb_stack_execute(hdev, intbuf);
    return ret;
}

/*
******** CAMAC_C ************************

  Performs a CAMAC clear

  Parameters:
    hdev: USB device handle returned from an open function

  Returns:
    Number of bytes written to xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_C(usb_dev_handle *hdev)
{
    long intbuf[4];
    int ret;
    intbuf[0]=1;
    intbuf[1]=(long)(29+9*32+28*512 + 0x4000);
    ret = xxusb_stack_execute(hdev, intbuf);
    return ret;
}

/*
******** CAMAC_I ************************

  Set CAMAC inhibit

  Parameters:
    hdev: USB device handle returned from an open function

  Returns:
    Number of bytes written to xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_I(usb_dev_handle *hdev, int inhibit)
{
    long intbuf[4];
    int  ret;
    intbuf[0]=1;
    if (inhibit) intbuf[1]=(long)(24+9*32+29*512 + 0x4000);
    else intbuf[1]=(long)(26+9*32+29*512 + 0x4000);
    ret = xxusb_stack_execute(hdev, intbuf);
    return ret;
}

/*
******** CAMAC_block_read16 ************************

  Performs a 16-bit CAMAC block read using NAF commands with max 8kB size

  Parameters:
    hdev: USB device handle returned from an open function
    N: CAMAC Station Number
    A: CAMAC Subaddress
    F: CAMAC Function (F<16 or F>23)
    loops: number of read cycles (8kB buffer limit!!!)
    Data: Data array for block read

  Returns:
    Number of bytes read from xxusb when successful
    Upon failure, a negative number
*/
short  CAMAC_blockread16(usb_dev_handle *hdev, int N, int A, int F, int loops, int *Data)
{
  long intbuf[4096];
  int i, ret =-1;
  *Data=0;
// maximum block size is 8k => 4096 int data
  if (loops < 4096) loops=4096;
// only read F calls accepted
  if ((F < 16) || (F >23))
    {

      // CAMAC read function with 0x8000 for 16bit / 0xC000 for 24bit
      intbuf[0]=3;
      intbuf[1]=(long)(F+A*32+N*512 + 0x8000);
    intbuf[2]= 0x8040;
    intbuf[3]= loops;
    ret = xxusb_stack_execute(hdev, intbuf);
    for(i=0 ; i<loops ;i++)
    {Data[i] = intbuf[i];
    }
  }
    return ret;
}
