/***************************************************************************/
/*                                                                         */
/*  Filename: sis3153ETH_vme_class.h                                       */
/*                                                                         */
/*  Funktion:                                                              */
/*                                                                         */
/*  Autor:                CT/TH                                            */
/*  date:                 26.06.2014                                       */
/*  last modification:    31.08.2017                                       */
/*                                                                         */
/* ----------------------------------------------------------------------- */
/*   class_lib_version: 2.1                                                */
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

#ifndef _SIS3153ETH_VME_CLASS_
#define _SIS3153ETH_VME_CLASS_

#define SIS3153ETH_VERSION_MAJOR 0x02   
#define SIS3153ETH_VERSION_MINOR 0x01



#define DHCP_DEVICE_NAME_LARGE_CASE "SIS3153-"
#define DHCP_DEVICE_NAME_LOWER_CASE "sis3153-"


// error codes
#define PROTOCOL_WRONG_PARAMETER							0x100
#define PROTOCOL_ERROR_CODE_TIMEOUT							0x111
#define PROTOCOL_ERROR_CODE_WRONG_ACK						0x120
#define PROTOCOL_ERROR_CODE_WRONG_NOF_RECEVEID_BYTES		0x121
#define PROTOCOL_ERROR_CODE_WRONG_PACKET_IDENTIFIER			0x122
#define PROTOCOL_ERROR_CODE_WRONG_RECEIVED_PACKET_ORDER		0x123
#define PROTOCOL_ERROR_CODE_HEADER_STATUS					0x124

#define PROTOCOL_DISORDER_AND_VME_CODE_BUS_ERROR			0x311

#define PROTOCOL_VME_CODE_BUS_ERROR							0x211

#include "project_system_define.h"		//define LINUX or WIN
#define MAX_SOCKETS  1

//#define DEBUG_COUNTER

/**************************************************************************************/
#define UDP_MAX_PACKETS_PER_REQUEST		    32            //  max. tramnsmit packets per Read Request to the PC

/* nof_write_words: max. 0x100 (256 32-bit words = 1KBytes )  */
#define UDP_MAX_NOF_WRITE_32bitWords    0x100
/* nof_read_words: max. 0x10000 (64k 32-bit words = 256KBytes )  */

// in future
//#define UDP_JUMBO_READ_PACKET_32bitSIZE     2048          //  packet size = 8192 Bytes + 44/45 Bytes = 8236/8237 Bytes
//#define UDP_JUMBO_READ_PACKET_32bitSIZE     768           //  packet size = 3072 Bytes + 44/45 Bytes = 3116/3117 Bytes
//#define UDP_NORMAL_READ_PACKET_32bitSIZE    256           //  packet size = 1024 Bytes + 44/45 Bytes =  

#define UDP_NORMAL_READ_PACKET_32bitSIZE    360           //  packet size = 1440 Bytes + 44/45 Bytes = 1484/1485 Bytes
//#define UDP_JUMBO_READ_PACKET_32bitSIZE     800           //  packet size = 3072 Bytes + 44/45 Bytes = 3116/3117 Bytes            --> support with version v_1604
#define UDP_JUMBO_READ_PACKET_32bitSIZE     1792           //  packet size = 7168 (0x700*4) Bytes  + 44/45 Bytes = 7212/7213 Bytes  --> support with version v_1605

/**************************************************************************************/

// UDP read socket retries
#define UDP_READ_RETRY			5
#define UDP_REQUEST_RETRY		3
#define UDP_RETRANSMIT_RETRY	2


/**************************************************************************************/

#ifdef LINUX
	typedef char CHAR;
	typedef unsigned char UCHAR;
	typedef unsigned short USHORT;
	typedef unsigned int UINT;
	typedef unsigned int* PUINT;
    typedef int INT;

	#include <sys/types.h>
	#include <sys/socket.h>

	#include <sys/uio.h>

	#include <netdb.h>
	#include <sys/time.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <stdio.h>
	#include <unistd.h>
	#include <errno.h>
	#include <string.h>
	#include <stdlib.h>
#endif

#ifdef WIN
#include <windows.h>
	#include <iostream>
	#include <cstdio>
	//#define WIN32_LEAN_AND_MEAN
	//#include <windows.h>
	//#include <winsock2.h>



