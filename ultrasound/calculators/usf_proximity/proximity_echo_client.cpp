/*===========================================================================
                           proximity_echo_client.cpp

DESCRIPTION: Implements the proximity echo client class.

INITIALIZATION AND SEQUENCING REQUIREMENTS: None

Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential.
=============================================================================*/
#define LOG_TAG "ProximityEchoClient"

/*----------------------------------------------------------------------------
Include files
----------------------------------------------------------------------------*/
#include "proximity_echo_client.h"

/*----------------------------------------------------------------------------
  Defines
----------------------------------------------------------------------------*/
#define CLIENT_NAME "proximity";
#define DAEMON_NAME "usf_proximity"
#define LINK_CFG_FILE_LOCATION "/data/usf/proximity/usf_proximity.cfg"
#define FRAME_FILE_DIR_PATH "/data/usf/proximity/rec/"

/*-----------------------------------------------------------------------------
  Static Variable Definitions
-----------------------------------------------------------------------------*/
/**
  This struct represents a single proximity event generated
  from the proximity library in the dsp.
*/
struct proximity_dsp_event
{
  int                        timestamp;
  int                        seq_num;
  QcUsProximityLibResultType result;
  int distance;
};

/**
 * The name of the file containg the pid of the daemon
 */
static const char* PID_FILE_NAME = "usf_proximity.pid";

/*------------------------------------------------------------------------------
  Function definitions
------------------------------------------------------------------------------*/

/*==============================================================================
  FUNCTION:  get_directions_subset
==============================================================================*/
/**
 * See function description in header file
 */
int ProximityEchoClient::get_directions_subset(EchoContext *echo_context)
{
  // Gets property value with "0" as default
  char prop_val[PROPERTY_VALUE_MAX];
  property_get("debug.proximity_subset",
               prop_val,
               "0");
  if (strcmp("0", prop_val) < 0)
  {
    int directions = 0;
    sscanf(prop_val, "%d", &directions);
    LOGD("%s: Subset defined by property, %d",
         __FUNCTION__,
         directions);
    return directions;
  }

  if (NULL != echo_context->m_adapter)
  {
    LOGD("%s: Subset defined by adapter, %d",
         __FUNCTION__,
         (uint32_t)echo_context->m_adapter->get_config().sub_mode);
    return (uint32_t)echo_context->m_adapter->get_config().sub_mode;
  }

  return 0;
}

/*============================================================================
  FUNCTION:  get_daemon_name
============================================================================*/
/**
 * See function description in header file
 */
char *ProximityEchoClient::get_daemon_name()
{
  return (char *)DAEMON_NAME;
}

/*============================================================================
  FUNCTION:  get_client_name
============================================================================*/
/**
 * See function description in header file
 */
char *ProximityEchoClient::get_client_name()
{
  return (char *)CLIENT_NAME;
}

/*============================================================================
  FUNCTION:  get_dsp_event_size
============================================================================*/
/**
 * See function description in header file
 */
uint32_t ProximityEchoClient::get_dsp_event_size()
{
  return sizeof(struct proximity_dsp_event);
}

/*==============================================================================
  FUNCTION:  get_pid_file_name
==============================================================================*/
/**
 * See function description in header file
 */
char *ProximityEchoClient::get_pid_file_name()
{
  return (char *)PID_FILE_NAME;
}
/*============================================================================
  FUNCTION:  get_num_keyboard_keys
============================================================================*/
/**
 * See function description in header file
 */
int ProximityEchoClient::get_num_keyboard_keys()
{
  return ((QC_US_PROXIMITY_LIB_RESULT_DETECTED - QC_US_PROXIMITY_LIB_RESULT_IDLE) + 1);
}

/*============================================================================
  FUNCTION:  get_num_events
============================================================================*/
/**
 * See function description in header file
 */
int ProximityEchoClient::get_num_events()
{
  return US_MAX_EVENTS;
}

/*============================================================================
  FUNCTION:  get_keyboard_key_base
============================================================================*/
/**
 * See function description in header file
 */
int ProximityEchoClient::get_keyboard_key_base()
{
  return QC_US_PROXIMITY_LIB_RESULT_IDLE;
}

/*==============================================================================
  FUNCTION:  set_algo_dynamic_params
==============================================================================*/
/**
 * See function description in header file
 */
void ProximityEchoClient::set_algo_dynamic_params(us_all_info *paramsStruct,
                                                  EchoContext *echo_context)
{
  // TODO should construct a binary file to send to algo + define module
  // ids and implement this section below for proximity
}

/*==============================================================================
  FUNCTION:  get_cfg_path
==============================================================================*/
/**
 * See function description in header file
 */
char *ProximityEchoClient::get_cfg_path()
{
  return (char *)LINK_CFG_FILE_LOCATION;
}

