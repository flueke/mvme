// stack_list_buffer_example.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
/***************************************************************************/
/*                                                                         */
/*  Filename: stack_list_buffer_example.cpp						           */
/*                                                                         */
/*  Funktion:                                                              */
/*                                                                         */
/*  Autor:                TH                                               */
/*  date:                 03.10.2017                                       */
/*  last modification:    09.10.2017                                       */
/*                                                                         */
/* ----------------------------------------------------------------------- */
/*   use class_lib_version: 2.1                                            */
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
#include "project_system_define.h"		//define LINUX, MAC or WINDOWS


#ifdef WINDOWS
#include "stdafx.h"
#include <iostream>
#include <process.h>


#include <iomanip>
using namespace std;
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "wingetopt.h"

void usleep(unsigned int uint_usec)
{
	unsigned int msec;
	if (uint_usec <= 1000) {
		msec = 1;
	}
	else {
		msec = (uint_usec + 999) / 1000;
	}
	Sleep(msec);
}

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
//#pragma comment(lib, "wsock32.lib")
typedef int socklen_t;

long WinsockStartup()
{
	long rc;
	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD(2, 1);
	rc = WSAStartup(wVersionRequested, &wsaData);
	return rc;
}


bool gl_read_list_data_threadStop = false;
bool gl_read_list_data_threadFinished = false;
bool gl_read_list_data_threadBusy = false;

//void read_list_data_thread(void *param);
//void read_list_data_thread(LPVOID vme_list_device);
unsigned __stdcall read_list_data_thread(void* vme_list_device);


#endif


#ifdef LINUX
#include <sys/types.h>
#include <sys/socket.h>
#endif
BOOL gl_stopReq = FALSE;
//void program_stop_and_wait(void);
BOOL CtrlHandler(DWORD ctrlType);

#include "sis3153ETH_vme_class.h"
#include "sis3153eth.h"

