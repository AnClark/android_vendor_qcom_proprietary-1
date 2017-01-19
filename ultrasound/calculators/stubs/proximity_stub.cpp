/*===========================================================================
                           proximity_stub.cpp

DESCRIPTION: Provide stub to Proximity library


INITIALIZATION AND SEQUENCING REQUIREMENTS: None

Copyright (c) 2012-2013 Qualcomm Technologies, Inc.  All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential.
=============================================================================*/

#include "usf_log.h"
#include<ProximityExports.h>

#define STUB_MSG_INTERVAL 5000

/*============================================================================
  FUNCTION:  QcUsProximityLibGetSizes
============================================================================*/
/**
  Returns the size of the work space needed to the algorithm.
*/
extern int QcUsProximityLibGetSizes(ProximityCfg const *pConfig,
                                    uint32_t *pWorkspaceSize,
                                    uint32_t *pPatternSizeSamples)
{
  LOGW("%s: Stub.",
       __FUNCTION__);
  *pWorkspaceSize = 1;
  *pPatternSizeSamples = 1;
  return 0;
}
/*============================================================================
  FUNCTION:  QcUsProximityLibUpdatePattern
============================================================================*/
extern int QcUsProximityLibGetPattern(int16_t *pPattern,
                                      uint32_t patternSize)
{
  LOGW("%s: Stub.",
       __FUNCTION__);
  return 0;
}

/*============================================================================
  FUNCTION:  QcUsProximityLibInit
============================================================================*/
/**
  Init the Proximity algorithm.
*/
extern int QcUsProximityLibInit(ProximityCfg const *pConfig,
                                int8_t *pWorkspace,
                                uint32_t workspaceSize)
{
  LOGW("%s: Stub.",
       __FUNCTION__);
  return 0;
}

extern int QcUsProximityLibEngine(int16_t const *pSamplesBuffer,
                                  ProximityOutput *pProximity)
{
  static int print_stub_msg_counter = STUB_MSG_INTERVAL;
  if (STUB_MSG_INTERVAL == print_stub_msg_counter)
  {
    LOGW("%s: Stub.",
         __FUNCTION__);
    print_stub_msg_counter = 0;
  }
  else
  {
    print_stub_msg_counter++;
  }
  return 0;
}

extern int QcUsProximityLibSetDynamicConfig(int* pDynamicConfig,
                                            uint32_t dynamicConfigSize)
{
  LOGW("%s: Stub.",
       __FUNCTION__);
  return 0;
}
