#ifndef _VME_INTERFACE_CLASS_
#define _VME_INTERFACE_CLASS_

#include "project_system_define.h"		//define LINUX or WIN


#ifdef LINUX
	typedef char CHAR;
	typedef unsigned int UINT;
	typedef unsigned int* PUINT;
#endif

#define DEF_VME_READ_MODE_D32				0  
#define DEF_VME_READ_MODE_D32_DMA			1  
#define DEF_VME_READ_MODE_BLT32				2  
#define DEF_VME_READ_MODE_MBLT64			3  
#define DEF_VME_READ_MODE_2EVME				4  
#define DEF_VME_READ_MODE_2ESST160			5  
#define DEF_VME_READ_MODE_2ESST267			6  
#define DEF_VME_READ_MODE_2ESST320			7  

#pragma once
class vme_interface_class{
public:
	virtual int vmeopen( void ) = 0;
	virtual int vmeclose( void ) = 0;

	virtual int get_vmeopen_messages( CHAR* messages, UINT* nof_found_devices ) = 0;

	virtual int vme_A32D32_read( UINT addr, UINT* data ) = 0;

	virtual int vme_A32DMA_D32_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32BLT32_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32MBLT64_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2EVME_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2ESST160_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2ESST267_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2ESST320_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;

	virtual int vme_A32DMA_D32FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32BLT32FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32MBLT64FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2EVMEFIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2ESST160FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2ESST267FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;
	virtual int vme_A32_2ESST320FIFO_read (UINT addr, UINT* data, UINT request_nof_words, UINT* got_nof_words ) = 0;


	virtual int vme_A32D32_write( UINT addr, UINT data ) = 0;
	virtual int vme_A32DMA_D32_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words ) = 0;
	virtual int vme_A32BLT32_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words ) = 0;
	virtual int vme_A32MBLT64_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words ) = 0;

	virtual int vme_A32DMA_D32FIFO_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words ) = 0;
	virtual int vme_A32BLT32FIFO_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words ) = 0;
	virtual int vme_A32MBLT64FIFO_write (UINT addr, UINT* data, UINT request_nof_words, UINT* written_nof_words ) = 0;

	virtual int vme_IRQ_Status_read( UINT* data ) = 0;

	
	//	virtual int read( int *buf, int len ) = 0;
//	virtual int write( int *buf, int len ) = 0;
};

#endif

