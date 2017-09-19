//============================================================================
// Name        : sis3153eth_access_test.cpp
// Author      : th
// Version     :
// Copyright   : Your copyright notice
// Description :  
//============================================================================
#include "project_system_define.h"		//define LINUX or WINDOWS
#include "project_interface_define.h"   //define Interface (sis1100/sis310x, sis3150usb or Ethernet UDP)




#ifdef WINDOWS
	#include <iostream>
	#include <iomanip>
	using namespace std;
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <winsock2.h>

	#include <stdlib.h>
	#include <string.h>
	#include <math.h>
	#include "wingetopt.h" 

	void usleep(unsigned int uint_usec) ;

#endif

	 

#ifdef ETHERNET_VME_INTERFACE
	#include "sis3153ETH_vme_class.h"
	sis3153eth *gl_vme_crate;
	//char  gl_sis3153_ip_addr_string[32] = "212.60.16.90";
	//	char  gl_sis3153_ip_addr_string[32] = "192.168.1.11";

	#ifdef LINUX
		#include <sys/types.h>
		#include <sys/socket.h>
	#endif

	#ifdef WINDOWS
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

	  rc = WSAStartup( wVersionRequested, &wsaData );
	  return rc;
	}
 
	#endif
#endif



#include "sis3153usb.h"

unsigned int gl_dma_buffer[0x100000] ;

int main(int argc, char *argv[])
{

	unsigned int write_register_length ;
	unsigned int write_addr_array[256] ;
	unsigned int write_data_array[256] ;
	unsigned short ushort_write_data_array[256] ;
	unsigned char uchar_write_data_array[256] ;
	unsigned int read_register_length ;
	unsigned int read_address_array[64] ;
	unsigned int read_data_array[64] ;
	unsigned short ushort_read_data_array[256] ;
	unsigned char uchar_read_data_array[256] ;

	unsigned int addr;
	unsigned int data ;
	unsigned int i ;
	unsigned int request_nof_words ;
	unsigned int got_nof_words ;
	unsigned int written_nof_words ;

	unsigned char uchar_data  ;
	unsigned short ushort_data  ;

	cout << "sis3153eth_access_test" << endl; // prints sis3316_access_test_sis3153eth

	   //char char_command[256];
	char  ip_addr_string[32] ;
	unsigned int vme_base_address ;
	char ch_string[64] ;
	int int_ch ;
	int return_code ;

	unsigned int prot_error_counter ;
	unsigned int data_error_counter ;
	unsigned int loop_counter ;
	unsigned int max_nof_packets_per_read ;

	max_nof_packets_per_read = 1 ; // max = 32
	// default
	vme_base_address = 0x30000000 ;
	//strcpy(ip_addr_string,"212.60.16.2") ; // SIS3153 IP address
	//strcpy(ip_addr_string,"212.60.16.90") ; // SIS3153 IP address
	strcpy_s(ip_addr_string,"sis3153-0015") ; // SIS3153 IP address

	   if (argc > 1) {
	#ifdef raus
		   /* Save command line into string "command" */
		   memset(char_command,0,sizeof(char_command));
		   for (i=1;i<argc;i++) {
				strcat(char_command,argv[i]);
				strcat(char_command," ");
			}
			printf("gl_command %s    \n", char_command);
	#endif


			while ((int_ch = getopt(argc, argv, "?hI:")) != -1)
				switch (int_ch) {
				  case 'I':
						sscanf(optarg,"%s", ch_string) ;
						printf("-I %s    \n", ch_string );
						strcpy(ip_addr_string,ch_string) ;
						break;
				  case 'X':
					sscanf(optarg,"%X", &vme_base_address) ;
					break;
				  case '?':
				  case 'h':
				  default:
						printf("   \n");
					printf("Usage: %s  [-?h] [-I ip]  ", argv[0]);
					printf("   \n");
					printf("   \n");
				    printf("   -I string     SIS3153 IP Address       	Default = %s\n", ip_addr_string);
					printf("   \n");
					printf("   -h            Print this message\n");
					printf("   \n");
					exit(1);
				  }

		 } // if (argc > 1)
		printf("\n");




#ifdef ETHERNET_VME_INTERFACE
	sis3153eth *vme_crate;
	sis3153eth(&vme_crate, ip_addr_string);
#endif


	char char_messages[128] ;
	unsigned int nof_found_devices ;

	// open Vme Interface device
	return_code = vme_crate->vmeopen ();  // open Vme interface
	vme_crate->get_vmeopen_messages (char_messages, &nof_found_devices);  // open Vme interface
    printf("get_vmeopen_messages = %s , nof_found_devices %d \n",char_messages, nof_found_devices);

	printf("\n");
	unsigned int sis3153eth_class_lib_version;
	sis3153eth_class_lib_version = vme_crate->get_class_lib_version(); //
	printf("sis3153eth_class_lib_version %02X.%02X \n", ((sis3153eth_class_lib_version >> 8) & 0xff), (sis3153eth_class_lib_version & 0xff));
	printf("\n");
	printf("\n");

	prot_error_counter = 0 ;
	data_error_counter = 0  ;
	loop_counter = 0  ;

	printf("\n");

	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_MODID_VERSION, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_MODID_VERSION, data,return_code);
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_SERIAL_NUMBER_REG, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_SERIAL_NUMBER_REG, data,return_code);


#define SGL_RNDM_BURST_CYCLES
#ifdef SGL_RNDM_BURST_CYCLES
// A32D8 Burst write
	// setup burst write
	write_register_length  = 8 ;

		write_addr_array[0]        = 0x100  ; // Memory address
		uchar_write_data_array[0] = 0x11 ; // increment pattern
		write_addr_array[1]        = 0x101  ; // Memory address
		uchar_write_data_array[1] = 0x22 ; // increment pattern
		write_addr_array[2]        = 0x102  ; // Memory address
		uchar_write_data_array[2] = 0x33 ; // increment pattern
		write_addr_array[3]        = 0x103  ; // Memory address
		uchar_write_data_array[3] = 0x44 ; // increment pattern

		write_addr_array[4]        = 0x104  ; // Memory address
		uchar_write_data_array[4] = 0x55 ; // increment pattern
		write_addr_array[5]        = 0x106  ; // Memory address
		uchar_write_data_array[5] = 0x66 ; // increment pattern
		write_addr_array[6]        = 0x109  ; // Memory address
		uchar_write_data_array[6] = 0xaa ; // increment pattern
		write_addr_array[7]        = 0x10B  ; // Memory address
		uchar_write_data_array[7] = 0x44 ; // increment pattern

		
