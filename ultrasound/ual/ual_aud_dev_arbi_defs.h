#ifndef __UAL_AUD_DEV_ARBI_DEFS_H__
#define __UAL_AUD_DEV_ARBI_DEFS_H__

/*============================================================================
                           ual_aud_dev_arbi_defs.h

DESCRIPTION:  Some definitions, related to audio device arbitration

Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential.
==============================================================================*/

/*-----------------------------------------------------------------------------
  Include Files
-----------------------------------------------------------------------------*/
#include <hardware/audio.h>

/*-----------------------------------------------------------------------------
  Typedefs
-----------------------------------------------------------------------------*/
typedef void* aud_dev_arbi_client_handle_t;

typedef enum {
    AUD_DEV_ARBI_EVENT_DEVICE_REQUESTED = 0,
    AUD_DEV_ARBI_EVENT_DEVICE_RELEASED,
    AUD_DEV_ARBI_NUM_EVENTS
} aud_dev_arbi_event_t;

typedef enum {
    AUD_DEV_ARBI_EVENT_CLIENT_UAL = 0,
    AUD_DEV_ARBI_NUM_CLIENTS
} aud_dev_arbi_client_t;

typedef void (*aud_dev_arbi_event_cb_t)(audio_devices_t, aud_dev_arbi_event_t);
#endif // __UAL_AUD_DEV_ARBI_DEFS_H__