//int _tmain(int argc, _TCHAR* argv[])
int main(int argc, char *argv[])
{
	unsigned int i;
	int int_ch;
	char ch_string[128];
	char  sis3153_ip_addr_string[32];
	unsigned int acquisition_time;
	unsigned int jumbo_frame_flag = 0;
	unsigned int multi_event_buffering_flag = 0;

	int return_code = 0;
	unsigned short ushort_data = 0;

	sis3153eth *vme_crate;
	sis3153eth *vme_list;
	unsigned int nof_found_devices;
	char char_messages[128];
	int udp_port;

	unsigned int stack_list_buffer_ptr;
	unsigned int direct_list_length;
	unsigned int marker_word;
	unsigned int addr, data;
	unsigned int request_nof_words;

	unsigned int *uint_write_buffer;
	unsigned int *uint_stack_list_buffer;

	unsigned int  stack_offset;
	unsigned int  stack_addr;
	unsigned int  stack_list_length;
	unsigned int  written_nof_words;

	if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE)) {
		printf("Error setting Console-Ctrl Handler\n");
		return -1;
	}



	// default parameters
	strcpy(sis3153_ip_addr_string, "192.168.1.100"); // SIS3153 IP address
	strcpy(sis3153_ip_addr_string, "212.60.16.21"); // SIS3153 IP address
	acquisition_time = 10;

	if (argc > 1) {

		while ((int_ch = getopt(argc, argv, "?h:I:T:JM")) != -1)

			switch (int_ch) {
			case 'I':
				sscanf(optarg, "%s", ch_string);
				//printf("-I %s    \n", ch_string);
				strcpy(sis3153_ip_addr_string, ch_string);
				break;
			case 'T':
				sscanf(optarg, "%d", &acquisition_time);
				break;
			case 'J':
				jumbo_frame_flag = 1;
				break;
			case 'M':
				multi_event_buffering_flag = 1;
				break;
			case '?':
			case 'h':
			default:
				printf("Usage: %s  [-?h]  [-I ip]  [-T time in seconds]  ", argv[0]);
				printf("   \n");
				printf("   -I string     SIS3153 IP Address		      Default = %s\n", sis3153_ip_addr_string);
				printf("   -T decimal    Acquisition Time in seconds  Default 10 seconds (0: endless until Cntrl.C)  \n");
				printf("   -M            Multi_Event bufering enable (else disable)  \n");
				printf("   -J            Jumbo frame enable (else disable)  \n");
				printf("   \n");
				printf("   -h            Print this message\n");
				printf("   \n");
				printf("   date: 2017-10-10 \n");
				exit(1);
			}

	} // if (argc > 1)


	/******************************************************************************************/
	/*                                                                                        */
	/*  open Vme Interface device(s)                                                          */
	/*                                                                                        */
	/******************************************************************************************/
	// open Vme Interface device
	sis3153eth(&vme_crate, sis3153_ip_addr_string);
	vme_crate->vmeopen();  // open Vme interface
	vme_crate->get_vmeopen_messages(char_messages, sizeof(char_messages), &nof_found_devices);  // open Vme interface
	if (nof_found_devices == 0) {
		printf("\n");
		printf("get_vmeopen_messages = %s , found NO sis3153 device \n", char_messages);
		printf("\n");
		vme_crate->vmeclose();
		return 1;
	}

	// open a second Vme Interface device for "List-handling" (with incremented udp-por numbert)
	sis3153eth(&vme_list, sis3153_ip_addr_string);
	vme_list->vmeopen();  // open Vme interface
	vme_list->get_vmeopen_messages(char_messages, sizeof(char_messages), &nof_found_devices);  // open Vme interface
	if (nof_found_devices == 0) {
		printf("\n");
		printf("get_vmeopen_messages = %s , found NO sis3153 device \n", char_messages);
		printf("\n");
		vme_list->vmeclose();
		return 1;
	}
	return_code = vme_crate->udp_sis3153_register_read(SIS3153ETH_MODID_VERSION, &data); //
	udp_port = vme_crate->get_UdpSocketPort();
	printf("vme_crate->udp_sis3153_register_read: \treturn_code = 0x%08X    data = 0x%08X   udp_port = 0x%08X\n", return_code, data, udp_port);

	return_code = vme_list->udp_sis3153_register_read(SIS3153ETH_MODID_VERSION, &data); //
	udp_port = vme_list->get_UdpSocketPort();
	printf("vme_list->udp_sis3153_register_read:  \treturn_code = 0x%08X    data = 0x%08X   udp_port = 0x%08X\n", return_code, data, udp_port);

	if (jumbo_frame_flag == 1) {
		vme_crate->set_UdpSocketEnableJumboFrame();
	}
	else {
		vme_crate->set_UdpSocketDisableJumboFrame();
	}
	vme_crate->set_UdpSocketReceiveNofPackagesPerRequest(8); // set max. packet for  each read request (default is only 1)
	//vme_crate->set_UdpSocketGapValue(2); // set gap time between packets to 1us
	vme_crate->set_UdpSocketGapValue(1); // set gap time between packets to 512ns

	/******************************************************************************************/
	/*                                                                                        */
	/*  Malloc Memory                                                                         */
	/*                                                                                        */
	/******************************************************************************************/

	// create DMA write buffer (test incremet pattern)
	uint_write_buffer = (unsigned int *)malloc(4 * 0x10000); // 64k x 32-bit
	if (uint_write_buffer == NULL) {
		printf("Error allocating uint_write_buffer  !\n");
		return -1;
	}

	// create List buffer
	uint_stack_list_buffer = (unsigned int *)malloc(4 * 8192); // 8k x 32-bit
	if (uint_stack_list_buffer == NULL) {
		printf("Error allocating uint_stack_list_buffer  !\n");
		return -1;
	}



	/******************************************************************************************/
	// prepare test increment pattern and write these values to the VME Memory at addr. 0x0
	for (i = 0; i < 0x10000; i++) {
		uint_write_buffer[i] = 0xdada0000 + i;
	}
	request_nof_words = 0x10000;
	addr = 0x0; // VME Memory slave
	return_code = vme_crate->vme_A32BLT32_write(addr, uint_write_buffer, request_nof_words, &written_nof_words);   //


	/******************************************************************************************/
	/*                                                                                        */
	/*  Create "Stack List"  (same as List as "Direct List" )                                 */
	/*                                                                                        */
	/******************************************************************************************/
	// first disable stack operation and Timer (in case that is enabled)
	data = 0x30000;			// disable stack operation and Timer
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_CONTROL, data); //

	/******************************************************************************************/


