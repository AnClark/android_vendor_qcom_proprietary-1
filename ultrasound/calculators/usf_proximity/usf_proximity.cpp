/*===========================================================================
                           usf_proximity.cpp

DESCRIPTION: Implementation of the Synchronized Proximity daemon.


INITIALIZATION AND SEQUENCING REQUIREMENTS:
  If not started through java app then make sure to have
  correct /data/usf/proximity/usf_proximity.cfg file linked to the wanted cfg file
  placed in /data/usf/proximity/cfg/.

Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential.
=============================================================================*/
#define LOG_TAG "usf_proximity"

/*----------------------------------------------------------------------------
  Include files
----------------------------------------------------------------------------*/
#include "usf_echo_calculator.h"
#include "proximity_echo_client.h"

/*----------------------------------------------------------------------------
  Defines
----------------------------------------------------------------------------*/

/*==============================================================================
  FUNCTION:  main
==============================================================================*/
/**
  Main function of the Proximity daemon. Handle all the Proximity operations.
*/
int main (void)
{
  EchoClient *proximity_client = new ProximityEchoClient();
  echo_calculator_start(proximity_client);
}
