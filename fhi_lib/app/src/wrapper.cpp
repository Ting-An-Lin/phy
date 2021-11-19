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

// Callback Tags buffer size
#define CALLBACK_TAGS_BUFFER_LEN (1000)
// Number of bytes per symbol
#define SYMBOL_DATA_SIZE (13168)
//Size in slots of the intermediate buffer
#define SLOT_BUFFER_LEN (10) // =1 sub-frame
//Size in slot of the intermediate buffer
#define SYMBOL_BUFFER_LEN (SLOT_BUFFER_LEN*XRAN_NUM_OF_SYMBOL_PER_SLOT)

// Calback Tags buffer
xran_cb_tag callback_tags_buffer[CALLBACK_TAGS_BUFFER_LEN];
// index of tags to read
int  callback_tags_to_read_index=-1;
// index of read tags
int callback_tags_already_read_index=-1;

// Calback Tags buffer
xran_cb_tag prach_callback_tags_buffer[CALLBACK_TAGS_BUFFER_LEN];
// index of tags to read
int prach_callback_tags_to_read_index=-1;
// index of read tags
int prach_callback_tags_already_read_index=-1;


// Intermediate Buffer
uint8_t symbol_data_buffer[XRAN_MAX_SECTOR_NR][XRAN_MAX_ANTENNA_NR][SYMBOL_BUFFER_LEN][SYMBOL_DATA_SIZE];
// Intermediate Buffer Write Index
int write_symbol_in_symbol_data_buffer[XRAN_MAX_SECTOR_NR];
// Intermediate Buffer Read Index
int read_symbol_in_symbol_data_buffer[XRAN_MAX_SECTOR_NR];

// Intermediate Buffer
uint8_t prach_symbol_data_buffer[XRAN_MAX_SECTOR_NR][XRAN_MAX_ANTENNA_NR][SYMBOL_BUFFER_LEN][PRACH_PLAYBACK_BUFFER_BYTES];
// Intermediate Buffer Write Index
int prach_write_symbol_in_symbol_data_buffer[XRAN_MAX_SECTOR_NR];
// Intermediate Buffer Read Index
int prach_read_symbol_in_symbol_data_buffer[XRAN_MAX_SECTOR_NR];

// Index in TX buffer of the previous symbol sent
int previous_sent_symbol[XRAN_MAX_SECTOR_NR];

// device context
struct xran_device_ctx *p_xran_dev_ctx;
struct xran_device_ctx *p_xran_dev_ctx_2;

// while loop escape flag
int escape_flag;

long RX_bytes=0L;

long RX_bytes_prach=0L;

xranLibWraper *xranlib;

void sigint_handler(int signum){
	escape_flag=1;
	printf("Received SIGINT, Exiting.\n");
}