/**********************/
/*   List 1  (Timer)  */
/**********************/

	stack_list_buffer_ptr = 0; // start pointer of pc stack_list_buffer
	stack_offset = 0;  // List 1

	// start with Header !
	vme_list->list_generate_add_header(&stack_list_buffer_ptr, uint_stack_list_buffer); // start list with Header

  // begin of user list cycles

	// add marker word
	marker_word = 0xAFFEAFFE;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	// write internal register
	addr = SIS3153ETH_CONTROL_STATUS;
	data = 0x1; // set Led A
	vme_list->list_generate_add_register_write(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, data); //

	// read internal register
	addr = SIS3153ETH_MODID_VERSION;
	vme_list->list_generate_add_register_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

	// read internal register
	addr = SIS3153ETH_SERIAL_NUMBER_REG;
	vme_list->list_generate_add_register_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

	// add marker word
	marker_word = 0xDEADBEEF;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	// read VME A32D32
	addr = 0x31000004;  // SIS3316 version register
	vme_list->list_generate_add_vmeA32D32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

	 // write VME A32D32
	addr = 0x31000000;  // SIS3316 Control register
	data = 0x10001; // toggle Led U
	vme_list->list_generate_add_vmeA32D32_write(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, data); //


	// write internal register
	addr = SIS3153ETH_CONTROL_STATUS;
	data = 0x10000; // clr Led A
	vme_list->list_generate_add_register_write(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, data); //


	// write internal register
	addr = SIS3153ETH_STACK_LIST_CONTROL;
	data = 0x1000; // Set “Force to send rest of Buffer Enable” bit
	vme_list->list_generate_add_register_write(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, data); //

	// read internal register
	addr = SIS3153ETH_SERIAL_NUMBER_REG;
	vme_list->list_generate_add_register_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

#ifdef raus
																											// write internal register
	addr = SIS3153ETH_STACK_LIST_CONTROL;
	data = 0x10000000; // Clr “Force to send rest of Buffer Enable” bit
	vme_list->list_generate_add_register_write(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, data); //


	//
	addr = 0x0;
	request_nof_words = 0x164; //

	request_nof_words = 0x4; //
	vme_list->list_generate_add_vmeA32BLT32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, request_nof_words); //
#endif
  // end of user list cycles

	// stop with Trailer !
	vme_list->list_generate_add_trailer(&stack_list_buffer_ptr, uint_stack_list_buffer); // stop list with Trailer

	// write created list to stack memory
	stack_addr = SIS3153ETH_STACK_RAM_START_ADDR + stack_offset;
	stack_list_length = stack_list_buffer_ptr;
	return_code = vme_list->udp_sis3153_register_dma_write(stack_addr, uint_stack_list_buffer, stack_list_length, &written_nof_words); //
	printf("List 1: write to Stack Memory   stack_addr = 0x%08X \twritten_nof_words = 0x%08X    \treturn_code = 0x%08X \n", stack_addr, written_nof_words, return_code);

	// configure Stack-List 1 Configuration register
	data = stack_offset; // Stack-List 1 start address
	data = data + (((stack_list_length - 1) & 0xffff) << 16); // stack list length
	vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST1_CONFIG, data); //

	// Stack-List 1: select trigger source
	// data = 0xC; // enable trigger IN1 rising edge
	data = 8; // Timer 1
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST1_TRIGGER_SOURCE, data); //

	// setup Timer 1
	//data = 10000 - 1; // 10.000 * 100us -> 1 sec
	data = 5000 - 1; // 5.000 * 100us -> 0.5 sec
	//data = 2000 - 1; // 2.000 * 100us -> 0.2 sec
	//data = data + 0x80000000 ; // watchdog enable
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_TIMER1, data); //


	// prepare stack offset for next List
	stack_offset = stack_offset + stack_list_length;  // next free memory space for next List (can be defined in equal size for each List, also)

/**********************/
/*   end of List 1    */
/**********************/


/****************************************************************************************************************************************************/


