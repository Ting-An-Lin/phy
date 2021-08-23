/******************************************************************************
*
*   Copyright (c) 2019 Intel.
*
*   Licensed under the Apache License, Version 2.0 (the "License");
*   you may not use this file except in compliance with the License.
*   You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
*   Unless required by applicable law or agreed to in writing, software
*   distributed under the License is distributed on an "AS IS" BASIS,
*   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*   See the License for the specific language governing permissions and
*   limitations under the License.
*
*******************************************************************************/

/**
 * @brief This file provides implementation to Timing for XRAN.
 *
 * @file xran_timer.c
 * @ingroup group_lte_source_xran
 * @author Intel Corporation
 *
 **/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "xran_timer.h"
#include "xran_printf.h"
#include "xran_mlog_lnx.h"
#include "xran_lib_mlog_tasks_id.h"
#include "ethdi.h"
#include "xran_fh_o_du.h"
#include "xran_common.h"

#define NSEC_PER_SEC  1000000000L
#define NSEC_PER_USEC 1000L
#define THRESHOLD        35  /**< the avg cost of clock_gettime() in ns */
#define TIMECOMPENSATION 2   /**< time compensation in us, avg latency of clock_nanosleep */

#define SEC_MOD_STOP (60)

static struct timespec started_time;
static struct timespec last_time;
static struct timespec cur_time;

static uint64_t  curr_tick;
static uint64_t  last_tick;

static struct timespec* p_cur_time = &cur_time;
static struct timespec* p_last_time = &last_time;


static struct timespec* p_temp_time;

static struct timespec sleeptime = {.tv_nsec = 1E3 }; /* 1 us */

static unsigned long current_second = 0;
static unsigned long started_second = 0;
static uint8_t numerlogy = 0;
extern uint32_t xran_lib_ota_sym;
extern uint32_t xran_lib_ota_tti;
extern uint32_t xran_lib_ota_sym_idx;

static int debugStop = 0;
static int debugStopCount = 0;

static long fine_tuning[5][2] =
{
    {71428L, 71429L},  /* mu = 0 */
    {35714L, 35715L},  /* mu = 1 */
    {0, 0},            /* mu = 2 not supported */
    {8928L, 8929L},    /* mu = 3 */
    {0,0  }            /* mu = 4 not supported */
};

static uint8_t slots_per_subframe[4] =
{
    1,  /* mu = 0 */
    2,  /* mu = 1 */
    4,  /* mu = 2 */
    8,  /* mu = 3 */
};

uint64_t timing_get_current_second(void)
{
    return current_second;
}

int timing_set_numerology(uint8_t value)
{
    numerlogy = value;
    return numerlogy;
}

int timing_set_debug_stop(int value, int count)
{
    debugStop = value;
    debugStopCount = count;

    if(debugStop){
        clock_gettime(CLOCK_REALTIME, &started_time);
        started_second =started_time.tv_sec;
    }
    return debugStop;
}

int timing_get_debug_stop(void)
{
    return debugStop;
}

void timing_adjust_gps_second(struct timespec* p_time)
{
    struct xran_device_ctx * p_xran_dev_ctx = xran_dev_get_ctx();

    if (p_time->tv_nsec >= p_xran_dev_ctx->offset_nsec)
    {
        p_time->tv_nsec -= p_xran_dev_ctx->offset_nsec;
        p_time->tv_sec -= p_xran_dev_ctx->offset_sec;
    }
    else
    {
        p_time->tv_nsec += 1e9 - p_xran_dev_ctx->offset_nsec;
        p_time->tv_sec -= p_xran_dev_ctx->offset_sec + 1;
    }

    return;
}
uint64_t xran_tick(void)
{
    uint32_t hi, lo;
    __asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
}

unsigned long get_ticks_diff(unsigned long curr_tick, unsigned long last_tick)
{
    if (curr_tick >= last_tick)
        return (unsigned long)(curr_tick - last_tick);
    else
        return (unsigned long)(0xFFFFFFFFFFFFFFFF - last_tick + curr_tick);
}