#endif

#include "vme_interface_class.h"

#ifdef DEBUG_COUNTER
	#define NOF_ERR_COUNTER							11
	#define UDP_SINGLE_READ_SOKET_ERR_COU			0
	#define UDP_SINGLE_READ_VME_ERR_COU				1
	#define UDP_SINGLE_WRITE_SOKET_ERR_COU			2
	#define UDP_SINGLE_WRITE_VME_ERR_COU			3
	#define UDP_SUB_DMA_READ_TIMEOUT_COU			4
	#define UDP_SUB_DMA_READ_PACK_CMD_ERR_COU		5
	#define UDP_SUB_DMA_READ_PACK_STA_ERR_COU		6
	#define UDP_SUB_DMA_READ_PACK_VME_ERR_COU		7
	#define UDP_SUB_DMA_READ_PACK_LOST_COU			8
	#define UDP_SUB_DMA_WRITE_PACK_ERR_COU			9
	#define UDP_SUB_DMA_write_PACK_VME_ERR_COU		10
#endif

/**************************************************************************************/
// UDP reordering LUT
typedef struct {
	bool outstanding;
	size_t offs;
} reorder_lut_t;
/**************************************************************************************/

class sis3153eth : public vme_interface_class
{
private:
	CHAR char_messages[128] ;
	int udp_socket_status;
	int udp_socket;
	unsigned int udp_port ;
	struct sockaddr_in sis3153_sock_addr_in   ;
	struct sockaddr_in myPC_sock_addr   ;
    char udp_send_data[2048];
    char udp_recv_data[16384];

	unsigned int  sgl_random_addr_write_data[64];
	unsigned int  sgl_random_addr_read_data[64];

	unsigned int  jumbo_frame_enable;
	unsigned int  max_nofPacketsPerRequest;
	unsigned int  max_nof_read_lwords;
	unsigned int  max_nof_write_lwords;


	struct vme_struct{
		unsigned int Space:		4;
		unsigned int W:			1;
		unsigned int F:			1;
		unsigned int Size:		2;
		unsigned int Mode:		16;
	}vmeHead;

	char  packet_identifier;

	unsigned char packetNumberOffset;
	void reorderLutInit(reorder_lut_t *lut, size_t len, size_t packetLen);
	unsigned char reorderGetIdx(reorder_lut_t *lut, unsigned char currNum);




	/* Subroutiens/Methods */
	int udp_single_read( unsigned int nof_read_words, UINT* addr_ptr, UINT* data_ptr);
	int udp_single_write( unsigned int nof_write_words, UINT* addr_ptr, UINT* data_ptr);
	
	int udp_DMA_read ( unsigned int nof_read_words, UINT  addr, UINT* data_ptr, UINT* got_nof_words );
	int udp_sub_DMA_read ( unsigned int nof_read_words, UINT  addr, UINT* data_ptr, UINT* nof_got_words);
	
	int udp_DMA_write ( unsigned int nof_write_words, UINT addr, UINT* data_ptr, UINT* written_nof_words );
	int udp_sub_DMA_write ( unsigned int nof_write_words, UINT  addr, UINT* data_ptr, UINT* nof_written_words);

	#ifdef DEBUG_COUNTER
	/* DEVELOPMENT */
	unsigned long error_counter[NOF_ERR_COUNTER];
	#endif


public: // info counter
	unsigned int recv_timeout_sec;
	unsigned int recv_timeout_usec;


	unsigned int   info_udp_receive_timeout_counter;
	unsigned int   info_wrong_cmd_ack_counter;
	unsigned int   info_wrong_received_nof_bytes_counter;
	unsigned int   info_wrong_received_packet_id_counter;


	unsigned int   info_clear_UdpReceiveBuffer_counter;
	unsigned int   info_read_dma_packet_reorder_counter;

	unsigned char  udp_single_read_receive_ack_retry_counter;
	unsigned char  udp_single_read_req_retry_counter;

	unsigned char  udp_single_write_receive_ack_retry_counter;
	unsigned char  udp_single_write_req_retry_counter;