/*********************/
/*   List 2  (IN 1)  */
/*********************/

	stack_list_buffer_ptr = 0; // start pointer of pc stack_list_buffer


	// start with Header !
	vme_list->list_generate_add_header(&stack_list_buffer_ptr, uint_stack_list_buffer); // start list with Header

  // begin of user list cycles

	// add marker word
	marker_word = 0xADD00001;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	// read VME A32D32 with saving the data for  dynamic block sizing read
	addr = 0x10;  //
	vme_list->list_generate_add_vmeA32D32_read_saveDynBlkSizingLength(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

	unsigned int dummy;
	addr = 0x000;
	dummy = 0x10; //
	vme_crate->list_generate_add_vmeA32BLT32_read_withDynBlkSizingLength(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, dummy); //


	addr = 0x31000004;  // SIS3316 version register
	vme_list->list_generate_add_vmeA32D32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //


	addr = 0x31000004;  // SIS3316 version register
	//vme_list->list_generate_add_vmeA32D32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

	addr = 0x31000004;  // SIS3316 version register
	//vme_list->list_generate_add_vmeA32D32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

	addr = 0x31000004;  // SIS3316 version register
	//vme_list->list_generate_add_vmeA32D32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr); //

  // end of user list cycles

	// stop with Trailer !
	vme_list->list_generate_add_trailer(&stack_list_buffer_ptr, uint_stack_list_buffer); // stop list with Trailer

	// write created list to stack memory
	stack_addr = SIS3153ETH_STACK_RAM_START_ADDR + stack_offset;
	stack_list_length = stack_list_buffer_ptr;
	return_code = vme_list->udp_sis3153_register_dma_write(stack_addr, uint_stack_list_buffer, stack_list_length, &written_nof_words); //
	printf("List 2: write to Stack Memory   stack_addr = 0x%08X \twritten_nof_words = 0x%08X    \treturn_code = 0x%08X \n", stack_addr, written_nof_words, return_code);

	// configure Stack-List 2 Configuration register
	data = stack_offset; // Stack-List 2 start address
	data = data + (( stack_list_length - 1) << 16); // stack list length
	vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST2_CONFIG, data); //

	// Stack-List 2: select trigger source
	data = 0xC; // enable trigger IN1 rising edge
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST2_TRIGGER_SOURCE, data); //


	// setup for dynamicallay block sizing read
	data = 0x020000ff;
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_DYN_BLK_SIZING_CONFIG, data);


	// prepare stack offset for next List
	stack_offset = stack_offset + stack_list_length;  // next free memory space for next List (can be defined in equal size for each List, also)

/**********************/
/*   end of List 2    */
/**********************/

/****************************************************************************************************************************************************/


/*********************/
/*   List 3  (IN 2)  */
/*********************/

	stack_list_buffer_ptr = 0; // start pointer of pc stack_list_buffer


	// start with Header !
	vme_list->list_generate_add_header(&stack_list_buffer_ptr, uint_stack_list_buffer); // start list with Header

  // begin of user list cycles

	// add marker word
	marker_word = 0xADD00002;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	addr = 0x000;
	request_nof_words = 0x1000; //  4096 long words
	//request_nof_words = 0x100; //  256 long words
	request_nof_words = 0x400; //  1024 long words
	request_nof_words = 0x10; //
	vme_crate->list_generate_add_vmeA32BLT32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, request_nof_words); //
																															// add marker word
	marker_word = 0xADD0EE02;
	//vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	addr = 0x100;
	request_nof_words = 0x10; //
	//vme_crate->list_generate_add_vmeA32BLT32_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, request_nof_words); //
	//vme_crate->list_generate_add_vmeA32MBLT64_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, request_nof_words); //

	 // add marker word
	marker_word = 0xADD0EE12;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	addr = 0x200;
	request_nof_words = 0x10; //
	vme_crate->list_generate_add_vmeA32MBLT64_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, request_nof_words); //
	vme_crate->list_generate_add_vmeA32MBLT64_swapDWord_read(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, request_nof_words); //


	// add marker word
	marker_word = 0xADD0EE22;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

  // end of user list cycles

	// stop with Trailer !
	vme_list->list_generate_add_trailer(&stack_list_buffer_ptr, uint_stack_list_buffer); // stop list with Trailer

	// write created list to stack memory
	stack_addr = SIS3153ETH_STACK_RAM_START_ADDR + stack_offset;
	stack_list_length = stack_list_buffer_ptr;
	return_code = vme_list->udp_sis3153_register_dma_write(stack_addr, uint_stack_list_buffer, stack_list_length, &written_nof_words); //
	printf("List 3: write to Stack Memory   stack_addr = 0x%08X \twritten_nof_words = 0x%08X    \treturn_code = 0x%08X \n", stack_addr, written_nof_words, return_code);

	// configure Stack-List 3 Configuration register
	data = stack_offset; // Stack-List 2 start address
	data = data + (( (stack_list_length - 1) & 0xffff) << 16); // stack list length
	vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST3_CONFIG, data); //

	// Stack-List 3: select trigger source
	data = 0xE; // enable trigger IN2 rising edge
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST3_TRIGGER_SOURCE, data); //


	// prepare stack offset for next List
	stack_offset = stack_offset + stack_list_length;  // next free memory space for next List (can be defined in equal size for each List, also)