//while(0) {
	return_code = vme_crate->vme_A32D8_burst_write (write_register_length, write_addr_array, uchar_write_data_array); //

//}



// A32D16 Burst write
	// setup burst write
	write_register_length  = 8 ;

		write_addr_array[0]        = 0x100  ; // Memory address
		ushort_write_data_array[0] = 0x5555 ; // increment pattern
		write_addr_array[1]        = 0x202  ; // Memory address
		ushort_write_data_array[1] = 0x6666 ; // increment pattern
		write_addr_array[2]        = 0x300  ; // Memory address
		ushort_write_data_array[2] = 0x7777 ; // increment pattern
		write_addr_array[3]        = 0x402  ; // Memory address
		ushort_write_data_array[3] = 0x8888 ; // increment pattern

		write_addr_array[4]        = 0x102  ; // Memory address
		ushort_write_data_array[4] = 0x1234 ; // increment pattern
		write_addr_array[5]        = 0x200  ; // Memory address
		ushort_write_data_array[5] = 0x2222 ; // increment pattern
		write_addr_array[6]        = 0x302  ; // Memory address
		ushort_write_data_array[6] = 0x1111 ; // increment pattern
		write_addr_array[7]        = 0x400  ; // Memory address
		ushort_write_data_array[7] = 0x3333 ; // increment pattern


//while(0) {
	return_code = vme_crate->vme_A32D16_burst_write (write_register_length, write_addr_array, ushort_write_data_array); //
//}


// A32D32 Burst write
	// setup burst write
	write_register_length  = 8 ;
		write_addr_array[0] = 0x100  ; // Memory address
		write_data_array[0] = 0x21212121 ; // increment pattern
		write_addr_array[1] = 0x200  ; // Memory address
		write_data_array[1] = 0x32322222 ; // increment pattern
		write_addr_array[2] = 0x300  ; // Memory address
		write_data_array[2] = 0x33334333 ; // increment pattern
		write_addr_array[3] = 0x400  ; // Memory address
		write_data_array[3] = 0x44444445 ; // increment pattern
		write_addr_array[4] = 0x104  ; // Memory address
		write_data_array[4] = 0x44212121 ; // increment pattern
		write_addr_array[5] = 0x204  ; // Memory address
		write_data_array[5] = 0x55322222 ; // increment pattern
		write_addr_array[6] = 0x304  ; // Memory address
		write_data_array[6] = 0x66334333 ; // increment pattern
		write_addr_array[7] = 0x404  ; // Memory address
		write_data_array[7] = 0x77444445 ; // increment pattern
//while(1) {
	return_code = vme_crate->vme_A32D32_burst_write (write_register_length, write_addr_array, write_data_array); //
//}
 

// A32D8 Burst read
	// setup burst write
	read_register_length  = 8 ;
		read_address_array[0] = 0x100  ; // Memory address
		read_address_array[1] = 0x102  ; // Memory address
		read_address_array[2] = 0x101  ; // Memory address
		read_address_array[3] = 0x103  ; // Memory address
		read_address_array[4] = 0x200  ; // Memory address
		read_address_array[5] = 0x201  ; // Memory address
		read_address_array[6] = 0x202  ; // Memory address
		read_address_array[7] = 0x203  ; // Memory address
while(1) {
	return_code = vme_crate->vme_A32D8_burst_read (read_register_length, read_address_array, uchar_read_data_array); //
	printf("\n");
	printf("vme_A32D8_burst_read: return_code = 0x%08X \n", return_code);
	if(return_code == 0) {
		for (i=0;i<read_register_length; i++) {
				printf("0x%02X  ",  uchar_read_data_array[i]);
				if((i&0x7) == 0x7) printf("\n");
		}
	}
	printf("\n");
}


// A32D16 Burst read
	// setup burst write
	read_register_length  = 8 ;
		read_address_array[0] = 0x180  ; // Memory address
		read_address_array[1] = 0x200  ; // Memory address
		read_address_array[2] = 0x300  ; // Memory address
		read_address_array[3] = 0x400  ; // Memory address
		read_address_array[4] = 0x182  ; // Memory address
		read_address_array[5] = 0x202  ; // Memory address
		read_address_array[6] = 0x302  ; // Memory address
		read_address_array[7] = 0x402  ; // Memory address
while(0) {
	return_code = vme_crate->vme_A32D16_burst_read (read_register_length, read_address_array, ushort_read_data_array); //
	printf("\n");
	printf("vme_A32D16_burst_read: return_code = 0x%08X \n", return_code);
	if(return_code == 0) {
		for (i=0;i<read_register_length; i++) {
				printf("0x%04X  ",  ushort_read_data_array[i]);
				if((i&0x7) == 0x7) printf("\n");
		}
	}
	printf("\n");
}

// A32D32 Burst read
	// setup burst write
	read_register_length  = 4 ;
		read_address_array[0] = 0x180  ; // Memory address
		read_address_array[1] = 0x200  ; // Memory address
		read_address_array[2] = 0x300  ; // Memory address
		read_address_array[3] = 0x400  ; // Memory address
while(1) {
	return_code = vme_crate->vme_A32D32_burst_read (read_register_length, read_address_array, read_data_array); //
	printf("\n");
	printf("vme_A32D32_burst_read: return_code = 0x%08X \n", return_code);
	if(return_code == 0) {
		for (i=0;i<read_register_length; i++) {
				printf("0x%08X  ",  read_data_array[i]);
				if((i&0x7) == 0x7) printf("\n");
		}
	}
	printf("\n");
}