	unsigned char  udp_dma_read_receive_ack_retry_counter;
	unsigned char  udp_dma_read_req_retry_counter;

	unsigned char  udp_dma_write_receive_ack_retry_counter;
	unsigned char  udp_dma_write_req_retry_counter;


public:
	sis3153eth (void);
	sis3153eth (sis3153eth **eth_interface, char *device_ip);
    virtual ~sis3153eth() {}

	int udp_reset_cmd(void);
	int get_UdpSocketStatus( void );
	int get_UdpSocketPort(void );
	int set_UdpSocketOptionTimeout( void );
	int set_UdpSocketOptionBufSize( int sockbufsize );
	int set_UdpSocketBindToDevice( char* eth_device);
	int set_UdpSocketBindMyOwnPort( char* pc_ip_addr_string);
	int set_UdpSocketSIS3153_IpAddress( char* sis3153_ip_addr_string);

	unsigned int get_class_lib_version(void);

	unsigned int get_UdpSocketNofReadMaxLWordsPerRequest(void);
	unsigned int get_UdpSocketNofWriteMaxLWordsPerRequest(void);

	int set_UdpSocketReceiveNofPackagesPerRequest(unsigned int nofPacketsPerRequest);



	int get_UdpSocketJumboFrameStatus(void);
	int set_UdpSocketEnableJumboFrame(void);
	int set_UdpSocketDisableJumboFrame(void);

	int get_UdpSocketGapValue(void);
	int set_UdpSocketGapValue(UINT gapValue);

	int udp_retransmit_cmd( int* receive_bytes, char* data_byte_ptr);
	int clear_UdpReceiveBuffer(void);


	#ifdef DEBUG_COUNTER
		int get_ErrorCounter(unsigned long *errorcounter, bool resetAfterRead);
	#endif

	int udp_sis3153_register_read (UINT addr, UINT* data);
	int udp_sis3153_register_write (UINT addr, UINT data);
	int udp_sis3153_register_dma_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int udp_sis3153_register_dma_write (UINT addr, UINT *data, UINT request_nof_words, UINT* written_nof_words);



	int vmeopen ( void );
	int vmeclose( void );
	int get_vmeopen_messages( CHAR* messages, UINT* nof_found_devices );
	int get_vmeopen_messages(CHAR* messages, size_t size_of_messages, UINT* nof_found_devices);

	int vme_head_add(unsigned int nof_word);
	//int vme_word_head_add(unsigned int nof_word);

	int vme_IACK_D8_read (UINT vme_irq_level, UCHAR* data);		// new 19.01.2016


	

	int vme_CRCSR_D8_read(UINT addr, UCHAR* data);		// new 28.07.2017
	int vme_CRCSR_D16_read(UINT addr, USHORT* data);    // new 28.07.2017
	int vme_CRCSR_D32_read(UINT addr, UINT* data);		// new 28.07.2017

	int vme_A16D8_read(UINT addr, UCHAR* data);			// new 04.09.2015
	int vme_A16D16_read(UINT addr, USHORT* data);		// new 04.09.2015
	int vme_A16D32_read(UINT addr, UINT* data);			// new 28.07.2017

	int vme_A16supervisoryD8_read(UINT addr, UCHAR* data);			// new 22.08.2017
	int vme_A16supervisoryD16_read(UINT addr, USHORT* data);		// new 22.08.2017
	int vme_A16supervisoryD32_read(UINT addr, UINT* data);			// new 22.08.2017


	

	int vme_A24D8_read (UINT addr, UCHAR* data);		// new 04.09.2015
	int vme_A24D16_read (UINT addr, USHORT* data);      // new 04.09.2015
	int vme_A24D32_read (UINT addr, UINT* data);        // new 04.09.2015

	int vme_A32D8_read (UINT addr, UCHAR* data);		// modified 04.09.2015
	int vme_A32D16_read (UINT addr, USHORT* data);      // modified 04.09.2015
	int vme_A32D32_read (UINT addr, UINT* data);