/**********************/
/*   end of List 3    */
/**********************/




/************************/
/*   List 4  (Timer 2)  */
/************************/

	stack_list_buffer_ptr = 0; // start pointer of pc stack_list_buffer

	// start with Header !
	vme_list->list_generate_add_header(&stack_list_buffer_ptr, uint_stack_list_buffer); // start list with Header

	// begin of user list cycles
	// add marker word
	marker_word = 0xABBA0001;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word

	// write internal register
	addr = SIS3153ETH_STACK_LIST_TRIGGER_CMD;
	data = 15; // force to flush list buffer
	vme_list->list_generate_add_register_write(&stack_list_buffer_ptr, uint_stack_list_buffer, addr, data); //

	// add marker word
	marker_word = 0xABBA0002;
	vme_list->list_generate_add_marker(&stack_list_buffer_ptr, uint_stack_list_buffer, marker_word); // add marker word


	// stop with Trailer !
	vme_list->list_generate_add_trailer(&stack_list_buffer_ptr, uint_stack_list_buffer); // stop list with Trailer

  // write created list to stack memory
	stack_addr = SIS3153ETH_STACK_RAM_START_ADDR + stack_offset;
	stack_list_length = stack_list_buffer_ptr;
	return_code = vme_list->udp_sis3153_register_dma_write(stack_addr, uint_stack_list_buffer, stack_list_length, &written_nof_words); //
	printf("List 4: write to Stack Memory   stack_addr = 0x%08X \twritten_nof_words = 0x%08X    \treturn_code = 0x%08X \n", stack_addr, written_nof_words, return_code);

	// configure Stack-List 4 Configuration register
	data = stack_offset; // Stack-List 4 start address
	data = data + (((stack_list_length - 1) & 0xffff) << 16); // stack list length
	vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST4_CONFIG, data); //

	// Stack-List 4: select trigger source
	data = 9; // Timer 2
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST4_TRIGGER_SOURCE, data); //

	// setup Timer 2
	data = 50000 - 1; // 50.000 * 100us -> 5 sec
	//data = 10000 - 1; // 10.000 * 100us -> 1 sec
	//data = 2000 - 1; // 2.000 * 100us -> 0.2 sec
	//data = 500 - 1; // 1.000 * 100us -> 50 msec
	data = data + 0x80000000 ; // watchdog enable
	return_code = vme_list->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_TIMER2, data); //


	// prepare stack offset for next List
	stack_offset = stack_offset + stack_list_length;  // next free memory space for next List (can be defined in equal size for each List, also)

	/**********************/
	/*   end of List 4    */
	/**********************/




	/******************************************************************************************/

	/******************************************************************************************/
	/*                                                                                        */
	/* Start/Create read-thread                                                               */
	/*                                                                                        */
	/******************************************************************************************/

	HANDLE read_list_data_thread_Handle;
	unsigned int read_list_data_thread_Id;

	// starting thread
	printf("Starting read - thread\n");
	read_list_data_thread_Handle = (HANDLE)_beginthreadex(NULL, 0, read_list_data_thread, vme_list, 0, &read_list_data_thread_Id);
	/******************************************************************************************/

	/******************************************************************************************/
	/*                                                                                        */
	/* Enable List Operation                                                                  */
	/*                                                                                        */
	/******************************************************************************************/

	// clear List execution counter (List-Event number)
	return_code = vme_crate->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_TRIGGER_CMD, 8);

	printf("enable stack-list operation\n");
	// enable stack operation
	data = 0x1;			// enable stack operation
	data = data + 0x2;  // enable Timer 1 operation
	data = data + 0x4;  // enable Timer 2 operation
	if (multi_event_buffering_flag == 1) {
		data = data + 0x8000;  // enable List Buffer mode
	}
	return_code = vme_crate->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_CONTROL, data); // enable stack operation

	//
	if (acquisition_time == 0) {
		printf("stop acquisition with Ctrl. C \n");
	}
	else {
		printf("acquisition stops after %d seconds or with Ctrl. C\n", acquisition_time);
	}

	bool acquisition_run_flag;
	unsigned int acquisition_run_time;
	acquisition_run_time = 0;
	do {
		usleep(1000000); // 1 seconds
		acquisition_run_time++;
		if (acquisition_time == 0) {
			acquisition_run_flag = true;
		}
		else {
			if (acquisition_time > acquisition_run_time) {
				acquisition_run_flag = true;
			}
			else {
				acquisition_run_flag = false;
			}
		}
	} while ((gl_stopReq == false) && (acquisition_run_flag == true) );


	printf("disable stack operation\n");
	// disable stack operation
	data = 0xf0000;			    // disable stack operation
	data = data + 0x80000000;   // disable List Buffer mode
	return_code = vme_crate->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_CONTROL, data); //




	// read cycle to request the logic to push the last events of the List buffer to the UDP interface	// read internal register
	 //return_code = vme_crate->udp_sis3153_register_read(SIS3153ETH_MODID_VERSION, &data); //
	unsigned rest_list_buffer_length;

	return_code = vme_crate->udp_sis3153_register_read(SIS3153ETH_STACK_LIST_CONTROL, &data); //
	rest_list_buffer_length = ((data >> 16) & 0xfff);
	printf("List-Buffer rest length = 0x%08X  %d  \n", rest_list_buffer_length, rest_list_buffer_length);

	printf("force to push List-Buffer\n");
	data = 0x1000;			    // set "force to push List-Buffer"
	return_code = vme_crate->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_CONTROL, data); //
	data = 0x10000000;			    // set "force to push List-Buffer"
	return_code = vme_crate->udp_sis3153_register_write(SIS3153ETH_STACK_LIST_CONTROL, data); //

	do {
		return_code = vme_crate->udp_sis3153_register_read(SIS3153ETH_STACK_LIST_CONTROL, &data); //
		rest_list_buffer_length = ((data >> 16) & 0xfff);
	} while (rest_list_buffer_length > 0);

	gl_read_list_data_threadStop = true;
	do {
		usleep(10); //
	} while (gl_read_list_data_threadFinished == false);

	printf("List-Buffer rest length = 0x%08X  %d  \n", rest_list_buffer_length, rest_list_buffer_length);

	printf("End\n");
	vme_crate->vmeclose();
	vme_list->vmeclose();
	return 0;
}