/*==============================================================================
  FUNCTION:  get_frame_file_path
==============================================================================*/
/**
 * See function description in header file
 */
char *ProximityEchoClient::get_frame_file_path()
{
  return (char *)FRAME_FILE_DIR_PATH;
}

/*==============================================================================
  FUNCTION:  echo_lib_init
==============================================================================*/
/**
 * See function description in header file
 */
void ProximityEchoClient::echo_lib_init(us_all_info *paramsStruct,
                                        EchoContext *echo_context)
{
  ProximityCfg *proximity_config = NULL;
  uint32_t workspace_size = 0, pattern_size_samples = 0;

  echo_context->m_echo_workspace = NULL;
  proximity_config = (ProximityCfg *) malloc(sizeof(ProximityCfg) +
                                  paramsStruct->usf_algo_transparent_data_size);
  if (NULL == proximity_config)
  {
    LOGE("%s: Failed to allocate.",
         __FUNCTION__);
    echo_exit(EXIT_FAILURE);
  }

  // Fill in the requested algorithm configuration
  proximity_config->TxTransparentDataSize = paramsStruct->usf_algo_transparent_data_size;
  memcpy(proximity_config->TxTransparentData,
         paramsStruct->usf_algo_transparent_data,
         proximity_config->TxTransparentDataSize);

  int ret = QcUsProximityLibGetSizes(proximity_config,
                                     &workspace_size,
                                     &pattern_size_samples);
  if (ret)
  {
    free(proximity_config);
    LOGE("%s: Error while getting size from proximity library",
         __FUNCTION__);
    echo_exit(EXIT_FAILURE);
  }

  echo_context->m_pattern_size = pattern_size_samples *
    (paramsStruct->usf_tx_sample_width / BYTE_WIDTH);

  echo_context->m_echo_workspace =
    (signed char *)malloc(workspace_size * sizeof(signed char));
  if (NULL == echo_context->m_echo_workspace)
  {
    free(proximity_config);
    LOGE("%s: Failed to allocate %d bytes.",
         __FUNCTION__,
         workspace_size);
    echo_exit(EXIT_FAILURE);
  }

  echo_context->m_pattern = (int16_t *) malloc(echo_context->m_pattern_size);
  if (NULL == echo_context->m_pattern)
  {
    free(proximity_config);
    LOGE("%s: Failed to allocate %d bytes",
         __FUNCTION__,
         echo_context->m_pattern_size);
    echo_exit(EXIT_FAILURE);
  }

  ret = QcUsProximityLibInit(proximity_config,
                             echo_context->m_echo_workspace,
                             workspace_size);
  if (ret)
  {
    free(proximity_config);
    LOGE("%s: Init algorithm failed.",
         __FUNCTION__);
    echo_exit(0);
  }
  free(proximity_config);

  /*
  usm_param_id_proximity_dynanic_cfg_t dynamic_config_payload;
  dynamic_config_payload.sync_gesture_dynamic_cfg_version =
    USM_API_VERSION_SYNC_GESTURE_DYNAMIC_CONFIG;
  dynamic_config_payload.directions = get_directions_subset(echo_context);

  if (0 < dynamic_config_payload.directions)
  {
    ret = QcUsGestureLibSetDynamicConfig(
      (int *)&dynamic_config_payload.sync_gesture_dynamic_cfg_version,
      sizeof(dynamic_config_payload)/sizeof(int));
    if (ret)
    {
      LOGE("%s: Set dynamic config failed.",
           __FUNCTION__);
      echo_exit(0);
    }
  }
  */

  ret = QcUsProximityLibGetPattern(echo_context->m_pattern,
                                   pattern_size_samples);
  if (ret)
  {
    LOGE("%s: QcUsProximityLibGetPattern failed.",
         __FUNCTION__);
    echo_exit(0);
  }

  LOGI("%s: Received pattern from library with %d samples",
       __FUNCTION__,
       pattern_size_samples);

  LOGI("%s: Proximity lib init completed.",
       __FUNCTION__);
}

/*==============================================================================
  FUNCTION:  get_event_output_type
==============================================================================*/
/**
 * See function description in header file
 */
uint32_t ProximityEchoClient::get_event_output_type()
{
  return OUTPUT_TYPE_PROXIMITY_EVENT_MASK;
}

/*==============================================================================
  FUNCTION:  get_proximity_output_decision
==============================================================================*/
/**
 * See function description in header file
 */