	int vme_A32DMA_D32_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32BLT32_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32MBLT64_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2EVME_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2ESST160_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2ESST267_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2ESST320_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );

	int vme_A32DMA_D32FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32BLT32FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32MBLT64FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2EVMEFIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2ESST160FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2ESST267FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	int vme_A32_2ESST320FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words );
	

	

	int vme_CRCSR_D8_write(UINT addr, UCHAR data);      // new 28.07.2017
	int vme_CRCSR_D16_write(UINT addr, USHORT data);    // new 28.07.2017
	int vme_CRCSR_D32_write(UINT addr, UINT data);      // new 28.07.2017

	int vme_A16D8_write(UINT addr, UCHAR data);      // new 04.09.2015
	int vme_A16D16_write(UINT addr, USHORT data);    // new 04.09.2015
	int vme_A16D32_write(UINT addr, UINT data);      // new 28.07.2017

	int vme_A16supervisoryD8_write(UINT addr, UCHAR data);      // new 22.08.2017
	int vme_A16supervisoryD16_write(UINT addr, USHORT data);    // new 22.08.2017
	int vme_A16supervisoryD32_write(UINT addr, UINT data);      // new 22.08.2017


	


	int vme_A24D8_write (UINT addr, UCHAR data);      // new 04.09.2015
	int vme_A24D16_write (UINT addr, USHORT data);    // new 04.09.2015
	int vme_A24D32_write (UINT addr, UINT data);      // new 04.09.2015

	int vme_A32D8_write (UINT addr, UCHAR data);      // modified 04.09.2015
	int vme_A32D16_write (UINT addr, USHORT data);    // modified 04.09.2015
	int vme_A32D32_write (UINT addr, UINT data);

	int vme_A32DMA_D32_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words );
	int vme_A32BLT32_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words );
	int vme_A32MBLT64_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words );
	int vme_A32DMA_D32FIFO_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words );
	int vme_A32BLT32FIFO_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words );
	int vme_A32MBLT64FIFO_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words );

	int vme_IRQ_Status_read( UINT* data ) ;


	/**********************************************************************************************************/
	 
	int vme_A16D8_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UCHAR* data_ptr);		// new 28.7.2017
	int vme_A16D16_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, USHORT* data_ptr);	// new 28.7.2017
	int vme_A16D32_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UINT* data_ptr);  	// new 31.8.2017

	int vme_A16D8_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UCHAR* data_ptr);		// new 28.7.2017
	int vme_A16D16_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, USHORT* data_ptr);		// new 28.7.2017
	int vme_A16D32_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UINT* data_ptr);		// new 31.8.2017

	/*******************/

	int vme_A16supervisoryD8_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UCHAR* data_ptr);		// new 31.8.2017
	int vme_A16supervisoryD16_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, USHORT* data_ptr);	// new 31.8.2017
	int vme_A16supervisoryD32_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UINT* data_ptr);  	// new 31.8.2017

	int vme_A16supervisoryD8_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UCHAR* data_ptr);		// new 31.8.2017
	int vme_A16supervisoryD16_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, USHORT* data_ptr);		// new 31.8.2017
	int vme_A16supervisoryD32_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UINT* data_ptr);		// new 31.8.2017


	/**********************************************************************************************************/



	int vme_A24D8_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UCHAR* data_ptr);		// new 28.7.2017
	int vme_A24D16_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, USHORT* data_ptr);	// new 28.7.2017
	int vme_A24D32_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UINT* data_ptr);		// new 28.7.2017
	int vme_A24D8_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UCHAR* data_ptr);		// new 28.7.2017
	int vme_A24D16_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, USHORT* data_ptr);		// new 28.7.2017
	int vme_A24D32_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UINT* data_ptr);		// new 28.7.2017

	int vme_A32D8_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UCHAR* data_ptr);		// new 28.7.2017
	int vme_A32D16_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, USHORT* data_ptr);	// new 28.7.2017
	int vme_A32D32_sgl_random_burst_write(UINT nof_writes, UINT* addr_ptr, UINT* data_ptr);		// new 28.7.2017
	int vme_A32D8_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UCHAR* data_ptr);		// new 28.7.2017
	int vme_A32D16_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, USHORT* data_ptr);		// new 28.7.2017
	int vme_A32D32_sgl_random_burst_read(UINT nof_reads, UINT* addr_ptr, UINT* data_ptr);		// new 28.7.2017


	/* Free access */
	int udp_global_read(UINT addr, UINT* data, UINT request_nof_words, UINT space, UINT dma_fg, UINT fifo_fg, UINT size, UINT mode, UINT* got_nof_words);
	int udp_global_write(UINT addr, UINT* data, UINT request_nof_words, UINT space, UINT dma_fg, UINT fifo_fg, UINT size, UINT mode, UINT* written_nof_words);








	/**************************************************************************************************************************/
	/*                                                                                                                        */
	/*   The following part is under construction !!!!!!!!!!!!!!!!!!                                                          */
	/*                                                                                                                        */
	/**************************************************************************************************************************/

	int udp_send_direct_list(unsigned int list_length, UINT* list_buffer) ;
	int udp_read_list_packet(CHAR* packet_recv_data);
	int list_read_event(UCHAR* packet_ack, UCHAR* packet_ident, UCHAR* packet_status, UINT* event_data_buffer, UINT* got_nof_event_data);

	int list_generate_add_header(UINT* list_ptr, UINT* list_buffer);
	int list_generate_add_trailer(UINT* list_ptr, UINT* list_buffer);

	int list_generate_add_marker(UINT* list_ptr, UINT* list_buffer, UINT list_marker);

	int list_generate_add_register_write(UINT* list_ptr, UINT* list_buffer, UINT addr, UINT data);
	int list_generate_add_register_read(UINT* list_ptr, UINT* list_buffer, UINT addr);

	int list_generate_add_vmeA32D32_write(UINT* list_ptr, UINT* list_buffer, UINT vme_addr, UINT vme_data);
	int list_generate_add_vmeA32D32_read(UINT* list_ptr, UINT* list_buffer, UINT vme_addr);
	int list_generate_add_vmeA32BLT32_read(UINT* list_ptr, UINT* list_buffer, UINT vme_addr, UINT request_nof_words);
	int list_generate_add_vmeA32MBLT64_read(UINT* list_ptr, UINT* list_buffer, UINT vme_addr, UINT request_nof_words);
	int list_generate_add_vmeA32MBLT64_swapDWord_read(UINT* list_ptr, UINT* list_buffer, UINT vme_addr, UINT request_nof_words);

	
	 

};