/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/
unsigned __stdcall read_list_data_thread(void* vme_list_device)
//void read_list_data_thread(LPVOID vme_list_device)
{
	sis3153eth *vme_list_handle;
	unsigned int return_code;
	int udp_port;
	int rc;
	int i;
	int i_offset;
	unsigned int data;

	unsigned char packet_ack;
	unsigned char packet_ident;
	unsigned char packet_status;
	unsigned int got_nof_event_data;
	unsigned int loop_counter;

	unsigned int event_list_length;
	unsigned char event_list_packet_ack;


	bool stop_flag = false;
	bool udp_timeout_flag = false;


	/******************************************************************************************/
	/*                                                                                        */
	/*  Malloc Memory                                                                         */
	/*                                                                                        */
	/******************************************************************************************/
	// create List udp read buffer
	unsigned int *uint_read_udp_list_buffer;
	gl_read_list_data_threadFinished = false;

	uint_read_udp_list_buffer = (unsigned int *)malloc(4 * 0x1000); //  16 KByte
	if (uint_read_udp_list_buffer == NULL) {
		printf("\tThread: Error allocating uint_stack_list_buffer  !\n");
		gl_read_list_data_threadFinished = true;
		gl_read_list_data_threadBusy = false;
		_endthreadex(0);
		return 0;
 	}


	loop_counter = 0;

	vme_list_handle = (sis3153eth*) vme_list_device;
	return_code = vme_list_handle->udp_sis3153_register_read(SIS3153ETH_MODID_VERSION, &data); //
	udp_port = vme_list_handle->get_UdpSocketPort();
	printf("\tThread: vme_list_handle->udp_sis3153_register_read:  \treturn_code = 0x%08X    data = 0x%08X   udp_port = 0x%08X\n", return_code, data, udp_port);
	usleep(1);

	do {
		// read
		rc = vme_list_handle->list_read_event(&packet_ack, &packet_ident, &packet_status, uint_read_udp_list_buffer, &got_nof_event_data);
		if (rc < 0) { // timeout
			udp_timeout_flag = true;
			//printf("Timeout list_read_event : \treturn_code = 0x%08X   (%d)  \n", return_code, return_code);
		}
		else {
			udp_timeout_flag = false;
			gl_read_list_data_threadBusy = true;
			if (packet_ack == 0x60) { // Multi-Event Packet

				printf("\tThread: list_read_event (multi) : \treturn_code = 0x%08X   (%d)  \t0x%02X  got_nof_data = %d\n", rc, rc, (unsigned char)packet_ack, got_nof_event_data);
				i_offset = 0;
				loop_counter = 0;
#ifdef raus
				do {
					event_list_packet_ack = (unsigned char) uint_read_udp_list_buffer[i_offset] & 0xff;
					event_list_length     = ((uint_read_udp_list_buffer[i_offset] & 0xff0000) >> 16) + (uint_read_udp_list_buffer[i_offset] & 0xff00);
					printf("\tlist_read_event (multi) : \t0x%02X  \tevent_list_length = 0x%08X   (%d)  \n", (unsigned char)event_list_packet_ack, event_list_length, event_list_length);
					for (i = 0; i < event_list_length; i++) {
						printf("\ti = %d	\tdata = 0x%08X    \n", i, uint_read_udp_list_buffer[i_offset + i + 1]);
					}

					i_offset = i_offset + event_list_length  + 1;
					printf("\ti_offset:  = 0x%08X   (%d)  \n", i_offset, i_offset);
				} while (i_offset<got_nof_event_data);
					//loop_counter++;
				//} while (loop_counter < 2);
#endif
			}
			else { // Single-Event Packet
				printf("\tThread: list_read_event (single) : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X   got_nof_event_data = %d\n", rc, rc, (unsigned char)packet_ack, (unsigned char)packet_ident, (unsigned char)packet_status, got_nof_event_data);
				for (i = 0; i < got_nof_event_data; i++) {
					printf("\tThread: i = %d	\tdata = 0x%08X    \n", i, uint_read_udp_list_buffer[i]);
				}
			}
			printf("\n");
			loop_counter++;
			gl_read_list_data_threadBusy = false;
		}
		//printf("gl_read_list_data_threadStop = %d  udp_timeout_flag = %d     \n", gl_read_list_data_threadStop, udp_timeout_flag);

	} while ((gl_read_list_data_threadStop == false) || (udp_timeout_flag == false));


	printf("\tThread: Thread function ends\n");
	gl_read_list_data_threadFinished = true;
	gl_read_list_data_threadBusy = false;
	_endthreadex(0);
	return 0;
}





/***************************************************/
#ifdef raus
void program_stop_and_wait(void)
{
	gl_stopReq = FALSE;
	printf("\n\nProgram stopped");
	printf("\n\nEnter ctrl C");
	do {
		Sleep(1);
	} while (gl_stopReq == FALSE);

	//		result = scanf( "%s", line_in );
}
#endif

BOOL CtrlHandler(DWORD ctrlType) {
	switch (ctrlType) {
	case CTRL_C_EVENT:
		printf("\n\nCTRL-C pressed. finishing current task.\n\n");
		gl_stopReq = TRUE;
		return(TRUE);
		break;
	default:
		printf("\n\ndefault pressed. \n\n");
		return(FALSE);
		break;
	}
}