void init_buffer_indexes(){
	
	for(uint16_t cell_id=0; cell_id<XRAN_MAX_SECTOR_NR; cell_id++){
		write_symbol_in_symbol_data_buffer[cell_id]=0;
		read_symbol_in_symbol_data_buffer[cell_id]=0;
		previous_sent_symbol[cell_id]=0;
		prach_write_symbol_in_symbol_data_buffer[cell_id]=0;
		prach_read_symbol_in_symbol_data_buffer[cell_id]=0;

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

void send_intermediate_buffer_symbol_test(){

       int32_t flowId;
       void *ptr = NULL;
       char *pos = NULL;

        	
       // Retrieve the device context which contains information to access the buffer
       p_xran_dev_ctx_2 = xran_dev_get_ctx();
       if (p_xran_dev_ctx_2 != NULL){
          printf("p_xran_dev_ctx_2=%d\n",p_xran_dev_ctx_2);      
       }	

       int num_eaxc = xranlib->get_num_eaxc();
       int num_eaxc_ul = xranlib->get_num_eaxc_ul();
       uint32_t xran_max_antenna_nr = RTE_MAX(num_eaxc, num_eaxc_ul);
       int ant_el_trx = xranlib->get_num_antelmtrx();
       uint32_t xran_max_ant_array_elm_nr = RTE_MAX(ant_el_trx, xran_max_antenna_nr);        

       int32_t nSectorIndex[XRAN_MAX_SECTOR_NR];
       int32_t nSectorNum;

       for (nSectorNum = 0; nSectorNum < XRAN_MAX_SECTOR_NR; nSectorNum++)
       {
           nSectorIndex[nSectorNum] = nSectorNum;
       }
       nSectorNum = xranlib->get_num_cc();

       int maxflowid = num_eaxc * (nSectorNum-1) + (xran_max_antenna_nr-1);
       printf("the maximum flowID will be=%d\n",maxflowid); 

       for(uint16_t cc_id=0; cc_id<nSectorNum; cc_id++){
          for(int32_t tti  = 0; tti  < XRAN_N_FE_BUF_LEN; tti++) {
             for(uint8_t ant_id = 0; ant_id < xran_max_antenna_nr; ant_id++){
                for(int32_t sym_idx = 0; sym_idx < XRAN_NUM_OF_SYMBOL_PER_SLOT; sym_idx++) {
	
	           flowId = num_eaxc * cc_id + ant_id;
                   //printf ("flow_id %d\n",flowId);	
	
							
				// Symbol Data
			//	uint32_t nElementLenInBytes = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
			//	uint32_t nNumberOfElements = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
		       //		uint32_t nOffsetInBytes = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
		//		uint32_t nIsPhyAddr = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
				uint8_t *pData = p_xran_dev_ctx_2->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
		//		void *pCtrl = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
			
				// Prb Map Data	
		//		uint32_t nPrbMapElementLenInBytes = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers->nElementLenInBytes;
                //               	uint32_t nPrbMapNumberOfElements = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers->nNumberOfElements;
               
    rte_pause(); //                uint32_t nPrbMapOffsetInBytes = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers->nOffsetInBytes;
                //                uint32_t nPrbMapIsPhyAddr = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers->nIsPhyAddr;
                                uint8_t *pPrbMapData = p_xran_dev_ctx_2->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers->pData;
                //                void *pPrbMapCtrl = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cc_id][ant_id].sBufferList.pBuffers->pCtrl;

                                struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;

				// Variables for the following pasted part
				ptr = pData;
	                        pos = ((char*)p_tx_play_buffer[flowId]) + tx_play_buffer_position[flowId];

				uint8_t *u8dptr;
				struct xran_prb_map *pRbMap = pPrbMap;
				int32_t sym_id = sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT;

				// Pasted sending of sample_app.c from here
				if(ptr && pos){
                                    int idxElm = 0;
                                    u8dptr = (uint8_t*)ptr;
                                    int16_t payload_len = 0;

                                    uint8_t  *dst = (uint8_t *)u8dptr;
                                    uint8_t  *src = (uint8_t *)pos;
                                    struct xran_prb_elm* p_prbMapElm = &pRbMap->prbMap[idxElm];
                                   // printf("\n\nant_id %d, tti %d \n",ant_id,tti);
                                    dst =  xran_add_hdr_offset(dst, p_prbMapElm->compMethod);
                                    for (idxElm = 0;  idxElm < pRbMap->nPrbElm; idxElm++) {
                                        struct xran_section_desc *p_sec_desc = NULL;
                                        p_prbMapElm = &pRbMap->prbMap[idxElm];
                                        p_sec_desc =  p_prbMapElm->p_sec_desc[sym_id];

                                        if(p_sec_desc == NULL){
                                            printf ("p_sec_desc == NULL\n");
                                            exit(-1);
                                        }
                                       src = (uint8_t *)(pos + p_prbMapElm->nRBStart*N_SC_PER_PRB*4L);

                                        if(p_prbMapElm->compMethod == XRAN_COMPMETHOD_NONE) {
					    //printf("idxElm=%d, compMeth==NONE\n",idxElm);
                                            payload_len = p_prbMapElm->nRBSize*N_SC_PER_PRB*4L;
                                            rte_memcpy(dst, src, payload_len);

                                        } else if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_BLKFLOAT) {
					    printf("idxElm=%d, compMeth==BLKFLOAT\n",idxElm);
                                            struct xranlib_compress_request  bfp_com_req;
                                            struct xranlib_compress_response bfp_com_rsp;

                                            memset(&bfp_com_req, 0, sizeof(struct xranlib_compress_request));
                                            memset(&bfp_com_rsp, 0, sizeof(struct xranlib_compress_response));

                                            bfp_com_req.data_in    = (int16_t*)src;
                                            bfp_com_req.numRBs     = p_prbMapElm->nRBSize;
                                            bfp_com_req.len        = p_prbMapElm->nRBSize*N_SC_PER_PRB*4L;
                                            bfp_com_req.compMethod = p_prbMapElm->compMethod;
                                            bfp_com_req.iqWidth    = p_prbMapElm->iqWidth;

                                            bfp_com_rsp.data_out   = (int8_t*)dst;
                                            bfp_com_rsp.len        = 0;

                                            xranlib_compress_avx512(&bfp_com_req, &bfp_com_rsp);
                                            payload_len = bfp_com_rsp.len;

                                        }else {
                                            printf ("p_prbMapElm->compMethod == %d is not supported\n",
                                                p_prbMapElm->compMethod);
                                            exit(-1);
                                        }

                                        /* update RB map for given element */
                                        p_sec_desc->iq_buffer_offset = RTE_PTR_DIFF(dst, u8dptr);
                                        p_sec_desc->iq_buffer_len = payload_len;

                                        /* add headroom for ORAN headers between IQs for chunk of RBs*/
                                        dst += payload_len;
                                        dst  = xran_add_hdr_offset(dst, p_prbMapElm->compMethod);
                                    }
                                } else {
                                    exit(-1);
                                    printf("ptr ==NULL\n");
                                }
				// end of pasted sending of sample_app.c

				// Copy data from Intermediate Buffer
				/*for (int byte_index=0; byte_index<SYMBOL_DATA_SIZE && byte_index<nElementLenInBytes; byte_index++){
					pData[byte_index]=symbol_data_buffer[cell_id][ant_id][read_symbol_in_symbol_data_buffer[cell_id]][byte_index];
	     			}*/
/*
 * if (flowId <= 3){
   printf("cc_id=%d, tti=%d, ant_id=%d, sym_idx=%d, sym_id=%d\n",cc_id,tti,ant_id,sym_idx, sym_id);
   if (flowId == 3) exit(-1);
}
*/
                }
              }  
            }              	

			

		/*	
			// Increment Buffers Indexes
			read_symbol_in_symbol_data_buffer[cell_id]=read_symbol_in_symbol_data_buffer[cell_id]+1;
			if (read_symbol_in_symbol_data_buffer[cell_id]==SYMBOL_BUFFER_LEN){
				read_symbol_in_symbol_data_buffer[cell_id]=0;
			}
			previous_sent_symbol[cell_id]=sym;
		*/	
	
	}
	
	return;
	
}

void send_intermediate_buffer_symbol(){
	
	// Retrieve the device context which contains information to access the buffer
	p_xran_dev_ctx = xran_dev_get_ctx();
/*
        if (p_xran_dev_ctx->send_cpmbuf2ring == NULL){
           printf("p_xran_dev_ctx->send_cpmbuf2ring NOT set >>>> Exit\n");
           exit(1);
        }
        if(p_xran_dev_ctx->send_upmbuf2ring == NULL){
           printf("p_xran_dev_ctx->send_upmbuf2ring NOT set >>>> Exit\n");
           exit(1);
        }
*/	
	for(uint16_t cell_id=0; cell_id<XRAN_MAX_SECTOR_NR; cell_id++){
		
		int32_t sym = get_current_tx_symbol_id();
	
		if(write_symbol_in_symbol_data_buffer[cell_id]!=read_symbol_in_symbol_data_buffer[cell_id] && sym!=previous_sent_symbol[cell_id]){	// There are symbols to read and the current TX symbol is not already used
			// printf to see if sending data to RU
			printf("sending data to RU, symbol %d in intermediate buffer, cell %d\n",read_symbol_in_symbol_data_buffer[cell_id],cell_id);			

			int32_t tti = sym / XRAN_NUM_OF_SYMBOL_PER_SLOT;
			int32_t sym_idx = sym % XRAN_NUM_OF_SYMBOL_PER_SLOT;
			
			for(uint8_t ant_id = 0; ant_id < 7/*XRAN_MAX_ANTENNA_NR*/; ant_id++){
				
				// Symbol Data
				uint32_t nElementLenInBytes = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
				uint32_t nNumberOfElements = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
				uint32_t nOffsetInBytes = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
				uint32_t nIsPhyAddr = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
				uint8_t *pData = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
				void *pCtrl = p_xran_dev_ctx->sFrontHaulTxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
			
				// Prb Map Data	
				uint32_t nPrbMapElementLenInBytes = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers->nElementLenInBytes;
                               	uint32_t nPrbMapNumberOfElements = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers->nNumberOfElements;
                                uint32_t nPrbMapOffsetInBytes = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers->nOffsetInBytes;
                                uint32_t nPrbMapIsPhyAddr = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers->nIsPhyAddr;
                                uint8_t *pPrbMapData = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers->pData;
                                void *pPrbMapCtrl = p_xran_dev_ctx->sFrontHaulTxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers->pCtrl;

                                struct xran_prb_map *pPrbMap = (struct xran_prb_map *)pPrbMapData;

				// Variables for the following pasted part
				void *ptr = pData;
                                int flowId =0; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1
				// uint8_t *pos = symbol_data_buffer[cell_id][ant_id][read_symbol_in_symbol_data_buffer[cell_id]];
				char *pos = ((char*)p_tx_play_buffer[flowId]) + tx_play_buffer_position[flowId];
				uint8_t *u8dptr;
				struct xran_prb_map *pRbMap = pPrbMap;
				int32_t sym_id = sym_idx%XRAN_NUM_OF_SYMBOL_PER_SLOT;

				// Pasted sending of sample_app.c from here
				if(ptr && pos){
                                    int idxElm = 0;
                                    u8dptr = (uint8_t*)ptr;
                                    int16_t payload_len = 0;

                                    uint8_t  *dst = (uint8_t *)u8dptr;
                                    uint8_t  *src = (uint8_t *)pos;
                                    struct xran_prb_elm* p_prbMapElm = &pRbMap->prbMap[idxElm];
                                    printf("\n\nant_id %d, tti %d \n",ant_id,tti);
                                    dst =  xran_add_hdr_offset(dst, p_prbMapElm->compMethod);
                                    for (idxElm = 0;  idxElm < pRbMap->nPrbElm; idxElm++) {
                                        struct xran_section_desc *p_sec_desc = NULL;
                                        p_prbMapElm = &pRbMap->prbMap[idxElm];
                                        p_sec_desc =  p_prbMapElm->p_sec_desc[sym_id];

                                        if(p_sec_desc == NULL){
                                            printf ("p_sec_desc == NULL\n");
                                            exit(-1);
                                        }
                                       src = (uint8_t *)(pos + p_prbMapElm->nRBStart*N_SC_PER_PRB*4L);

                                        if(p_prbMapElm->compMethod == XRAN_COMPMETHOD_NONE) {
					    printf("idxElm=%d, compMeth==NONE\n",idxElm);
                                            payload_len = p_prbMapElm->nRBSize*N_SC_PER_PRB*4L;
                                            rte_memcpy(dst, src, payload_len);

                                        } else if (p_prbMapElm->compMethod == XRAN_COMPMETHOD_BLKFLOAT) {
					    printf("idxElm=%d, compMeth==BLKFLOAT\n",idxElm);
                                            struct xranlib_compress_request  bfp_com_req;
                                            struct xranlib_compress_response bfp_com_rsp;

                                            memset(&bfp_com_req, 0, sizeof(struct xranlib_compress_request));
                                            memset(&bfp_com_rsp, 0, sizeof(struct xranlib_compress_response));

                                            bfp_com_req.data_in    = (int16_t*)src;
                                            bfp_com_req.numRBs     = p_prbMapElm->nRBSize;
                                            bfp_com_req.len        = p_prbMapElm->nRBSize*N_SC_PER_PRB*4L;
                                            bfp_com_req.compMethod = p_prbMapElm->compMethod;
                                            bfp_com_req.iqWidth    = p_prbMapElm->iqWidth;

                                            bfp_com_rsp.data_out   = (int8_t*)dst;
                                            bfp_com_rsp.len        = 0;

                                            xranlib_compress_avx512(&bfp_com_req, &bfp_com_rsp);
                                            payload_len = bfp_com_rsp.len;

                                        }else {
                                            printf ("p_prbMapElm->compMethod == %d is not supported\n",
                                                p_prbMapElm->compMethod);
                                            exit(-1);
                                        }

                                        /* update RB map for given element */
                                        p_sec_desc->iq_buffer_offset = RTE_PTR_DIFF(dst, u8dptr);
                                        p_sec_desc->iq_buffer_len = payload_len;

                                        /* add headroom for ORAN headers between IQs for chunk of RBs*/
                                        dst += payload_len;
                                        dst  = xran_add_hdr_offset(dst, p_prbMapElm->compMethod);
                                    }
                                } else {
                                    exit(-1);
                                    printf("ptr ==NULL\n");
                                }
				// end of pasted sending of sample_app.c

				// Copy data from Intermediate Buffer
				/*for (int byte_index=0; byte_index<SYMBOL_DATA_SIZE && byte_index<nElementLenInBytes; byte_index++){
					pData[byte_index]=symbol_data_buffer[cell_id][ant_id][read_symbol_in_symbol_data_buffer[cell_id]][byte_index];
				}*/

                        	

			
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
	rte_pause();
	return;
}

void xran_fh_rx_prach_callback(void *pCallbackTag, xran_status_t status){

	
	if(status!=XRAN_STATUS_SUCCESS){
		return;
	}

	prach_callback_tags_to_read_index=(prach_callback_tags_to_read_index+1)%CALLBACK_TAGS_BUFFER_LEN;
	prach_callback_tags_buffer[prach_callback_tags_to_read_index]=*((xran_cb_tag *)pCallbackTag);

	rte_pause(); 
	return;
}

void xran_fh_rx_callback(void *pCallbackTag, xran_status_t status){

	
	if(status!=XRAN_STATUS_SUCCESS){
		return;
	}

	callback_tags_to_read_index=(callback_tags_to_read_index+1)%CALLBACK_TAGS_BUFFER_LEN;
	callback_tags_buffer[callback_tags_to_read_index]=*((xran_cb_tag *)pCallbackTag);

        rte_pause();
        return;
}

void xran_fh_rx_prach_shared_buffer(){


	// Do it after in another separate function
	int num_eaxc = xranlib->get_num_eaxc();
	int num_eaxc_ul = xranlib->get_num_eaxc_ul();
	uint32_t xran_max_antenna_nr = RTE_MAX(num_eaxc, num_eaxc_ul);

	if(prach_callback_tags_to_read_index==prach_callback_tags_already_read_index){
		return;
	}

	// pCallbackTag is a structure which contains the timing and the cell id
	prach_callback_tags_already_read_index=(prach_callback_tags_already_read_index+1)%CALLBACK_TAGS_BUFFER_LEN;
	struct xran_cb_tag *pTag = &prach_callback_tags_buffer[prach_callback_tags_already_read_index];

	uint16_t cell_id = pTag->cellId;
	uint32_t tti = pTag->slotiId;
	uint32_t symbol = pTag->symbol;
	
	// Retrieve the device context which contains information to access the buffer
	p_xran_dev_ctx = xran_dev_get_ctx();
	
	/* The slot and the cell id are fixed
	 * We also know the start symbol and that we have to read half a slot
	 */
	// Loop over the antennas
	for(uint32_t symb_id = 0; symb_id<XRAN_NUM_OF_SYMBOL_PER_SLOT; symb_id++){
		
		// Loop over the symbols
		// for(uint8_t ant_id = 0; ant_id < XRAN_MAX_ANTENNA_NR; ant_id++){
		for(uint8_t ant_id = 0; ant_id < xran_max_antenna_nr; ant_id++){ // just to try
			
			uint32_t nElementLenInBytes = p_xran_dev_ctx->sFHPrachRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
			uint32_t nNumberOfElements = p_xran_dev_ctx->sFHPrachRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
			uint32_t nOffsetInBytes = p_xran_dev_ctx->sFHPrachRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
			//uint32_t nIsPhyAddr = p_xran_dev_ctx->sFHPrachRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
			uint8_t *pData = p_xran_dev_ctx->sFHPrachRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
			void *pCtrl = p_xran_dev_ctx->sFHPrachRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
			
			// Copy data to Intermediate Buffer
			int16_t payload_len=0;
			if(PRACH_PLAYBACK_BUFFER_BYTES<nElementLenInBytes){
				payload_len=PRACH_PLAYBACK_BUFFER_BYTES;
			}else{
				payload_len=nElementLenInBytes;
			}
			uint32_t *dst=(uint32_t *)prach_symbol_data_buffer[cell_id][ant_id][prach_write_symbol_in_symbol_data_buffer[cell_id]];
			uint32_t *src=(uint32_t *)pData;
                        rte_memcpy(dst, src, payload_len);

			for(int index=0; index<100; index++){
				if(dst[index]!=0)
					RX_bytes_prach++;
			}

		}

		// Increment Intermediate Buffer Index
		prach_write_symbol_in_symbol_data_buffer[cell_id]=prach_write_symbol_in_symbol_data_buffer[cell_id]+1;
		if (prach_write_symbol_in_symbol_data_buffer[cell_id]==SYMBOL_BUFFER_LEN){
			prach_write_symbol_in_symbol_data_buffer[cell_id]=0;
		}

	}
	return;
}

void xran_fh_rx_read_shared_buffer(){ 

// Do it after in another separate function
	int num_eaxc = xranlib->get_num_eaxc();
	int num_eaxc_ul = xranlib->get_num_eaxc_ul();
	uint32_t xran_max_antenna_nr = RTE_MAX(num_eaxc, num_eaxc_ul);

	if(callback_tags_to_read_index==callback_tags_already_read_index){
		return;
	}

	// pCallbackTag is a structure which contains the timing and the cell id
	callback_tags_already_read_index=(callback_tags_already_read_index+1)%CALLBACK_TAGS_BUFFER_LEN;
	struct xran_cb_tag *pTag = &callback_tags_buffer[callback_tags_already_read_index];

	uint16_t cell_id = pTag->cellId;
	uint32_t tti = pTag->slotiId;
	uint32_t symbol = pTag->symbol;
	
	// Retrieve the device context which contains information to access the buffer
	p_xran_dev_ctx = xran_dev_get_ctx();
	
	/* The slot and the cell id are fixed
	 * We also know the start symbol and that we have to read half a slot
	 */
	
	// printf to see if receiving data from RU
	//printf("receiving data from RU, symbol %d in intermediate buffer, cell %d\n",write_symbol_in_symbol_data_buffer[cell_id],cell_id);
	// Loop over the antennas
	for(uint32_t symb_id = symbol; symb_id<symbol+7; symb_id++){
		
		// Loop over the symbols
		// for(uint8_t ant_id = 0; ant_id < XRAN_MAX_ANTENNA_NR; ant_id++){
		for(uint8_t ant_id = 0; ant_id < xran_max_antenna_nr; ant_id++){ // just to try
			
			uint32_t nElementLenInBytes = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
			uint32_t nNumberOfElements = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
			uint32_t nOffsetInBytes = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
			uint32_t nIsPhyAddr = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
			uint8_t *pData = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
			void *pCtrl = p_xran_dev_ctx->sFrontHaulRxBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;
			
			// Copy data to Intermediate Buffer
			int16_t payload_len=0;
			if(SYMBOL_DATA_SIZE<nElementLenInBytes){
				payload_len=SYMBOL_DATA_SIZE;
			}else{
				payload_len=nElementLenInBytes;
			}
			uint8_t *dst=(uint8_t *)symbol_data_buffer[cell_id][ant_id][write_symbol_in_symbol_data_buffer[cell_id]];
			uint8_t *src=(uint8_t *)pData;
                        rte_memcpy(dst, src, payload_len);
			/*for (int byte_index=0; byte_index<SYMBOL_DATA_SIZE && byte_index<nElementLenInBytes; byte_index++){
				symbol_data_buffer[cell_id][ant_id][write_symbol_in_symbol_data_buffer[cell_id]][byte_index]=pData[byte_index];
			}*/
			

			//if((dst[10]||dst[20]||dst[30]||dst[40]||dst[50]||dst[60])!=0)
			//	printf("Rx callback symbol_id %d, antenna_id %d \n\n",symb_id,ant_id);


			for(int index=0; index<100; index++){
				if(dst[index]!=0)
					RX_bytes++;
			}

			if(symb_id==0 && p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers!=NULL){
                                nElementLenInBytes = p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nElementLenInBytes;
                                nNumberOfElements = p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nNumberOfElements;
                                nOffsetInBytes = p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nOffsetInBytes;
                                nIsPhyAddr = p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].nIsPhyAddr;
                                pData = p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pData;
                                pCtrl = p_xran_dev_ctx->sFrontHaulRxPrbMapBbuIoBufCtrl[tti % XRAN_N_FE_BUF_LEN][cell_id][ant_id].sBufferList.pBuffers[symb_id%XRAN_NUM_OF_SYMBOL_PER_SLOT].pCtrl;

                                struct xran_prb_map prb_map_buffer=*(struct xran_prb_map *)pData;
/*
                                printf("cell_id=%d, ant_id=%d, tti=%d:\n",cell_id,ant_id,tti);
                                printf("dir=%d\n",prb_map_buffer.dir);
                                printf("xran_port=%d\n",prb_map_buffer.xran_port);
                                printf("band_id=%d\n",prb_map_buffer.band_id);
                                printf("cc_id=%d\n",prb_map_buffer.cc_id);
                                printf("ru_port_id=%d\n",prb_map_buffer.ru_port_id);
                                printf("tti_id=%d\n",prb_map_buffer.tti_id);
                                printf("start_sym_id=%d\n",prb_map_buffer.start_sym_id);
                                printf("nPrbElm=%d\n",prb_map_buffer.nPrbElm);
*/
                                //printf("Rx callback symbol_id %d, antenna_id %d \n\n",symb_id,ant_id);
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

// Sofia for Romain: I try to develop a function that fills in the Tx buffer beforee the xran start as done in sample app.
//

void fill_in_tx_buffer(){
   
   // ----
   // Read the valies from the json configuration file using the xran_lib_wrapper functions:
   // ----
   int  numCCPorts_ = xranlib->get_num_cc();     
   int  num_eAxc_   = xranlib->get_num_eaxc(); 
   
   printf("numCCPorts_ =%d, num_eAxc_=%d, MAX_ANT_CARRIER_SUPPORTED =%d\n",numCCPorts_,num_eAxc_,MAX_ANT_CARRIER_SUPPORTED);
 
   int i;
   char *IQ_filename[MAX_ANT_CARRIER_SUPPORTED];
   for(i=0; i<MAX_ANT_CARRIER_SUPPORTED; i++){
      if( (i==0) || (i==1) || (i==2) || (i==3) ){
         IQ_filename[0] = "/home/oba/PISONS/phy/fhi_lib/app/usecase/mu0_5mhz/ant_0.bin";
         IQ_filename[1] = "/home/oba/PISONS/phy/fhi_lib/app/usecase/mu0_5mhz/ant_1.bin";
         IQ_filename[2] = "/home/oba/PISONS/phy/fhi_lib/app/usecase/mu0_5mhz/ant_2.bin";
         IQ_filename[3] = "/home/oba/PISONS/phy/fhi_lib/app/usecase/mu0_5mhz/ant_3.bin"; 
      }else{
          IQ_filename[i] = "";
      }
   }
   uint32_t number_slots =  40;                        // According to wrapper.hpp  uint32_t m_nSlots = 10; but for the file 5MHz is set to 40 
   uint32_t numerology   =  xranlib->get_numerology(); // According to the conf file is mu number
   uint32_t bandwidth    =  5;                         // According to the wrapper.hpp since we are reading the 5MHz files
   uint32_t sub6         =  xranlib->get_sub6(); 
   iq_playback_buffer_size_dl = (number_slots * N_SYM_PER_SLOT * N_SC_PER_PRB * xranlib->get_num_rbs(numerology,bandwidth,sub6)*4L);   
   // iq_playback_buffer_size_dl = 672000;
   // ----
   // List of variables we are using to fill in the tx buffer
   // ----
   // app/src/common.h: #define MAX_ANT_CARRIER_SUPPORTED (XRAN_MAX_SECTOR_NR*XRAN_MAX_ANTENNA_NR)
   // app/src/common.h: extern int32_t tx_play_buffer_size[MAX_ANT_CARRIER_SUPPORTED];
   // app/src/common.h: extern uint8_t numCCPorts
   // app/src/common.h: extern uint8_t num_eAxc
   // app/src/common.h: extern int iq_playback_buffer_size_dl
   // app/src/common.h: #define PRACH_PLAYBACK_BUFFER_BYTES (144*14*4L)
   // app/src/common.h: int  sys_load_file_to_buff(char *filename, char *bufname, unsigned char *pBuffer, unsigned int size, unsigned int buffers_num);
 
   for(i = 0; i < MAX_ANT_CARRIER_SUPPORTED && i < (uint32_t)(numCCPorts_ * num_eAxc_); i++) {
	if(((uint8_t *)IQ_filename[i])[0]!=0){

		p_tx_play_buffer[i]    = (int16_t*)malloc(iq_playback_buffer_size_dl);
        	assert (NULL != (p_tx_play_buffer[i]));
        	// memset(p_tx_play_buffer[i], 0, PRACH_PLAYBACK_BUFFER_BYTES);
	        tx_play_buffer_size[i] = (int32_t)iq_playback_buffer_size_dl;

        	printf("Loading file [%d] %s \n",i,IQ_filename[i]);
        	tx_play_buffer_size[i] = sys_load_file_to_buff( IQ_filename[i],
        	                     "DL IFFT IN IQ Samples in binary format",
        	                     (uint8_t*) p_tx_play_buffer[i],
        	                     tx_play_buffer_size[i],
        	                     1);
        	tx_play_buffer_position[i] = 0;        
	} else {

		p_tx_play_buffer[i]=(int16_t*)malloc(iq_playback_buffer_size_dl);
		tx_play_buffer_size[i]=0;
		tx_play_buffer_position[i] = 0;
		//printf("CC %d file not present in the folder\n",i);

	}
        //printf("tx_play_buffer_size[%d] = %d, tx_play_buffer_position[%d] = %d\n",i,tx_play_buffer_size[i],i,tx_play_buffer_position[i]);

   }

   return;
} 


int physide_dl_tti_call_back(void * param)
{
    //printf("Sofia: called physide_dl \n");
    rte_pause();
    return 0;
}

int physide_ul_half_slot_call_back(void * param)
{
    rte_pause();
    return 0;
}

int physide_ul_full_slot_call_back(void * param)
{
    rte_pause();
    return 0;
}

int main(int argc, char *argv[]){

        int sleep_const = 3;		
	xranlib = new xranLibWraper;
       
        printf("\n\n>>> Call SetUP() function\n");
        if ( xranlib->SetUp() < 0 ) {
           printf("Error in xranlib->Setup function >>> EXIT.\n");
     	   return (-1);
        }else{
           sleep(sleep_const);
           printf(">>> Call SetUP() Done\n");
        }
        
        printf("\n\n>>> Call fill_in_tx_buffer() function\n");
        fill_in_tx_buffer();
        sleep(sleep_const);
        printf(">>> fill_in_tx_buffer() Done\n");

        // Sofia, Roman enable xran_reg_physide_cb(xranHandle, physide_dl_tti_call_back, NULL, 10, XRAN_CB_TTI); for the CP
        xran_reg_physide_cb(xranlib->get_xranhandle(), physide_dl_tti_call_back, NULL, 10, XRAN_CB_TTI);
	xran_reg_physide_cb(xranlib->get_xranhandle(), physide_ul_half_slot_call_back, NULL, 10, XRAN_CB_HALF_SLOT_RX);
	xran_reg_physide_cb(xranlib->get_xranhandle(), physide_ul_full_slot_call_back, NULL, 10, XRAN_CB_FULL_SLOT_RX);
        
        printf("\n\n>>> Call Open() function\n");
        xranlib->Open(NULL, 
                      nullptr, 
                      (void *)xran_fh_rx_callback, 
                      (void *)xran_fh_rx_prach_callback, 
                     (void *)xran_fh_srs_callback);
        sleep(sleep_const);
	printf(">>> Open() function Done\n");

        if(xranlib->is_cpenable() == false ){
           printf("\nCP NOT enabled\n");
        }else{
           printf("\nCP IS enabled! Apply CP enable ...\n");
           if ( xranlib->apply_cpenable(xranlib->is_cpenable()) != 0 ){
              printf("Error applying CP ... Exit >>>\n");
              exit(-1);
           }else{
              printf("CP applied correctly \n");
           }
        }


        printf("\n\n>>> Call Init() function\n");
        xranlib->Init(nullptr);
        sleep(sleep_const);
        printf(">>> Init() function Done\n");

     
        printf("\n\n>>> Call our send symbol function\n");
        send_intermediate_buffer_symbol_test();
        sleep(sleep_const);
        printf(">>> Our send symbol function Done\n");

        printf("\n\n>>>  xran_open(xranlib->get_xranhandle(),pCfg); \n");
        struct xran_fh_config *pCfg = (struct xran_fh_config*) malloc(sizeof(struct xran_fh_config));
        assert(pCfg != NULL);
        xranlib->get_cfg_fh(pCfg);
        xran_open(xranlib->get_xranhandle(),pCfg);
        sleep(sleep_const);
        printf(">>>  xran_open(xranlib->get_xranhandle(),pCfg); --- DONE \n");

        printf("\n\n>>> Call Sart() function\n");	
	if ( xranlib->Start() != 0 ){
           printf("Error starting the xranlib->Start() \n");
        }else{
           printf("xranlib->Start() started correctly\n");
        }
        sleep(sleep_const);
        printf(">>> Start() function Done\n");

	escape_flag=0;
	
	//signal(SIGINT,sigint_handler);
	
	printf("wrapper's initilization done.\n");

        bool res_running = xranlib->is_running();
	if (res_running==false){
           printf("XRAN NOT running\n");
        }else{
           printf("XRAN IS running\n");
        }

        if(xranlib->is_prachenable() == false){ 
           printf("PRACH is NOT enabled\n");
        }else{
           printf("PRACH IS enabled\n");
        }

        if(xranlib->is_dynamicsection() == false ){
           printf("Dynamic Section NOT enabled\n");
        }else{
           printf("Dynamic Section IS enabled\n");
        }


        int64_t count_sec =0;
        struct xran_common_counters x_counters;
        uint64_t nTotalTime;
        uint64_t nUsedTime;
        uint32_t nCoreUsed;
        float nUsedPercent;
        long old_rx_counter = 0;
        long old_tx_counter = 0;


	while(!escape_flag){
      		sleep(1);
 		count_sec++;
		printf("running second: %ld\n",count_sec);
		
                // Call the function that reads the RX buffers
		xran_fh_rx_read_shared_buffer(); 
		xran_fh_rx_prach_shared_buffer();
 		printf("RX_bytes=%d\n",RX_bytes);
		printf("RX_bytes_prach=%d\n",RX_bytes_prach);

 		
		// Compute the statistics
		
		if(xran_get_common_counters(xranlib->get_xranhandle(), &x_counters) == XRAN_STATUS_SUCCESS) {
			xran_get_time_stats(&nTotalTime, &nUsedTime, &nCoreUsed, 1);
                        nUsedPercent = ((float)nUsedTime * 100.0) / (float)nTotalTime;

                        printf("[rx %7ld pps %7ld kbps %7ld][tx %7ld pps %7ld kbps %7ld] [on_time %ld early %ld late %ld corrupt %ld pkt_dupl %ld Total %ld] IO Util: %5.2f %%\n",
                  		 x_counters.rx_counter,
                  		 x_counters.rx_counter-old_rx_counter,
                   		 x_counters.rx_bytes_per_sec*8/1000L,
                  		 x_counters.tx_counter,
	                         x_counters.tx_counter-old_tx_counter,
              			 x_counters.tx_bytes_per_sec*8/1000L,
                   		 x_counters.Rx_on_time,
                		 x_counters.Rx_early,
                   		 x_counters.Rx_late,
                   		 x_counters.Rx_corrupt,
                   		 x_counters.Rx_pkt_dupl,
                   		 x_counters.Total_msgs_rcvd,
                   	         nUsedPercent);

			if(x_counters.rx_counter > old_rx_counter)
                 		old_rx_counter = x_counters.rx_counter;
            		if(x_counters.tx_counter > old_tx_counter)
                		old_tx_counter = x_counters.tx_counter;
        	} else {
           		 printf("error xran_get_common_counters\n");
        	}
		

	}


	xranlib->Stop();

	printf("RX_bytes total=%d\n",RX_bytes);

	printf("\nexiting wrapper.\n");
	
}


