// Return Codes
/**************************************************************************************/
/* udp_single_read                                                                    */
/* possible ReturnCodes:                                                              */     
/*       0     : OK                                                                   */
/*       0x111 : PROTOCOL_ERROR_CODE_TIMEOUT                                          */
/*       0x121 : wrong Packet Length after N Retransmit and M Request commands        */
/*       0x122 : wrong received Packet ID after N Retransmit and M Request commands   */ 
/*       0x211 : PROTOCOL_VME_CODE_BUS_ERROR                                          */
/**************************************************************************************/

/**************************************************************************************/
/* udp_single_write                                                                   */
/* possible ReturnCodes:                                                              */     
/*       0     : OK                                                                   */
/*       0x111 : PROTOCOL_ERROR_CODE_TIMEOUT                                          */
/*       0x122 : wrong received Packet ID after N Retransmit and M Request commands   */ 
/*       0x211 : PROTOCOL_VME_CODE_BUS_ERROR                                          */
/*       0x2xx : PROTOCOL_VME_CODES                                                   */
/**************************************************************************************/

/**************************************************************************************/

/*******************************************************************************************/
/* udp_DMA_read, udp_sub_DMA_read                                                          */ 
/* possible ReturnCodes:                                                                   */  
/*       0     : OK                                                                        */
/*       0x111 : PROTOCOL_ERROR_CODE_TIMEOUT                                               */
/*       0x120 : wrong received Packet CMD after N Retransmit                              */
/*       0x122 : wrong received Packet ID after N Retransmit                               */
/*       0x124 : PROTOCOL_ERROR_CODE_HEADER_STATUS                                         */
/*                                                                                         */
/*       0x211 : PROTOCOL_VME_CODE_BUS_ERROR                                               */
/*                                                                                         */
/*       0x311 : PROTOCOL_DISORDER_AND_VME_CODE_BUS_ERROR                                  */
/*******************************************************************************************/

#endif