ProximityOutput ProximityEchoClient::get_proximity_output_decision(short *packet,
                                                                   us_all_info *paramsStruct)
{
  QcUsProximityLibResultType dsp_result = QC_US_PROXIMITY_LIB_RESULT_IDLE;
  ProximityOutput proximity_lib_output;
  proximity_lib_output.proximity = QC_US_PROXIMITY_LIB_RESULT_IDLE;
  int distance = 0;

  if (paramsStruct->usf_output_type & OUTPUT_TYPE_PROXIMITY_EVENT_MASK)
  { // Events handling
    struct proximity_dsp_event *dsp_event = (struct proximity_dsp_event *) packet;
    dsp_result = dsp_event->result;
    distance = dsp_event->distance;
    // Update pointer to point to next raw data place
    packet += get_dsp_event_size() / sizeof(short);
  }

  if ((paramsStruct->usf_output_type & OUTPUT_TYPE_RAW_MASK) &&
      !paramsStruct->usf_app_lib_bypass)
  { // Raw data handling
    int rc = QcUsProximityLibEngine(packet,
                                    &proximity_lib_output);
    if (rc)
    {
      LOGE("%s: QcUsProximityLibEngine failed.",
           __FUNCTION__);
      echo_exit(EXIT_FAILURE);
    }
  }

  /* TODO: uncomment when dsp and apps libraries report accurate events.
  if (QC_US_GESTURE_LIB_RESULT_IDLE != proximity_lib_output.gesture &&
      QC_US_GESTURE_LIB_RESULT_IDLE != dsp_result &&
      proximity_lib_output.gesture != dsp_result)
  {
    LOGW("%s: Library result and DSP result are not the same, lib: %d, dsp: %d",
         __FUNCTION__,
         proximity_lib_output.gesture,
         dsp_result);
  }
  */

  // APPS result has the priority over DSP result
  if (QC_US_PROXIMITY_LIB_RESULT_IDLE != proximity_lib_output.proximity)
  {
    return proximity_lib_output;
  }
  else if (QC_US_PROXIMITY_LIB_RESULT_IDLE != dsp_result)
  {
    proximity_lib_output.proximity = dsp_result;
    proximity_lib_output.distance = distance;
    return proximity_lib_output;
  }
  proximity_lib_output.proximity = QC_US_PROXIMITY_LIB_RESULT_IDLE;
  return proximity_lib_output;
}

/*==============================================================================
  FUNCTION:  get_directions_subset
==============================================================================*/
/**
 * See function description in header file
 */
bool ProximityEchoClient::is_different_output_decision(ProximityOutput current_decision,
                                                       ProximityOutput previous_decision)
{
  if (current_decision.proximity != previous_decision.proximity ||
      current_decision.distance != previous_decision.distance)
  {
    return true;
  }
  return false;
}

/*==============================================================================
  FUNCTION:  get_points
==============================================================================*/
/**
 * See function description in header file
 */
int ProximityEchoClient::get_points(EchoContext *echo_context,
                                    short *packet,
                                    us_all_info *paramsStruct)
{
  int rc = 1;
  ProximityOutput proximity_output;
  static ProximityOutput last_proximity = {QC_US_PROXIMITY_LIB_RESULT_NOT_READY, -1};
  proximity_output = get_proximity_output_decision(packet,
                                                   paramsStruct);

  int mapped_proximity = (proximity_output.proximity > 0) ?
                         echo_context->m_keys[proximity_output.proximity - echo_context->m_echo_client->get_keyboard_key_base()] : 0;

  event_source_t event_source = ((paramsStruct->usf_output_type & OUTPUT_TYPE_RAW_MASK) &&
                                 !paramsStruct->usf_app_lib_bypass) ?
                       EVENT_SOURCE_APSS : EVENT_SOURCE_DSP;

  // Send socket event
  if (echo_context->m_send_points_to_socket &&
      is_different_output_decision(proximity_output,
                                   last_proximity))
  {
    LOGD("%s: proximity[%d], distance: %d",
         __FUNCTION__,
         proximity_output.proximity,
         proximity_output.distance);

    if (NULL != echo_context->m_adapter)
    {
      int event = (echo_context->m_adapter->get_event_mapping() == MAPPED) ?
        // Framework requests mapped events
                  mapped_proximity :
        // Framework requests raw events
                  proximity_output.proximity;
      if (1 == echo_context->m_adapter->send_event(event, event_source, proximity_output.distance))
      {
        LOGE("%s: adapter send_event failed.",
             __FUNCTION__);
      }
    }
  }
  last_proximity = proximity_output;

  if ((proximity_output.proximity > 0) &&
      (proximity_output.proximity <= (int)(sizeof(echo_context->m_keys) /
                                sizeof(echo_context->m_keys[0])) ) )
  {
    // There is a proximity.
    // Assumption: a key duration << time between 2 proximities.

    // Send start press to UAL
    if (echo_context->m_send_points_to_ual)
    {
      add_event_key (mapped_proximity, 1);
      add_event_key (mapped_proximity, 0);
    }

    echo_context->m_echoStats.m_nPointsCalculated++;
  }

  return echo_context->m_s_eventCounter;
}
