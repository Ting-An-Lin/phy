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

void xran_fh_rx_callback(void *pCallbackTag, xran_status_t status){
    return;
}
void xran_fh_srs_callback(void *pCallbackTag, xran_status_t status){
    return;
}
void xran_fh_rx_prach_callback(void *pCallbackTag, xran_status_t status){
    rte_pause();
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
}



























