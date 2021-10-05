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

#include <signal.h>
#include <rte_pdump.h>

// Number of bytes per symbol
#define SYMBOL_DATA_SIZE 13168
//Size in symbols of the intermediate buffer
#define SYMBOL_BUFFER_LEN 112 // =1 sub-frame

// Intermediate Buffer
uint8_t symbol_data_buffer[XRAN_MAX_SECTOR_NR][XRAN_MAX_ANTENNA_NR][SYMBOL_BUFFER_LEN][SYMBOL_DATA_SIZE];
// Intermediate Buffer Write Index
int write_symbol_in_symbol_data_buffer[XRAN_MAX_SECTOR_NR];
// Intermediate Buffer Read Index
int read_symbol_in_symbol_data_buffer[XRAN_MAX_SECTOR_NR];

// Index in TX buffer of the previous symbol sent
int previous_sent_symbol[XRAN_MAX_SECTOR_NR];

// device context
struct xran_device_ctx *p_xran_dev_ctx;

// while loop escape flag
int escape_flag;

void sigint_handler(int signum){
	escape_flag=1;
}

void init_buffer_indexes(){
	
	for(uint16_t cell_id=0; cell_id<XRAN_MAX_SECTOR_NR; cell_id++){
		write_symbol_in_symbol_data_buffer[cell_id]=0;
		read_symbol_in_symbol_data_buffer[cell_id]=0;
		previous_sent_symbol[cell_id]=0;
	}
	
}

int32_t get_current_tx_symbol_id(){
	
	// Retrieve the device context which contains information to access the buffer
	p_xran_dev_ctx = xran_dev_get_ctx();
	
	int32_t ota_sym = xran_lib_ota_sym_idx;									// declared extern in xran_lib_wrap.hpp
	int32_t off_sym = p_xran_dev_ctx->sym_up;								// symbol offset of TX at DU with respect to OTA time
	
	// the symbol index is reset every period (1 second=1000 ms=1000 sub-frames)
	int32_t max_sym = XRAN_NUM_OF_SYMBOL_PER_SLOT*8/*SLOTNUM_PER_SUBFRAME*/*1000;
	
	int32_t sym = ota_sym - off_sym + 1;	// Added 1 since we do not know if ota_sym is the symbol currently send or the next one
	
	if(sym>=max_sym){
		sym-=max_sym;
	}
	
	if(sym<0){
		sym+=max_sym;
	}
	
	return sym;
	
}

void send_intermediate_buffer_symbol(){
	
	// Retrieve the device context which contains information to access the buffer
	p_xran_dev_ctx = xran_dev_get_ctx();
	
	for(uint16_t cell_id=0; cell_id<XRAN_MAX_SECTOR_NR; cell_id++){
		
		int32_t sym = get_current_tx_symbol_id();
	
		if(write_symbol_in_symbol_data_buffer[cell_id]!=read_symbol_in_symbol_data_buffer[cell_id] && sym!=previous_sent_symbol[cell_id]){	// There are symbols to read and the current TX symbol is not already used
			// printf to see if sending data to RU
			printf("sending data to RU, symbol %d in intermediate buffer, cell %d\n",read_symbol_in_symbol_data_buffer[cell_id],cell_id);			

			int32_t tti = sym / XRAN_NUM_OF_SYMBOL_PER_SLOT;
			int32_t sym_idx = sym % XRAN_NUM_OF_SYMBOL_PER_SLOT;
			
			for(uint8_t ant_id = 0; ant_id < XRAN_MAX_ANTENNA_NR; ant_id++){
				
				uint32_t nElementLenInBytes = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
				uint32_t nNumberOfElements = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
				uint32_t nOffsetInBytes = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
				uint32_t nIsPhyAddr = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
				uint8_t *pData = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
				void *pCtrl = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
				
				// TODO:
				// Build headers
				// Write headers
				
				// Copy data from Intermediate Buffer
				for (int byte_index=0; byte_index<SYMBOL_DATA_SIZE && byte_index<nElementLenInBytes; byte_index++){
					pData[byte_index]=symbol_data_buffer[cell_id][ant_id][read_symbol_in_symbol_data_buffer[cell_id]][byte_index];
				}
			
			}
			
			// Increment Buffers Indexes
			read_symbol_in_symbol_data_buffer[cell_id]=read_symbol_in_symbol_data_buffer[cell_id]+1;
			if (read_symbol_in_symbol_data_buffer[cell_id]==SYMBOL_BUFFER_LEN){
				read_symbol_in_symbol_data_buffer[cell_id]=0;
			}
			previous_sent_symbol[cell_id]=sym;
			
		}
	
	}
	
	return;
	
}

void xran_fh_srs_callback(void *pCallbackTag, xran_status_t status){
    return;
}

void xran_fh_rx_prach_callback(void *pCallbackTag, xran_status_t status){
    return;
}

void xran_fh_rx_callback(void *pCallbackTag, xran_status_t status){
	
	if(status!=XRAN_STATUS_SUCCESS){
		return;
	}
	
	// pCallbackTag is a structure which contains the timing and the cell id
	struct xran_cb_tag *pTag = (xran_cb_tag *)pCallbackTag;
	uint16_t cell_id = pTag->cellId;
	uint32_t tti = pTag->slotiId;
	uint32_t symbol = pTag->symbol;
	
	// Retrieve the device context which contains information to access the buffer
	p_xran_dev_ctx = xran_dev_get_ctx();
	
	/* The slot and the cell id are fixed
	 * We also know the start symbol and that we have to read half a slot
	 */
	
	// printf to see if receiving data from RU
	printf("receiving data from RU, symbol %d in intermediate buffer, cell %d\n",write_symbol_in_symbol_data_buffer[cell_id],cell_id);
	// Loop over the antennas
	for(uint32_t symb_id = symbol; symb_id<symbol+7; symb_id++){
		
		// Loop over the symbols
		for(uint8_t ant_id = 0; ant_id < XRAN_MAX_ANTENNA_NR; ant_id++){
			
			uint32_t nElementLenInBytes = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
			uint32_t nNumberOfElements = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
			uint32_t nOffsetInBytes = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
			uint32_t nIsPhyAddr = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
			uint8_t *pData = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
			void *pCtrl = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
			
			// Copy data to Intermediate Buffer
			for (int byte_index=0; byte_index<SYMBOL_DATA_SIZE && byte_index<nElementLenInBytes; byte_index++){
				symbol_data_buffer[cell_id][ant_id][write_symbol_in_symbol_data_buffer[cell_id]][byte_index]=pData[byte_index];
			}
			
		}
		
		// Increment Intermediate Buffer Index
		write_symbol_in_symbol_data_buffer[cell_id]=write_symbol_in_symbol_data_buffer[cell_id]+1;
		if (write_symbol_in_symbol_data_buffer[cell_id]==SYMBOL_BUFFER_LEN){
			write_symbol_in_symbol_data_buffer[cell_id]=0;
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
	
	init_buffer_indexes();
	
	xranlib->Start();

	escape_flag=0;
	
	signal(SIGINT,sigint_handler);
	
	printf("wrapper's initilization done.\n");
	
	while(!escape_flag){
		
		send_intermediate_buffer_symbol();
		rte_pause();
		
	}
	
	xranlib->Stop();

	printf("\nexiting wrapper.\n");
	
}


