long poll_next_tick(long interval_ns, unsigned long *used_tick)
{
    struct xran_device_ctx * p_xran_dev_ctx = xran_dev_get_ctx();
    struct xran_common_counters* pCnt = &p_xran_dev_ctx->fh_counters;

    long target_time;
    long delta;
    static int counter = 0;
    static long sym_acc = 0;
    static long sym_cnt = 0;

    if(counter == 0) {
       clock_gettime(CLOCK_REALTIME, p_last_time);
       last_tick = MLogTick();
       if(unlikely(p_xran_dev_ctx->offset_sec || p_xran_dev_ctx->offset_nsec))
           timing_adjust_gps_second(p_last_time);
       current_second = p_last_time->tv_sec;
       counter = 1;
    }

    target_time = (p_last_time->tv_sec * NSEC_PER_SEC + p_last_time->tv_nsec + interval_ns);

    while(1) {
        clock_gettime(CLOCK_REALTIME, p_cur_time);
        curr_tick = MLogTick();
        if(unlikely(p_xran_dev_ctx->offset_sec || p_xran_dev_ctx->offset_nsec))
            timing_adjust_gps_second(p_cur_time);
        delta = (p_cur_time->tv_sec * NSEC_PER_SEC + p_cur_time->tv_nsec) - target_time;
        if(delta > 0 || (delta < 0 && abs(delta) < THRESHOLD)) {
            if (debugStop &&(debugStopCount > 0) && (pCnt->tx_counter >= debugStopCount)){
                uint64_t t1;
                printf("STOP:[%ld.%09ld], debugStopCount %d, tx_counter %ld\n", p_cur_time->tv_sec, p_cur_time->tv_nsec, debugStopCount, pCnt->tx_counter);
                t1 = MLogTick();
                rte_pause();
                MLogTask(PID_TIME_SYSTIME_STOP, t1, MLogTick());
                xran_if_current_state = XRAN_STOPPED;
            }
            if(current_second != p_cur_time->tv_sec){
                current_second = p_cur_time->tv_sec;
                xran_updateSfnSecStart();
                xran_lib_ota_sym_idx = 0;
                xran_lib_ota_tti = 0;
                xran_lib_ota_sym = 0;
                sym_cnt = 0;
                sym_acc = 0;
                print_dbg("ToS:C Sync timestamp: [%ld.%09ld]\n", p_cur_time->tv_sec, p_cur_time->tv_nsec);
                if(debugStop){
                    if(p_cur_time->tv_sec > started_second && ((p_cur_time->tv_sec % SEC_MOD_STOP) == 0)){
                        uint64_t t1;
                        printf("STOP:[%ld.%09ld]\n", p_cur_time->tv_sec, p_cur_time->tv_nsec);
                        t1 = MLogTick();
                        rte_pause();
                        MLogTask(PID_TIME_SYSTIME_STOP, t1, MLogTick());
                        xran_if_current_state = XRAN_STOPPED;
                    }
                }
                p_cur_time->tv_nsec = 0; // adjust to 1pps
            } else {
                xran_lib_ota_sym_idx = XranIncrementSymIdx(xran_lib_ota_sym_idx, XRAN_NUM_OF_SYMBOL_PER_SLOT*slots_per_subframe[numerlogy]);
                /* adjust to sym boundary */
                if(sym_cnt & 1)
                    sym_acc +=  fine_tuning[numerlogy][0];
                else
                    sym_acc +=  fine_tuning[numerlogy][1];
                /* fine tune to second boundary */
                if(sym_cnt % 13 == 0)
                    sym_acc += 1;

                p_cur_time->tv_nsec = sym_acc;
                sym_cnt++;
            }

#ifdef USE_PTP_TIME
            if(debugStop && delta < interval_ns*10)
                MLogTask(PID_TIME_SYSTIME_POLL, (p_last_time->tv_sec * NSEC_PER_SEC + p_last_time->tv_nsec), (p_cur_time->tv_sec * NSEC_PER_SEC + p_cur_time->tv_nsec));
#else
            MLogTask(PID_TIME_SYSTIME_POLL, last_tick, curr_tick);
            last_tick = curr_tick;
#endif


            p_temp_time = p_last_time;
            p_last_time = p_cur_time;
            p_cur_time  = p_temp_time;
            break;
        } else {
            if( likely(xran_if_current_state == XRAN_RUNNING)){
                uint64_t t1, t2;
                t1 = xran_tick();

                if(p_xran_dev_ctx->fh_init.io_cfg.pkt_proc_core == 0)
                    ring_processing_func();

                process_dpdk_io();

                /* work around for some kernel */
                if(p_xran_dev_ctx->fh_init.io_cfg.io_sleep)
                    nanosleep(&sleeptime,NULL);

                t2 = xran_tick();
                *used_tick += get_ticks_diff(t2, t1);
            }

        }
  }

  return delta;
}

long sleep_next_tick(long interval)
{
   struct timespec start_time;
   struct timespec cur_time;
   //struct timespec target_time_convert;
   struct timespec sleep_target_time_convert;
   long target_time;
   long sleep_target_time;
   long delta;

   clock_gettime(CLOCK_REALTIME, &start_time);
   target_time = (start_time.tv_sec * NSEC_PER_SEC + start_time.tv_nsec + interval * NSEC_PER_USEC) / (interval * NSEC_PER_USEC) * interval;
   //printf("target: %ld, current: %ld, %ld\n", target_time, start_time.tv_sec, start_time.tv_nsec);
   sleep_target_time = target_time - TIMECOMPENSATION;
   sleep_target_time_convert.tv_sec = sleep_target_time * NSEC_PER_USEC / NSEC_PER_SEC;
   sleep_target_time_convert.tv_nsec = (sleep_target_time * NSEC_PER_USEC) % NSEC_PER_SEC;

   //target_time_convert.tv_sec = target_time * NSEC_PER_USEC / NSEC_PER_SEC;
   //target_time_convert.tv_nsec = (target_time * NSEC_PER_USEC) % NSEC_PER_SEC;

   clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sleep_target_time_convert, NULL);

   clock_gettime(CLOCK_REALTIME, &cur_time);

   delta = (cur_time.tv_sec * NSEC_PER_SEC + cur_time.tv_nsec) - target_time * NSEC_PER_USEC;

   return delta;
}