#endif   //#define SGL_RNDM_BURST_CYCLES





#ifdef raus
	printf("\n");
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_VME_MASTER_CONTROL_STATUS, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_VME_MASTER_CONTROL_STATUS, data,return_code);
	return_code = vme_crate->udp_sis3153_register_write (SIS3153USB_VME_MASTER_CONTROL_STATUS, 0xc0); //
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_VME_MASTER_CONTROL_STATUS, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_VME_MASTER_CONTROL_STATUS, data,return_code);
#endif

#ifdef raus
	for (i=0; i<32;i++) {
		write_data_array[i] = 0x12345600 + (  i) ; // increment pattern
	}

do {
	return_code = vme_crate->udp_sis3153_register_dma_write (0x1800000, write_data_array, 0x4, &written_nof_words); //
	printf("udp_sis3153_register_dma_write: \twritten_nof_words = 0x%08X    \treturn_code = 0x%08X \n", written_nof_words, return_code);
} while(0) ;
#endif

	printf("\n");

		addr = 0x00000078 ;
		data = 0x12233445 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 
	printf("vme_A32D32_write: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

		addr = 0x00000078 ;
		return_code = vme_crate->vme_A32D32_read (addr, &data); 
	printf("vme_A32D32_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);


		addr = 0x00000078 ;
		data = 0x12233445 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 
	printf("vme_A32D32_write: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

		addr = 0x00000078 ;
		return_code = vme_crate->vme_A32D32_read (addr, &data); 
	printf("vme_A32D32_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);


	//write_register_length  = 2 ;
	// setup burst write
	//for (i=0; i<32;i++) {
	//	write_addr_array[i] = 0x100 + (4 * i) ; // Memory address
	//	write_data_array[i] = 0x12345500 + (  i) ; // increment pattern
	//}

	char* char_dma_ptr;
	char_dma_ptr = (char*)gl_dma_buffer ;
	//write_register_length  = 2 ;




/***********************************************************************************************************************************************************/
/***********************************************************************************************************************************************************/


	unsigned int vme_write_flag ;
	unsigned int vme_fifo_mode ;
	unsigned int vme_access_size ;
	unsigned int vme_am_mode ;
	unsigned int vme_nof_bytes ;
	unsigned int stack_length ;

// set VME Berr to 1.25us to see it with chip-scope (debuging)
	return_code = vme_crate->udp_sis3153_register_write (SIS3153USB_VME_MASTER_CONTROL_STATUS, 0xC0000000); // BERR = 1,25us

#define STACK_LIST_EXECUTION
#ifdef STACK_LIST_EXECUTION


	return_code = vme_crate->udp_sis3153_register_write (0x1000001, 0x0); // stack 1 control

	stack_length = 0 ;
// header , must be written at first command
	write_data_array[stack_length+0] = 0xAAAA9000; // header
	write_data_array[stack_length+1] = 0x44556677;  // dummy
	stack_length = stack_length + 2 ;

#define MARKER
#ifdef MARKER
// marker
	write_data_array[stack_length+0] = 0xAAAA8000; // marker
	write_data_array[stack_length+1] = 0x11111111;
	stack_length = stack_length + 2 ;
#endif 

//#define INTERNAL_REGISTER
#ifdef INTERNAL_REGISTER

// internal register write: led toggle
	write_data_array[stack_length+0] = 0xAAAA1A00; // internal register / write / 4byte-size
	write_data_array[stack_length+1] = 0x00000001; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	write_data_array[stack_length+3] = 0x00010001; // data
	stack_length = stack_length + 4 ;

// internal register read ident
	write_data_array[stack_length+0] = 0xAAAA1200; // internal register / write / 4byte-size
	write_data_array[stack_length+1] = 0x00000001; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000001; // addr
	stack_length = stack_length + 3 ;
#endif 

//#define VME_STACK_CYCLES
//#ifdef VME_STACK_CYCLES

#ifdef VME_STACK_VME_WRITE
// VME write: A32 D32
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_nof_bytes    = 4 ;

// VME write: A32 D16
	vme_access_size  = 1 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_nof_bytes    = 2 ;

// VME write: A32 D8
	vme_access_size  = 0 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_nof_bytes    = 2 ;


// VME write: A32 D32
	vme_write_flag   = 1 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	write_data_array[stack_length+3] = 0x00010001; // data
	printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 4 ;

#endif 


//#define VME_STACK_VME_READ
#ifdef VME_STACK_VME_READ
// VME write: A32 D32
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_nof_bytes    = 4 ;

// VME write: A32 D16
	vme_access_size  = 1 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_nof_bytes    = 2 ;

// VME write: A32 D8
	vme_access_size  = 0 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_nof_bytes    = 2 ;


	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;


#endif 




#define VME_STACK_VME_MBLT_READ
#ifdef VME_STACK_VME_MBLT_READ
// VME write: A32 D32

// VME write: A32 D8


	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 3 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x8; // MBLT64
	vme_nof_bytes    = 0x3C ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x007ffff0; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;



	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 3 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x8; // MBLT64
	vme_nof_bytes    = 0x3C ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;

#endif


#define VME_STACK_VME_BERR_WRITE
#ifdef VME_STACK_VME_BERR_WRITE


// VME write: A32 D32
	vme_write_flag   = 1 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x10000000; // addr
	write_data_array[stack_length+3] = 0x00010001; // data
	printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 4 ;


#endif


#define VME_STACK_VME_BERR_READ
#ifdef VME_STACK_VME_BERR_READ
 
	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x10000000; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x10000004; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;


 
#endif

 



// trailer, must be written at last command
	write_data_array[stack_length+0] = 0xAAAAA000; // trailer
	write_data_array[stack_length+1] = 0x0;
	stack_length = stack_length + 2 ;



// write created list to stack memory
	stack_length = stack_length - 1 ; 
	return_code = vme_crate->udp_sis3153_register_dma_write (0x1800000, write_data_array, stack_length, &written_nof_words); //
	printf("udp_sis3153_register_dma_write: \twritten_nof_words = 0x%08X    \treturn_code = 0x%08X \n", written_nof_words, return_code);

// stack 1: write start address and length
	data = 0 ; // stack start address
	data = data + ((stack_length & 0xffff) << 16) ; // stack length
	return_code = vme_crate->udp_sis3153_register_write (0x1000000, data); // stack 1 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x1000002, data); // stack 2 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x1000004, data); // stack 3 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x1000006, data); // stack 4 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x1000008, data); // stack 5 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x100000A, data); // stack 6 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x100000B, data); // stack 7 length /start address = 0
	return_code = vme_crate->udp_sis3153_register_write (0x100000C, data); // stack 8 length /start address = 0

// stack 1: select trigger source
	return_code = vme_crate->udp_sis3153_register_write (0x1000001, 0xC); // stack 1: enable trigger IN1 rising edge
	return_code = vme_crate->udp_sis3153_register_write (0x1000003, 0xE); // stack 2: enable trigger IN2 rising edge
	return_code = vme_crate->udp_sis3153_register_write (0x1000005, 0xA); // stack 3: enable trigger cmd
	return_code = vme_crate->udp_sis3153_register_write (0x1000007, 0xA); // stack 4: enable trigger cmd
	return_code = vme_crate->udp_sis3153_register_write (0x1000009, 0xA); // stack 5: enable trigger cmd
	return_code = vme_crate->udp_sis3153_register_write (0x100000B, 0xA); // stack 6: enable trigger cmd
	return_code = vme_crate->udp_sis3153_register_write (0x100000D, 0x8); // stack 7: timer1
	return_code = vme_crate->udp_sis3153_register_write (0x100000F, 0x9); // stack 8: timer2


	//return_code = vme_crate->udp_sis3153_register_write (0x1000001, 0x0); // stack 1: no
	//return_code = vme_crate->udp_sis3153_register_write (0x1000003, 0x1); // stack 2: VME IRQ 1
	//return_code = vme_crate->udp_sis3153_register_write (0x1000005, 0x2); // stack 3: VME IRQ 2
	//return_code = vme_crate->udp_sis3153_register_write (0x1000007, 0x3); // stack 4: VME IRQ 3
	//return_code = vme_crate->udp_sis3153_register_write (0x1000009, 0x4); // stack 5: VME IRQ 4
	//return_code = vme_crate->udp_sis3153_register_write (0x100000B, 0x5); // stack 6: VME IRQ 5
	//return_code = vme_crate->udp_sis3153_register_write (0x100000D, 0x6); // stack 7: VME IRQ 6
	//return_code = vme_crate->udp_sis3153_register_write (0x100000F, 0x7); // stack 8: VME IRQ 7

// enable stack operation
	return_code = vme_crate->udp_sis3153_register_write (0x1000010, 0x001); // enable stack operation


	do {
		return_code = vme_crate->udp_read_packet (char_dma_ptr); //
		if(return_code > 0) {
			printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code,  (unsigned char)char_dma_ptr[0],  (unsigned char)char_dma_ptr[1],  (unsigned char)char_dma_ptr[2]);
		}
		if(return_code > 3) {
		//printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code,  (unsigned char)char_dma_ptr[0],  (unsigned char)char_dma_ptr[1],  (unsigned char)char_dma_ptr[2]);		printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code,  (unsigned char)char_dma_ptr[0],  (unsigned char)char_dma_ptr[1],  (unsigned char)char_dma_ptr[2]);
			for (i=0;i<return_code-3;i++) {
				printf("0x%02X  ", (unsigned char)char_dma_ptr[i+3]);
				if((i&0x7) == 0x7) printf("\n");
			}
			printf(" \n" );
			printf(" \n" );
		}


	} while (1) ;
#endif //#define STACK_LIST_EXECUTION

/***********************************************************************************************************************************************************/
/***********************************************************************************************************************************************************/

//#define DIRECT_LIST_EXECUTION
#ifdef DIRECT_LIST_EXECUTION
	// use addr 0x100 for dynamicallay read block size test
	addr = 0x00000100 ;
	data = 0x020 ;
	return_code = vme_crate->vme_A32D32_write (addr, data); 
	printf("vme_A32D32_write: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);



	stack_length = 0 ;
// header
	write_data_array[stack_length+0] = 0xAAAA9000; // header
	write_data_array[stack_length+1] = 0x44556677;  // dummy
	stack_length = stack_length + 2 ;

#define DIRECT_MARKER
#ifdef DIRECT_MARKER
// marker
	write_data_array[stack_length+0] = 0xAAAA8000; // marker
	write_data_array[stack_length+1] = 0x11111111;
	stack_length = stack_length + 2 ;
#endif 

#define DIRECT_INTERNAL_REGISTER
#ifdef DIRECT_INTERNAL_REGISTER

// internal register write: led toggle
	write_data_array[stack_length+0] = 0xAAAA1A00; // internal register / write / 4byte-size
	write_data_array[stack_length+1] = 0x00000001; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	write_data_array[stack_length+3] = 0x00010001; // data
	stack_length = stack_length + 4 ;

// internal register read ident
	write_data_array[stack_length+0] = 0xAAAA1200; // internal register / write / 4byte-size
	write_data_array[stack_length+1] = 0x00000001; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000001; // addr
	stack_length = stack_length + 3 ;
#endif 


#define VME_DIRECT_LIST_WRITE
#ifdef VME_DIRECT_LIST_WRITE

// VME write: A32 D32
	vme_write_flag   = 1 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x10000000; // addr
	write_data_array[stack_length+3] = 0x00010001; // data
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 4 ;
#endif 
#define VME_DIRECT_LIST_READ
#ifdef VME_DIRECT_LIST_READ

// VME read: A32 D32
	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;


// VME read: A32 D32
	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x10000004; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;
#endif 
#ifdef raus
// VME write: A32 D32
	vme_write_flag   = 1 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9;
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x10000000; // addr
	write_data_array[stack_length+3] = 0x00010001; // data
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 4 ;
#endif


#define VME_DIRECT_STACK_VME_MBLT_READ
#ifdef VME_DIRECT_STACK_VME_MBLT_READ



	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 3 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x8  ; //   // MBLT64 

	//vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	//vme_am_mode      = 0xB  + 0x1000; //   // BLT32 
	vme_nof_bytes    = 0x20 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	//write_data_array[stack_length+2] = 0x00000000; // addr
	write_data_array[stack_length+2] = 0x0000; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;

	vme_nof_bytes    = 0x100 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	//write_data_array[stack_length+2] = 0x00000000; // addr
	write_data_array[stack_length+2] = 0x007fff80; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;

#endif

//#define VME_DIRECT_STACK_VME_MBLT_READ_DYN_SIZED
#ifdef VME_DIRECT_STACK_VME_MBLT_READ_DYN_SIZED

// configure "dynamically blockread length" mask register , can be written one time before with a standard sgl write to the internal register, also
	write_data_array[stack_length+0] = 0xAAAA1A00; // internal register / write / 4byte-size
	write_data_array[stack_length+1] = 0x00000001; // 4 Bytes
	write_data_array[stack_length+2] = 0x01000016; // addr
	write_data_array[stack_length+3] = 0x000000FC; // data
	stack_length = stack_length + 4 ;

// VME read: A32 D32 and use the reading data for following dynamically blockread length     
	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	vme_am_mode      = 0x9  + 0x2000; // bit 13
	vme_nof_bytes    = 4 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000100; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;



	vme_write_flag   = 0 ; 
	vme_fifo_mode    = 0 ; 
	vme_access_size  = 3 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	//vme_am_mode      = 0x8 + 0x1000; // bit 12 dynamically blockread length; // MBLT64 
	vme_am_mode      = 0x8  ; //   // MBLT64 

	//vme_access_size  = 2 ; // (0: 1-byte; 1: 2-byte; 2: 4-byte; 3: 8-byte)
	//vme_am_mode      = 0xB  + 0x1000; //   // BLT32 
	vme_nof_bytes    = 0x30 ;

	write_data_array[stack_length+0] = 0xAAAA4000   | (vme_write_flag << 11) | (vme_fifo_mode << 10) | (vme_access_size << 8) | ((vme_nof_bytes>>16)&0xFF);
	write_data_array[stack_length+1] = ((vme_am_mode & 0xFFFF) << 16)  | (vme_nof_bytes & 0xFFFF) ; // 4 Bytes
	write_data_array[stack_length+2] = 0x00000000; // addr
	//printf("write_data_array:   0x%08X      0x%08X   \n", write_data_array[stack_length+0], write_data_array[stack_length+1]);
	stack_length = stack_length + 3 ;
#endif


// trailer
	write_data_array[stack_length+0] = 0xAAAAA000; // trailer
	write_data_array[stack_length+1] = 0x0;
	stack_length = stack_length + 2 ;

 
	do {
		//return_code = vme_crate->udp_send_list (stack_length, write_addr_array, write_data_array); //
		return_code = vme_crate->udp_send_direct_list (stack_length, write_data_array); //
		return_code = vme_crate->udp_read_packet (char_dma_ptr); //
		printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code,  (unsigned char)char_dma_ptr[0],  (unsigned char)char_dma_ptr[1],  (unsigned char)char_dma_ptr[2]);
		if(return_code > 3) {
			for (i=0;i<return_code-3;i++) {
				printf("0x%02X  ", (unsigned char)char_dma_ptr[i+3]);
				if((i&0x7) == 0x7) printf("\n");
			}
			printf(" \n" );
		}
		printf(" \n" );

		return_code = vme_crate->udp_read_packet (char_dma_ptr); //
		if(return_code != -1) {
			printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code, char_dma_ptr[0], char_dma_ptr[1], char_dma_ptr[2]);
			//return_code = vme_crate->udp_read_packet (char_dma_ptr); //
			//printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code, char_dma_ptr[0], char_dma_ptr[1], char_dma_ptr[2]);
			//return_code = vme_crate->udp_read_packet (char_dma_ptr); //
			//printf("udp_read_packet : \treturn_code = 0x%08X   (%d)  \t0x%02X  0x%02X  0x%02X \n", return_code, return_code, char_dma_ptr[0], char_dma_ptr[1], char_dma_ptr[2]);
			printf(" \n" );
		}
		

	} while (1) ;
#endif //#define DIRECT_LIST_EXECUTION
/***********************************************************************************************************************************************************/
/***********************************************************************************************************************************************************/

//#define LED_NIM_LOOP
#ifdef LED_NIM_LOOP
	do {
		// set LEMO CO
		addr = 0x30000070 ;
		data = 0x40000000 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 

		//  Led U on
		addr = 0x30000000 ;
		data = 0x1 ;
		return_code = vme_crate->vme_A32D32_write (addr, data = 0x40000000); 

		// set LEMO TO
		addr = 0x30000074 ;
		data = 0x40000000 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 

		// first burst write of 32 cycles		
		return_code = vme_crate->vme_A32D32_burst_write (write_register_length, write_addr_array, write_data_array); 

		// set LEMO UO
		addr = 0x30000078 ;
		data = 0x40000000 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 


		// second burst write of 32 cycles
		return_code = vme_crate->vme_A32D32_burst_write (write_register_length, write_addr_array, write_data_array); 

		// clear LEMO TO
		addr = 0x30000074 ;
		data = 0x00000000 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 

		// clear LEMO UO
		addr = 0x30000078 ;
		data = 0x00000000 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 



		//  Led U off
		addr = 0x30000000 ;
		data = 0x1 ;
		return_code = vme_crate->vme_A32D32_write (addr, data = 0x40000000); 

		// clear LEMO CO
		addr = 0x30000070 ;
		data = 0x00000000 ;
		return_code = vme_crate->vme_A32D32_write (addr, data); 


		usleep(20000) ; // 20ms


} while (1) ;

#endif

	write_addr_array[0] = 0x30000074 ; // SIS3316 LEMO CO Control register (set/clear CO output )
	write_data_array[0] = 0x40000000 ; // set CO 

	write_addr_array[1] = 0x30000074 ; // SIS3316 LEMO CO Control register (set/clear CO output )
	write_data_array[1] = 0x22222222 ; // clear CO 

	write_addr_array[2] = 0x88888888 ; // SIS3316 LEMO CO Control register (set/clear CO output )
	write_data_array[2] = 0x55555555; // clear CO 


	//write_addr_array[1] = 0x30000000 ; // SIS3316 Control register (set/clear Led)
	//write_data_array[1] = 0x1 ;        // set LED U 
#ifdef raus
	write_addr_array[2] = 0x30000074 ; // SIS3316 LEMO TO Control register (set/clear TO output )
	write_data_array[2] = 0x40000000 ; // set TO 

	for (i=3; i<13;i++) {
		write_addr_array[i] = 0x30000078 ; // SIS3316 LEMO UO Control register (set/clear UO output )
		write_data_array[i] = 0x80000000 ; // genrate pulse at UO 
	}

	write_addr_array[13] = 0x30000074 ; // SIS3316 LEMO TO Control register (set/clear TO output )
	write_data_array[13] = 0x00000000 ; // clear TO 

	write_addr_array[14] = 0x30000000 ; // SIS3316 Control register (set/clear Led)
	write_data_array[14] = 0x10000 ;    // clear LED U 

	write_addr_array[15] = 0x30000070 ; // SIS3316 LEMO CO Control register (set/clear CO output )
	write_data_array[15] = 0x00000000 ; // clear CO 
#endif

/*************************************************************************************************************/


do {

#define INTERNAL_REGISTER
#ifdef INTERNAL_REGISTER
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_CONTROL_STATUS, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_MODID_VERSION, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_MODID_VERSION, data,return_code);
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_SERIAL_NUMBER_REG, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_SERIAL_NUMBER_REG, data,return_code);
	return_code = vme_crate->udp_sis3153_register_read (SIS3153USB_LEMO_IO_CTRL_REG, &data); //
	printf("udp_sis3153_register_read: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_LEMO_IO_CTRL_REG, data,return_code);

	printf("\n");

	do {
		addr = SIS3153USB_CONTROL_STATUS;
		data = 0x1;
		return_code = vme_crate->udp_sis3153_register_write (addr, data); //
		printf("udp_sis3153_register_write: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);
		return_code = vme_crate->udp_sis3153_register_read (addr, &data); //
		printf("udp_sis3153_register_read : \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);
		return_code = vme_crate->udp_sis3153_register_read (addr, &data); //
		printf("udp_sis3153_register_read : \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);

		//usleep(500000);

		addr = SIS3153USB_CONTROL_STATUS;
		data = 0x10000;
		return_code = vme_crate->udp_sis3153_register_write (addr, data); //
		printf("udp_sis3153_register_write: \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);
		return_code = vme_crate->udp_sis3153_register_read (addr, &data); //
		printf("udp_sis3153_register_read : \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);
		return_code = vme_crate->udp_sis3153_register_read (addr, &data); //
		printf("udp_sis3153_register_read : \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", SIS3153USB_CONTROL_STATUS, data,return_code);
		printf("\n");
		printf("\n");

	} while(0) ;
#endif


#define VME_D8_READ_TEST
#ifdef VME_D8_READ_TEST

	printf("\n");
	addr = 0 ;
	data = 0x12345678 ;
	return_code = vme_crate->vme_A32D32_write (addr, data); //
	printf("vme_A32D32_write: \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

	addr = 0x0 ;
	return_code = vme_crate->vme_A32D8_read (addr, &uchar_data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0x1 ;
	return_code = vme_crate->vme_A32D8_read (addr, &uchar_data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0x2 ;
	return_code = vme_crate->vme_A32D8_read (addr, &uchar_data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0x3 ;
	return_code = vme_crate->vme_A32D8_read (addr, &uchar_data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

#endif

 
#define VME_D16_READ_TEST
#ifdef VME_D16_READ_TEST

	printf("\n");
	addr = 0 ;
	data = 0x12345678 ;
	return_code = vme_crate->vme_A32D32_write (addr, data); //
	printf("vme_A32D32_write: \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

	addr = 0x0 ;
	return_code = vme_crate->vme_A32D16_read (addr, &ushort_data); //
	printf("vme_A32D16_read:  \t\taddr = 0x%08X    \tushort_data = 0x%04X \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

	addr = 0x2 ;
	return_code = vme_crate->vme_A32D16_read (addr, &ushort_data); //
	printf("vme_A32D16_read:  \t\taddr = 0x%08X    \tushort_data = 0x%04X \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

#endif

#define VME_D8_WRITE_TEST
#ifdef VME_D8_WRITE_TEST

	printf("\n");

	addr = 0x0 ;
	uchar_data = 0x11 ;
	return_code = vme_crate->vme_A32D8_write (addr, uchar_data); //
	printf("vme_A32D8_write:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0x1 ;
	uchar_data = 0x22 ;
	return_code = vme_crate->vme_A32D8_write (addr, uchar_data); //
	printf("vme_A32D8_write:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0x2 ;
	uchar_data = 0x33 ;
	return_code = vme_crate->vme_A32D8_write (addr, uchar_data); //
	printf("vme_A32D8_write:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0x3 ;
	uchar_data = 0x44 ;
	return_code = vme_crate->vme_A32D8_write (addr, uchar_data); //
	printf("vme_A32D8_write:  \t\taddr = 0x%08X    \tuchar_data = 0x%02X    \treturn_code = 0x%08X \n", addr, uchar_data,return_code);

	addr = 0 ;
	return_code = vme_crate->vme_A32D32_read (addr, &data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

#endif

#define VME_D16_WRITE_TEST
#ifdef VME_D16_WRITE_TEST

	printf("\n");

	addr = 0x0 ;
	ushort_data = 0x7654 ;
	return_code = vme_crate->vme_A32D16_write (addr, ushort_data); //
	printf("vme_A32D16_write: \t\taddr = 0x%08X    \tushort_data = 0x%04X \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

	addr = 0x2 ;
	ushort_data = 0x3210 ;
	return_code = vme_crate->vme_A32D16_write (addr, ushort_data); //
	printf("vme_A32D16_write: \t\taddr = 0x%08X    \tushort_data = 0x%04X \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

	addr = 0 ;
	return_code = vme_crate->vme_A32D32_read (addr, &data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

#endif


//#define SIS_MODULE_READ
#ifdef SIS_MODULE_READ
	printf("\n");
	addr = 0x30000004 ;
	return_code = vme_crate->vme_A32D32_read (addr, &data); //
	printf("vme_A32D32_read (SIS3316):  \taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);
#endif


//#define USER_MODULE_TEST
#ifdef USER_MODULE_TEST
	printf("\n");
	addr = 0xEE1100FC ; // Caen
	return_code = vme_crate->vme_A32D16_read (addr, &ushort_data); //
	printf("vme_A32D16_read (Caen): \taddr = 0x%08X    \tushort_data = 0x%04X  \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

	addr = 0x405c ; // ISEG upper 16 bit of VendorID 
	return_code = vme_crate->vme_A16D16_read (addr, &ushort_data); //
	printf("vme_A16D16_read (ISEG): \taddr = 0x%08X    \tushort_data = 0x%04X  \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

	addr = 0x405e ; // ISEG lower 16 bit of VendorID 
	return_code = vme_crate->vme_A16D16_read (addr, &ushort_data); //
	printf("vme_A16D16_read (ISEG): \taddr = 0x%08X    \tushort_data = 0x%04X  \treturn_code = 0x%08X \n", addr, ushort_data,return_code);

#endif




#define VME_BLT32_READ_TEST
#ifdef VME_BLT32_READ_TEST


	printf("\n");
	request_nof_words = 0x400 ;
	addr = 0x0 ;
	data = 0 ;
	for (i=0;i<request_nof_words;i++) {
		return_code = vme_crate->vme_A32D32_write (addr, data); //
		//printf("vme_A32D32_write: \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);
		addr = addr + 4 ;
		data = data + 1 ;
	}

	for (i=0;i<request_nof_words;i++) {
		gl_dma_buffer[i] = 0 ;
	}
	addr = 0x0 ;
	return_code = vme_crate->vme_A32BLT32_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
	printf("vme_A32BLT32_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
// check
	for (i=0;i<request_nof_words;i++) {
		if (gl_dma_buffer[i] != i) {
			printf("Error: check vme_A32D32_write - vme_A32BLT32_read\n");		
			printf("i = 0x%08X    \twritten = 0x%08X \tread = 0x%08X \n", i, i,gl_dma_buffer[i]);
			Sleep(10);
		}
	}

#endif

#define VME_BLT32_WRITE_TEST
#ifdef VME_BLT32_WRITE_TEST

	printf("\n");
	request_nof_words = 0x10000 ;
	addr = 0 ;
	for (i=0;i<request_nof_words;i++) {
		gl_dma_buffer[i] = i ;
	}
	return_code = vme_crate->vme_A32BLT32_write (addr, gl_dma_buffer, request_nof_words, &written_nof_words); //
	printf("vme_A32BLT32_write:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, written_nof_words,return_code);


	for (i=0;i<request_nof_words;i++) {
		gl_dma_buffer[i] = 0 ;
	}
	addr = 0x0 ;
	return_code = vme_crate->vme_A32BLT32_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
	printf("vme_A32BLT32_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);

// check
	for (i=0;i<request_nof_words;i++) {
		if (gl_dma_buffer[i] != i) {
			printf("Error: check vme_A32BLT32_write - vme_A32BLT32_read\n");		
			printf("i = 0x%08X    \twritten = 0x%08X \tread = 0x%08X \n", i, i,gl_dma_buffer[i]);
			Sleep(10);
		}
	}


#endif





#define VME_MBLT64_READ_TEST
#ifdef VME_MBLT64_READ_TEST

	printf("\n");

	request_nof_words = 0x20000 ;
	addr = 0 ;
	for (i=0;i<request_nof_words;i++) {
		gl_dma_buffer[i] = loop_counter + i ;
	}
	return_code = vme_crate->vme_A32BLT32_write (addr, gl_dma_buffer, request_nof_words, &written_nof_words); //
	if(return_code != 0) {
		prot_error_counter++ ;
	}
	printf("vme_A32BLT32_write:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, written_nof_words,return_code);


	for (i=0;i<request_nof_words;i++) {
		gl_dma_buffer[i] = 0 ;
	}

	request_nof_words = 0x20000 ;
	addr = 0x00000000 ;
	return_code = vme_crate->vme_A32MBLT64_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
	if(return_code != 0) {
		prot_error_counter++ ;
	}
	printf("vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
	if(return_code == 0) {
		// check
		unsigned int error_counter = 0 ;
		for (i=0;i<request_nof_words;i++) {
			if (gl_dma_buffer[i] != loop_counter + i) {
				error_counter++ ;
				data_error_counter++ ;
				if (error_counter == 1) {
					printf("Error: check Data vme_A32MBLT64_read \n");	
				}
				if (error_counter < 20) {
					printf("i = 0x%08X    \twritten = 0x%08X \tread = 0x%08X \n", i, loop_counter+i,gl_dma_buffer[i]);
				}
				//Sleep(500);
			}
		}
	}
	else {
		printf("Error: vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
		prot_error_counter++ ;
	}


/******************/

	return_code = vme_crate->set_UdpSocketEnableJumboFrame(); //
	printf("set_UdpSocketEnableJumboFrame \n");


	return_code = vme_crate->vme_A32MBLT64_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
	if(return_code != 0) {
		prot_error_counter++ ;
	}
	printf("vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
	if(return_code == 0) {
		// check
		unsigned int error_counter = 0 ;
		for (i=0;i<request_nof_words;i++) {
			if (gl_dma_buffer[i] != loop_counter + i) {
				error_counter++ ;
				data_error_counter++ ;
				if (error_counter == 1) {
					printf("Error: check Data vme_A32MBLT64_read \n");	
				}
				if (error_counter < 20) {
					printf("i = 0x%08X    \twritten = 0x%08X \tread = 0x%08X \n", i, loop_counter+i,gl_dma_buffer[i]);
				}
				//Sleep(500);
			}
		}
	}
	else {
		printf("Error: vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
		prot_error_counter++ ;
	}
	return_code = vme_crate->set_UdpSocketDisableJumboFrame(); //
	printf("set_UdpSocketDisableJumboFrame \n");


/******************/

	return_code = vme_crate->set_UdpSocketReceiveNofPackagesPerRequest(max_nof_packets_per_read); //
	printf("set_UdpSocketReceiveNofPackagesPerRequest(%d) \n",max_nof_packets_per_read);

	return_code = vme_crate->vme_A32MBLT64_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
	if(return_code != 0) {
		prot_error_counter++ ;
	}
	printf("vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);

	if(return_code == 0) {
		// check
		unsigned int error_counter = 0 ;
		for (i=0;i<request_nof_words;i++) {
			if (gl_dma_buffer[i] != loop_counter + i) {
				error_counter++ ;
				data_error_counter++ ;
				if (error_counter == 1) {
					printf("Error: check Data vme_A32MBLT64_read \n");	
				}
				if (error_counter < 20) {
					printf("i = 0x%08X    \twritten = 0x%08X \tread = 0x%08X \n", i, loop_counter+i,gl_dma_buffer[i]);
				}
				//Sleep(500);
			}
		}
	}
	else {
		printf("Error: vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
		prot_error_counter++ ;
	}

/******************/

	return_code = vme_crate->set_UdpSocketEnableJumboFrame(); //
	printf("set_UdpSocketEnableJumboFrame \n");
	return_code = vme_crate->set_UdpSocketReceiveNofPackagesPerRequest(max_nof_packets_per_read); //
	printf("set_UdpSocketReceiveNofPackagesPerRequest(%d) \n",max_nof_packets_per_read);

	return_code = vme_crate->vme_A32MBLT64_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
	if(return_code != 0) {
		prot_error_counter++ ;
	}
	printf("vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
	if(return_code == 0) {
		// check
		unsigned int error_counter = 0 ;
		for (i=0;i<request_nof_words;i++) {
			if (gl_dma_buffer[i] != loop_counter + i) {
				error_counter++ ;
				data_error_counter++ ;
				if (error_counter == 1) {
					printf("Error: check Data vme_A32MBLT64_read \n");	
				}
				if (error_counter < 20) {
					printf("i = 0x%08X    \twritten = 0x%08X \tread = 0x%08X \n", i, loop_counter+i,gl_dma_buffer[i]);
				}
				//Sleep(500);
			}
		}
	}
	else {
		printf("Error: vme_A32MBLT64_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
		prot_error_counter++ ;
	}
	return_code = vme_crate->set_UdpSocketDisableJumboFrame(); //
	printf("set_UdpSocketDisableJumboFrame \n");
	return_code = vme_crate->set_UdpSocketReceiveNofPackagesPerRequest(1); //
	printf("set_UdpSocketReceiveNofPackagesPerRequest(1) \n");




	loop_counter++ ;
	printf("\n");
	printf("\n");
	printf("loop_counter = %d    \tprot_error_counter = %d \tdata_error_counter = %d \n", loop_counter, prot_error_counter, data_error_counter);
	printf("\n");




#endif

//#define VME_BLT32_READ_LOOP_TEST
#ifdef VME_BLT32_READ_LOOP_TEST


 
	printf("\n");
	addr = 0 ;
	data = 0x55555555 ;
	return_code = vme_crate->vme_A32D32_write (addr, data); //
	printf("vme_A32D32_write:  \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);
	printf("\n");
	addr = 0x800000 ;
	data = 0xAAAAAAAA ;
	return_code = vme_crate->vme_A32D32_write (addr, data); //
	printf("vme_A32D32_write:  \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);

	printf("\n");
	addr = 0 ;
	return_code = vme_crate->vme_A32D32_read (addr, &data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);
	printf("\n");
	addr = 0x800000 ;
	return_code = vme_crate->vme_A32D32_read (addr, &data); //
	printf("vme_A32D32_read:  \t\taddr = 0x%08X    \tdata = 0x%08X    \treturn_code = 0x%08X \n", addr, data,return_code);


	for (i=0;i<3;i++) {
		printf("\n");
		request_nof_words = 0x200 ;
		addr = 0x7FF800 + (i*0x200) ;
		//addr = 0x7FF000 + (i*0x400) ;
		return_code = vme_crate->vme_A32BLT32_read (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
		printf("vme_A32BLT32_read:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
	}
 

	for (i=0;i<5;i++) {
		printf("\n");
		request_nof_words = 0x200 ;
		addr = 0x7FF800 + (i*0x200) ;
		//addr = 0x7FF000 + (i*0x400) ;
		return_code = vme_crate->vme_A32BLT32_write (addr, gl_dma_buffer, request_nof_words, &got_nof_words); //
		printf("vme_A32BLT32_write:  \t\taddr = 0x%08X    \tgot_nof_words = 0x%08X \treturn_code = 0x%08X \n", addr, got_nof_words,return_code);
	}


#endif


	printf("\n");
	printf("\n");
	printf("\n");
	usleep(500);
} while(1);





	return 0;
}

#ifdef WIN

void usleep(unsigned int uint_usec)
{
    unsigned int msec;
	if (uint_usec <= 1000) {
		msec = 1 ;
	}
	else {
		msec = (uint_usec+999) / 1000 ;
	}
	Sleep(msec);

}
#endif

