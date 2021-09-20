#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <assert.h>
#include <err.h>
#include <libgen.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>  // for getopt


#include "common.h"
#include "config.h"
#include "xran_mlog_lnx.h"

#include "xran_fh_o_du.h"
#include "xran_compression.h"
#include "xran_cp_api.h"
#include "xran_sync_api.h"
#include "xran_mlog_task_id.h"
#include "xran_lib_wrap.hpp"
#include "common.hpp"

// Number of bytes per symbol
#define SYMBOL_DATA_SIZE 13168
//Size in symbols of the intermediate buffer
#define SYMBOL_BUFFER_LEN 112 // =1 sub-frame

// Intermediate Buffer
uint8_t symbol_data_buffer[SYMBOL_BUFFER_LEN][SYMBOL_DATA_SIZE];
// Intermediate Buffer Index
int symbol_in_symbol_data_buffer=0;

void xran_fh_rx_callback(void *pCallbackTag, xran_status_t status){
    return;
}

void xran_fh_srs_callback(void *pCallbackTag, xran_status_t status){
    return;
}

void xran_fh_rx_callback(void *pCallbackTag, xran_status_t status){
	
	if(status!=XRAN_STATUS_SUCCESS){
		return -1;
	}
	
	// pCallbackTag is a structure which contains the timing and the cell id
	struct xran_cb_tag *pTag = (xran_cb_tag *)pCallbackTag;
	uint16_t cell_id = pTag->cellId;
	uint32_t tti = pTag->slotiId;
	uint32_t symbol = pTag->symbol;
	
	// Retrieve the device context which contains information to access the buffer
	struct xran_device_ctx *p_xran_dev_ctx = xran_dev_get_ctx();
	
	/* The slot and the cell id are fixed
	 * We also know the start symbol and that we have to read half a slot
	 */

	// Loop over the antennas
	for(uint8_t ant_id = 0; ant_id < XRAN_MAX_ANTENNA_NR; ant_id++){
		
		// Loop over the symbols
		for(uint32_t symb_id = symbol; symb_id<symbol+7; symb_id++){
			
			uint32_t nElementLenInBytes = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
			uint32_t nNumberOfElements = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
			uint32_t nOffsetInBytes = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
			uint32_t nIsPhyAddr = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
			uint8_t *pData = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
			void *pCtrl = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
			
			// Copy data to Intermediate Buffer
			for (int byte_index=0; byte_index<SYMBOL_DATA_SIZE && byte_index<nElementLenInBytes; byte_index++){
				symbol_data_buffer[symbol_in_symbol_data_buffer][byte_index]=pData[byte_index];
			}
			
			// Increment Intermediate Buffer Index
			symbol_in_symbol_data_buffer=symbol_in_symbol_data_buffer+1;
			if (symbol_in_symbol_data_buffer==SYMBOL_BUFFER_LEN){
				symbol_in_symbol_data_buffer=0;
			}
			
		}
		
	}
	
    return;
}

int main(int argc, char *argv[]){
	xranLibWraper *xranlib;	
	xranlib = new xranLibWraper;
        if(xranlib->SetUp() < 0) {
     	   return (-1);
        }
	xranlib->Init();
	xranlib->Open(nullptr, 
                      nullptr, 
                      (void *)xran_fh_rx_callback, 
                      (void *)xran_fh_rx_prach_callback, 
                      (void *)xran_fh_srs_callback);
	xranlib->Start();
}



























