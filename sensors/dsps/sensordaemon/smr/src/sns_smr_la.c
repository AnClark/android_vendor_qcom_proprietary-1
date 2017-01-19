#define __SNS_MODULE__ SNS_SMR

#define SNS_SMR_LA_C
/*============================================================================
  @file sns_smr_la.c

  @brief
  This file contains the implementation of SMR internal
  functions for Linux Andorid

  Copyright (c) 2010-2013 Qualcomm Technologies, Inc.  All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.
============================================================================*/

/*============================================================================

                                INCLUDE FILES

============================================================================*/
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "oi_support_init.h"
#include "sns_common.h"
#include "sns_debug_api.h"
#include "sns_debug_str.h"
#include "sns_em.h"
#include "sns_init.h"
#include "sns_log_api.h"
#include "sns_log_types.h"
#include "sns_main.h"
#include "sns_memmgr.h"
#include "sns_osa.h"
#include "sns_pwr.h"
#include "sns_queue.h"
#include "sns_smr.h"
#include "sns_smr_priv.h"

/* definition of millisecond sleep */
#define sns_os_msleep(x) usleep((x)*1000)

/* Android defines PRI.PTR incorrectly. Fix it here */
#ifdef SNS_LA
#  undef PRIxPTR
#  define PRIxPTR "x"
#endif /* SNS_LA */

/*===========================================================================

                         INTERNAL FUNCTION PROTOTYPES

===========================================================================*/

/*===========================================================================

  FUNCTION:   smr_print_heap_summary

===========================================================================*/
/*!
  @brief This function prints the summary of the heap memory

  @detail

  @param[i] none

  @return
   None
*/
/*=========================================================================*/
#ifdef USE_NATIVE_MALLOC
void smr_print_heap_summary (void)
{
  return;
}
#else
#define NUM_BLOCK 10
extern uint32_t PoolFreeCnt[];
extern const OI_MEMMGR_POOL_CONFIG memmgrPoolTable[];

void smr_print_heap_summary (void)
{
  static bool     is_block_counted = false;
  static uint32_t tot_block_cnt = 0;
  uint32_t        i, tot_free_cnt = 0;

  SNS_PRINTF_STRING_FATAL_0(SNS_DBG_MOD_APPS_SMR,
                            "prints heap_summary");
  if ( !is_block_counted )
  {
    for ( i = 0; i < NUM_BLOCK; i++)
    {
      tot_block_cnt += memmgrPoolTable[i].numBlocks;
    }
    is_block_counted = true;
  }
  for ( i = 0; i < NUM_BLOCK; i++)
  {
    tot_free_cnt += PoolFreeCnt[i];
    SNS_PRINTF_STRING_FATAL_3(SNS_DBG_MOD_APPS_SMR,
                              "Free Cnt[%5d] = %d/%d",
                              memmgrPoolTable[i].blockSize, PoolFreeCnt[i], memmgrPoolTable[i].numBlocks);
  }
  SNS_PRINTF_STRING_FATAL_2(SNS_DBG_MOD_APPS_SMR,
                            "Total Free Cnt  = %d/%d", tot_free_cnt, tot_block_cnt);
}
#endif

/*===========================================================================

  FUNCTION:   smr_print_queue_summary

===========================================================================*/
/*!
  @brief This function prints the summary of SMR queues

  @detail

  @param[i] none

  @return
   None
*/
/*=========================================================================*/
static void smr_print_queue_summary (void)
{
#if !defined(SNS_QMI_ENABLE)  // TODO: A more elegant approach
  uint32_t        i;

  SNS_PRINTF_STRING_FATAL_0(SNS_DBG_MOD_APPS_SMR,
                            "prints msg cnt in queue: module");
  /* prints SMR queue summary  */
  for ( i = 0; i < SNS_MODULE_CNT; i++ )
  {
    uint32_t que_cnt;
    sns_smr_msg_pri_e      pri_idx;
    smr_que_entry_s *que_entry_ptr;
    que_entry_ptr = &sns_smr.smr_que_tb[i];

#ifdef SMR_PRIORITY_QUE_ON
    for ( pri_idx = SNS_SMR_MSG_PRI_LOW; pri_idx <= SNS_SMR_MSG_PRI_HIGH; pri_idx++)
#else
    for ( pri_idx = SNS_SMR_MSG_PRI_LOW; pri_idx <= SNS_SMR_MSG_PRI_LOW; pri_idx++)
#endif
    {
      que_cnt = sns_q_cnt (que_entry_ptr->q_ptr[pri_idx]);
      if ( que_cnt )
      {
        SNS_PRINTF_STRING_FATAL_3(SNS_DBG_MOD_APPS_SMR,
                              "msg cnt in queue: module = %d, pri=%d, cnt=%d", i, pri_idx, que_cnt);
      }
    }
  }
#endif /* !defined(SNS_QMI_ENABLED) */
}

/*===========================================================================

  FUNCTION:   smr_print_queue_and_heap_summary

===========================================================================*/
/*!
  @brief This function prints the summary of SMR queues and the heap memory

  @detail

  @param[i] none

  @return
   None
*/
/*=========================================================================*/
int smr_print_queue_and_heap_summary (void)
{
  smr_print_heap_summary();
  smr_print_queue_summary();
  return 0;
}
