/******************************************************************************

                        N E T M G R _ I W L A N _C L I E N T . CPP

******************************************************************************/

/******************************************************************************

  @file    netmgr_iwlan_client.cpp
  @brief   Network manager iWLAN client

  DESCRIPTION
  Implementation of iwlan client module

******************************************************************************/
/*===========================================================================

  Copyright (c) 2014 Qualcomm Technologies, Inc. All Rights Reserved

  Qualcomm Technologies Proprietary and Confidential.

===========================================================================*/

/*===========================================================================
                              INCLUDE FILES
===========================================================================*/
#ifdef FEATURE_DATA_IWLAN
extern "C" {
  #include <stdio.h>
  #include "qmi_client.h"
  #include "qmi_cci_target.h"
  #include "qmi_cci_common.h"
  #include "qmi_client_instance_defs.h"
  #include "qmi_platform.h"
  #include "ds_util.h"
  #include "data_system_determination_v01.h"
  #include <arpa/inet.h>
}

#include "netmgr_iwlan_client.h"
#include "CneFactory.h"
#include "ICneNetworkObserver.h"
#include "ICneNetworkObserverNotifier.h"
#include "ICneFeatureObserver.h"
#include "ICneFeatureObserverNotifier.h"

#include <pthread.h>

#define NETMGR_SUCCESS (0)
#define NETMGR_FAILURE (-1)

#define QMI_DSD_SYNC_MSG_TIMEOUT        (10000)
#define NETMGR_CLIENT_DEFAULT_DEV_ID    "rmnet0"
#define NETMGR_CLIENT_MDMUSB_DEV_ID     "rmnet_usb0"
#define NETMGR_CLIENT_MSM_DEV_ID        "rmnet_data0"
#define NETMGR_CLIENT_MDMMHI_DEV_ID     NETMGR_CLIENT_MSM_DEV_ID

#ifdef __cplusplus
class NetmgrIwlanClient : public ICneNetworkObserverNotifier,
    public ICneFeatureObserverNotifier
{
public:
  static NetmgrIwlanClient *getInstance();
  bool                     regSignals(void);
  bool                     stopSignals(void);
  virtual void       onNetConfigChange (const CnoNetConfigType &netConfig);
  int initQmiClient(const char* devName);
  int releaseQmiClient();
  bool getInitStatus(void)
  {
    return isInitialized;
  }
  void setInitStatus(bool status)
  {
    isInitialized = status;
  }

protected:
   NetmgrIwlanClient();

private:
   void getQcciConnId(qmi_connection_id_type qmiConnId);

   bool createObservers();

   static NetmgrIwlanClient *instancePtr;

   bool                           isInitialized;
   qmi_client_type                qmi_notifier;
   qmi_client_type                qmi_clnt_hndl;
   qmi_client_qmux_instance_type  qcci_conn_id;
   CnoNetType                     lastNetType;
   ICneNetworkObserver            *cno;
   ICneFeatureObserver            *cfo;
};
#else
  typedef struct NetmgrIwlanClient NetmgrIwlanClient;
#endif /* __cplusplus*/

using namespace std;

#define NETMGR_MAX_LOG_MSG_SIZE      512

#ifdef __cplusplus
  /* C++ treats anything between "" as string literals which is interpreted as const char*.
   * The current logging macros do not have support for handling const char* so we created
   * a new function to handle this. If required we can move it into ds_util module */

/*---------------------------------------------------------------------------
   Function for formating log messages
---------------------------------------------------------------------------*/
  LOCAL void
  netmgr_format_msg (char* buf_ptr, const char* fmt,...)
  {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf_ptr, NETMGR_MAX_LOG_MSG_SIZE, fmt, ap);
    va_end(ap);
  }

  #define NETMGR_LOG_ERR(...)                                         \
  {                                                                   \
    char buf[NETMGR_MAX_LOG_MSG_SIZE];                                \
    netmgr_format_msg(buf, __VA_ARGS__);                              \
    MSG_SPRINTF_1(MSG_SSID_LINUX_DATA, MSG_LEGACY_ERROR, "%s", buf);  \
  }

  #define NETMGR_LOG_LOW(...)                                         \
  {                                                                   \
    char buf[NETMGR_MAX_LOG_MSG_SIZE];                                \
    netmgr_format_msg(buf, __VA_ARGS__);                              \
    MSG_SPRINTF_1(MSG_SSID_LINUX_DATA, MSG_LEGACY_LOW, "%s", buf);    \
  }

  #define NETMGR_LOG_MED(...)                                         \
  {                                                                   \
    char buf[NETMGR_MAX_LOG_MSG_SIZE];                                \
    netmgr_format_msg(buf, __VA_ARGS__);                              \
    MSG_SPRINTF_1(MSG_SSID_LINUX_DATA, MSG_LEGACY_MED, "%s", buf);    \
  }

  #define NETMGR_LOG_HIGH(...)                                        \
  {                                                                   \
    char buf[NETMGR_MAX_LOG_MSG_SIZE];                                \
    netmgr_format_msg(buf, __VA_ARGS__);                              \
    MSG_SPRINTF_1(MSG_SSID_LINUX_DATA, MSG_LEGACY_HIGH, "%s", buf);   \
  }

#else
  #define NETMGR_LOG_ERR  netmgr_log_err
  #define NETMGR_LOG_LOW  netmgr_log_low
  #define NETMGR_LOG_MED  netmgr_log_med
  #define NETMGR_LOG_HIGH netmgr_log_high
#endif

static qmi_cci_os_signal_type iwlan_os_params;

/*===========================================================================
                            PUBLIC DEFINITIONS
===========================================================================*/
static pthread_t iwlanClientThread;
static pthread_mutex_t iwlanClientMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  iwlanClientStopCond = PTHREAD_COND_INITIALIZER;

NetmgrIwlanClient* NetmgrIwlanClient::instancePtr = NULL;

/*===========================================================================
  FUNCTION  getInstance
===========================================================================*/
/*!
@brief
  Returns an instance of NetmgrIwlanClient. This is a singleton class.

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
NetmgrIwlanClient* NetmgrIwlanClient::getInstance(void)
{
  if (NULL == instancePtr)
  {
    instancePtr = new NetmgrIwlanClient();
  }

  return instancePtr;
}


/*===========================================================================
  FUNCTION  getQcciConnId
===========================================================================*/
/*!
@brief
  Returns the correct QCCI conn id based on the qmi connection id

@param
  qmiConnId QMI connection id

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
void NetmgrIwlanClient::getQcciConnId(qmi_connection_id_type qmiConnId)
{
  /* TODO: Add new qmi client connection for 8994 */
  switch (qmiConnId)
  {
  case QMI_CONN_ID_RMNET_0:
    qcci_conn_id = QMI_CLIENT_QMUX_RMNET_INSTANCE_0;
    break;
  case QMI_CONN_ID_RMNET_MDM_0:
    qcci_conn_id = QMI_CLIENT_QMUX_RMNET_USB_INSTANCE_0;
    break;
  default:
    NETMGR_LOG_MED("Defaulting to rmnet0");
    qcci_conn_id = QMI_CLIENT_QMUX_RMNET_INSTANCE_0;
    break;
  }
}

/*===========================================================================
  FUNCTION  NetmgrIwlanClient
===========================================================================*/
/*!
@brief
  Constructor

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
NetmgrIwlanClient::NetmgrIwlanClient()
{
  /* Initialize member variables */
  isInitialized = false;
  cno = NULL;
  cfo = NULL;
  lastNetType = NET_UNKNOWN;
  qmi_clnt_hndl = NULL;
  qmi_notifier = NULL;
}

/*===========================================================================
  FUNCTION  createObservers
===========================================================================*/
/*!
@brief
  Create observer objects

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
bool NetmgrIwlanClient::createObservers()
{
  if (NULL == cno)
  {
    cno = CneFactory::CreateCneNetworkObserver(this);

    if (NULL == cno)
    {
      NETMGR_LOG_ERR("NetmgrIwlanClient::createObservers: Failed to create CNE Network observer!");
      return false;
    }
  }

  if (NULL == cfo)
  {
    cfo = CneFactory::CreateCneFeatureObserver(this);

    if (NULL == cfo)
    {
      NETMGR_LOG_ERR("NetmgrIwlanClient::createObservers: Failed to create CNE feature observer!");
      return false;
    }
  }

  return true;
}

/*===========================================================================
  FUNCTION  regSignals
===========================================================================*/
/*!
@brief
  Starts observers and registers for appropriate signals

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
bool NetmgrIwlanClient::regSignals()
{
  CnoNetConfigType netConfig;

  if (!createObservers())
  {
    NETMGR_LOG_ERR("NetmgrIwlanClient::regSignals: No signals registered!");
    return false;
  }

  if (CO_RET_SUCCESS != cno->startSignal(SIGNAL_NET_CONFIG_CHANGE))
  {
    NETMGR_LOG_ERR("NetmgrIwlanClient::regSignals:"
                         " Failed to start SIGNAL_NET_CONFIG_CHANGE!");
    return false;
  }

  if (CO_RET_SUCCESS != cfo->startSignal(SIGNAL_FEATURE_ALL)) {
    NETMGR_LOG_ERR("NetmgrIwlanClient::regSignals:"
                         " Failed to start SIGNAL_FEATURE_ALL!");
    return false;
  }

  /* Call onNetConfigChange to send the current WLAN status */
  if (CO_RET_SUCCESS != cno->getNetConfig(netConfig))
  {
    NETMGR_LOG_ERR("regSignals(): Failed to get netConfig!");
  }
  else
  {
    onNetConfigChange(netConfig);
  }

  return true;
}

/*===========================================================================
 FUNCTION stopSignals
===========================================================================*/
/*!
@brief
 Stops CNE observers

@note

 - Dependencies
 - None

 - Side Effects
 - None
*/
/*=========================================================================*/
bool NetmgrIwlanClient::stopSignals()
{
  if (CO_RET_SUCCESS != cno->stopSignal(SIGNAL_NET_CONFIG_CHANGE))
  {
    NETMGR_LOG_ERR("NetmgrIwlanClient::stopSignals: "
                   "Failed to stop SIGNAL_NET_CONFIG_CHANGE");
    return false;
  }

  if (CO_RET_SUCCESS != cfo->stopSignal(SIGNAL_FEATURE_ALL))
  {
    NETMGR_LOG_ERR("NetmgrIwlanClient::stopSignals: "
                   "Failed to stop SIGNAL_FEATURE_ALL");
    return false;
  }

 return true;
}

/*===========================================================================
  FUNCTION  onNetConfigChange
===========================================================================*/
/*!
@brief
  Callback registered with CNE framework to receive WLAN and WQE change
  notifications

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
void NetmgrIwlanClient::onNetConfigChange(const CnoNetConfigType &netConfig)
{
  int rc = QMI_NO_ERR;
  CfoFeatureStatusType wqeStatus;
  dsd_wqe_status_enum_v01 qmiWQEStatus = DSD_WQE_INACTIVE_V01;
  bool wqeAvail = false;
  char buff[INET6_ADDRSTRLEN+1];

  do
  {
    if (cfo && CO_RET_SUCCESS == cfo->getFeatureStatus(CFO_FEATURE_WQE, wqeStatus))
    {
      qmiWQEStatus = ((wqeStatus == CFO_STATUS_ACTIVE) ? DSD_WQE_ACTIVE_V01 : DSD_WQE_INACTIVE_V01);
      wqeAvail = true;
      NETMGR_LOG_MED("OnNetConfigChange: qmiWQEStatus=%d", qmiWQEStatus);
    }
    else
    {
      NETMGR_LOG_ERR("OnNetConfigChange: failed to obtain WQE status");
    }

    /* WLAN connected */
    if (NET_WLAN == netConfig.net.type)
    {
      CnoWlanSuppNetConfigType wnc;
      static const in6_addr null_ip6_addr = {{{0}}};


      NETMGR_LOG_MED("OnNetConfigChange: Propagating WLAN connect event");

      if (CO_RET_SUCCESS != cno->getWlanSuppNetConfig(wnc))
      {
        NETMGR_LOG_ERR("OnNetConfigChange: Failed to get WLAN supp netconfig");
      }

      /* Send WLAN connected DSD indication */
      dsd_wlan_available_req_msg_v01   req_msg;
      dsd_wlan_available_resp_msg_v01  resp_msg;

      memset(&req_msg, 0, sizeof(req_msg));
      memset(&resp_msg, 0, sizeof(resp_msg));

      memcpy(req_msg.wlan_ap_mac_address,
             wnc.bssid,
             QMI_DSD_MAC_ADDR_LEN_V01);

      if (0 != netConfig.ip4.s_addr)
      {
        if (NULL != inet_ntop(AF_INET, &netConfig.ip4, buff, INET_ADDRSTRLEN))
        {
          NETMGR_LOG_MED("OnNetConfigChange: Copying ipv4 addr [%s]", buff);
        }

        req_msg.wlan_ipv4_address_valid = TRUE;
        req_msg.wlan_ipv4_address = ntohl(netConfig.ip4.s_addr);
      }

      if (0 != wnc.ip4dns)
      {
        req_msg.dns_ipv4_address_1_valid = TRUE;
        req_msg.dns_ipv4_address_1 = ntohl(wnc.ip4dns[0].s_addr);

        if (NULL != inet_ntop(AF_INET, &wnc.ip4dns[0], buff, INET_ADDRSTRLEN))
        {
          NETMGR_LOG_MED("OnNetConfigChange: Copying ipv4 dns addr 1 [%s]", buff);
        }

        req_msg.dns_ipv4_address_2_valid = TRUE;
        req_msg.dns_ipv4_address_2 = ntohl(wnc.ip4dns[1].s_addr);

        if (NULL != inet_ntop(AF_INET, &wnc.ip4dns[1], buff, INET_ADDRSTRLEN))
        {
          NETMGR_LOG_MED("OnNetConfigChange: Copying ipv4 dns addr 2 [%s]", buff);
        }
      }

      if (NULL != wnc.ssid)
      {
        req_msg.ssid_len = (uint32_t)sizeof(wnc.ssid);
        req_msg.ssid_valid = TRUE;
        if (NULL != memmove(req_msg.ssid,
                            wnc.ssid,
                            QMI_DSD_MAX_SSID_LEN_V01))
        {
          req_msg.ssid_len = (uint32_t)sizeof(req_msg.ssid);
          req_msg.ssid_valid = TRUE;

          NETMGR_LOG_MED("OnNetConfigChange: Copying ssid %s of len %d",
                         req_msg.ssid, req_msg.ssid_len);
        }
        else
        {
          NETMGR_LOG_ERR("OnNetConfigChange: Failed to copy ssid");
        }
      }

      if (0 != memcmp(netConfig.ip6.s6_addr,
                      null_ip6_addr.s6_addr,
                      sizeof(netConfig.ip6.s6_addr)))
      {
        if (NULL != inet_ntop(AF_INET6, &netConfig.ip6, buff, INET6_ADDRSTRLEN))
        {
          NETMGR_LOG_MED("OnNetConfigChange: Copying ipv6 Addr [%s]", buff);
        }

        req_msg.wlan_ipv6_address_valid = TRUE;
        memcpy(req_msg.wlan_ipv6_address.ipv6_address,
               netConfig.ip6.s6_addr,
               QMI_DSD_IPV6_ADDR_LEN_V01);

        //FIXME: Get correct prefix length
        req_msg.wlan_ipv6_address.prefix_len = 64;

      }

      if (true == wqeAvail)
      {
        req_msg.wqe_status_valid = TRUE;
        req_msg.wqe_status = qmiWQEStatus;
      }

      /* Send message */
      rc = qmi_client_send_msg_sync(qmi_clnt_hndl,
                                    QMI_DSD_WLAN_AVAILABLE_REQ_V01,
                                    &req_msg,
                                    sizeof(req_msg),
                                    &resp_msg,
                                    sizeof(resp_msg),
                                    QMI_DSD_SYNC_MSG_TIMEOUT);

      if (QMI_NO_ERR != rc)
      {
        NETMGR_LOG_ERR("OnNetConfigChange: Failed to send qmi_dsd_wlan_available_req, err=%d",
                       rc);
      }
      else if (QMI_NO_ERR != resp_msg.resp.result)
      {
        NETMGR_LOG_ERR("OnNetConfigChange: Failed to send qmi_dsd_wlan_available_req, err=%d",
                       resp_msg.resp.result);
      }

    }
    /* If WLAN was last connected but not anymore, send a DISCONNECT */
    else if (NET_WLAN == lastNetType)
    {
      NETMGR_LOG_MED("OnNetConfigChange: Propagating WLAN disconnect event");

      // Send the WLAN disconnect DSD indication
      dsd_wlan_not_available_req_msg_v01   req_msg;
      dsd_wlan_not_available_resp_msg_v01  resp_msg;

      memset(&req_msg, 0, sizeof(req_msg));
      memset(&resp_msg, 0, sizeof(resp_msg));

      if (true == wqeAvail)
      {
        req_msg.wqe_status_valid = TRUE;
        req_msg.wqe_status = qmiWQEStatus;
      }

      /* Send message */
      rc = qmi_client_send_msg_sync(qmi_clnt_hndl,
                                    QMI_DSD_WLAN_NOT_AVAILABLE_REQ_V01,
                                    &req_msg,
                                    sizeof(req_msg),
                                    &resp_msg,
                                    sizeof(resp_msg),
                                    QMI_DSD_SYNC_MSG_TIMEOUT);

      if (QMI_NO_ERR != rc)
      {
        NETMGR_LOG_ERR("OnNetConfigChange: failed to send qmi_dsd_wlan_not_available_req, err=%d",
                      rc);
      }
      else if (QMI_NO_ERR != resp_msg.resp.result)
      {
        NETMGR_LOG_ERR("OnNetConfigChange: failed to send qmi_dsd_wlan_not_available_req, err=%d",
                      resp_msg.resp.error);
      }
    }

    lastNetType = netConfig.net.type;

  } while (0);
}

/*===========================================================================
  FUNCTION  initQmiClient
===========================================================================*/
/*!
@brief
  Initializes QMI DSD service client

@return
  NETMGR_SUCCESS on success
  NETMGR_FAILURE on error

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
int NetmgrIwlanClient::initQmiClient(const char* devName)
{
  int rc = NETMGR_FAILURE;
  int ret = QMI_NO_ERR;
  int i = 0;
  int muxId = -1;
  int epType = -1;
  int epId = -1;
  qmi_service_info qmi_svc_info;
  qmi_idl_service_object_type dsd_srvc_obj = dsd_get_service_object_v01();

  do {
    if (NULL == dsd_srvc_obj)
    {
      NETMGR_LOG_ERR("Unable to get QMI-DSD object");
      return NETMGR_FAILURE;
    }

    if (NULL == qmi_clnt_hndl)
    {
      memset(&qmi_svc_info, 0, sizeof(qmi_service_info));
      memset(&iwlan_os_params, 0, sizeof(qmi_cci_os_signal_type));

      /* Get the qcci conn id */
      getQcciConnId(QMI_PLATFORM_DEV_NAME_TO_CONN_ID(devName));

      /* Register with xport */
      qmi_cci_qmux_xport_register(qcci_conn_id);

      /* Initialize notifier client handle */
      ret = qmi_client_notifier_init(dsd_srvc_obj, &iwlan_os_params, &qmi_notifier);
      if (QMI_NO_ERR != ret)
      {
        NETMGR_LOG_ERR("failed to initialize client notifier");
        rc = NETMGR_FAILURE;
        break;
      }

      /* Wait till the service comes up */
      while (1)
      {
        /* Check if QMI-DSD service is up */
        ret = qmi_client_get_service_instance(dsd_srvc_obj, qcci_conn_id, &qmi_svc_info);
        NETMGR_LOG_LOW("qmi_client_get_service_instance ret [%d],",
                       ret);

        if (QMI_NO_ERR == ret)
        {
          break;
        }

        /* If there was an error in getting the instance, probably due to service not
         * being available wait for signal from service */
        QMI_CCI_OS_SIGNAL_WAIT(&iwlan_os_params, 500);
      }

      /* Initialize the client */
      ret = qmi_client_init(&qmi_svc_info,
                            dsd_srvc_obj,
                            NULL,
                            NULL,
                            NULL,
                            &qmi_clnt_hndl);

      if (QMI_NO_ERR != ret)
      {
        NETMGR_LOG_ERR("Failed to initialize QMI-DSD client");
        break;
      }

      /* Release the notifier client */
      ret = qmi_client_release(qmi_notifier);
      if (QMI_NO_ERR != ret)
      {
        NETMGR_LOG_ERR("failed to release notifier");
      }
      qmi_notifier = NULL;
    }

    NETMGR_LOG_MED("QMI-DSD client=0x%x successfully initialized", qmi_clnt_hndl);
    rc = NETMGR_SUCCESS;
  } while (0);

  return rc;
}

/*===========================================================================
  FUNCTION  releaseQmiClient
===========================================================================*/
/*!
@brief
  Releases QMI DSD service client

@return
  NETMGR_SUCCESS on success
  NETMGR_FAILURE on error

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
int NetmgrIwlanClient::releaseQmiClient()
{
  int rc = QMI_NO_ERR;

  CneFactory::DestroyCneNetworkObserver(cno);
  CneFactory::DestroyCneFeatureObserver(cfo);

  qmi_cci_qmux_xport_unregister(qcci_conn_id);

  rc = qmi_client_release(qmi_clnt_hndl);

  if (QMI_NO_ERR != rc)
  {
    NETMGR_LOG_ERR("Error releasing QMI-DSD client");
    return NETMGR_FAILURE;
  }

  NETMGR_LOG_MED("Successfully released QMI-DSD client");

  cno = NULL;
  cfo = NULL;
  qmi_clnt_hndl = NULL;
  qmi_notifier = NULL;
  lastNetType = NET_UNKNOWN;

  return NETMGR_SUCCESS;
}

/*===========================================================================
                              GLOBAL FUNCTIONS
===========================================================================*/

/*===========================================================================
  FUNCTION  iwlanClientLoop
===========================================================================*/
/*!
@brief
  Initializes the netmgr iwlan client state

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
void * iwlanClientLoop(void* args)
{
  /* Get an instance of NetmgrIwlanClient */
  NetmgrIwlanClient* iwlan_client = NetmgrIwlanClient::getInstance();
  int ret = -1;

  (void) args;

  if (NULL == iwlan_client) {
    NETMGR_LOG_ERR("Failed to allocate iwlan client!");
    return (void*) NULL;
  }

  pthread_mutex_lock(&iwlanClientMutex);

  /* Create observers and register for signals */
  iwlan_client->regSignals();

  while (ret) {
    ret = pthread_cond_wait(&iwlanClientStopCond, &iwlanClientMutex);
  }

  NETMGR_LOG_MED("iwlan client thread terminating...");
  pthread_mutex_unlock(&iwlanClientMutex);

  return NULL;
}

/*===========================================================================
                     GLOBAL FUNCTIONS AND DEFINITIONS
===========================================================================*/


extern "C" {
/*===========================================================================
  FUNCTION  netmgrIwlanClientInit
===========================================================================*/
/*!
@brief
  Initializes the netmgr iwlan client state

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
int netmgrIwlanClientInit(void)
{
  const char *dev_id;
  int rc = NETMGR_FAILURE;
  ds_target_t target = ds_get_target();

  /* Get an instance of NetmgrIwlanClient */
  NetmgrIwlanClient* iwlan_client = NetmgrIwlanClient::getInstance();

  if (NULL == iwlan_client) {
    NETMGR_LOG_ERR("Failed to get iwlan client instance!");
    return NETMGR_FAILURE;
  }
  else if (iwlan_client->getInitStatus() == true) {
    NETMGR_LOG_MED("iWLAN client already initialized!");
    return NETMGR_SUCCESS;
  }

  /* Determine the correct device id based on target */
  switch (target)
  {
  case DS_TARGET_FUSION4_5_PCIE:
    dev_id = NETMGR_CLIENT_MDMMHI_DEV_ID;
    break;
  case DS_TARGET_MDM:
    dev_id = NETMGR_CLIENT_MDMUSB_DEV_ID;
    break;
  case DS_TARGET_MSM8994:
    dev_id = NETMGR_CLIENT_MSM_DEV_ID;
    break;
  case DS_TARGET_MSM:
  default:
    dev_id = NETMGR_CLIENT_DEFAULT_DEV_ID;
    break;
  }

  /* Initialize QMI-DSD client */
  rc = iwlan_client->initQmiClient(dev_id);
  if (NETMGR_SUCCESS != rc)
  {
    NETMGR_LOG_ERR("Failed to initialize QMI-DSD client for netmgr iWLAN client");
    return NETMGR_FAILURE;
  }

  /* Start a thread */
  if (pthread_create(&iwlanClientThread, NULL, iwlanClientLoop,
                     (void*)NULL) < 0) {
    NETMGR_LOG_ERR("Could not create iwlan client thread!");
    return NETMGR_FAILURE;
  }

  iwlan_client->setInitStatus(true);

  NETMGR_LOG_MED("Sucessfully created iWLAN thread");
  return NETMGR_SUCCESS;
}

/*===========================================================================
  FUNCTION  netmgrIwlanClientRelease
===========================================================================*/
/*!
@brief
  Cleans up the netmgr iwlan client state

@note

  - Dependencies
    - None

  - Side Effects
    - None
*/
/*=========================================================================*/
int netmgrIwlanClientRelease(void)
{
  int rc = NETMGR_SUCCESS;

   /* Get an instance of NetmgrIwlanClient */
  NetmgrIwlanClient* iwlan_client = NetmgrIwlanClient::getInstance();

  if (NULL == iwlan_client) {
    NETMGR_LOG_ERR("Failed to get iwlan client instance!");
    return NETMGR_FAILURE;
  }
  else if (iwlan_client->getInitStatus() == false) {
    NETMGR_LOG_MED("iWLAN client already un-initialized!");
    return NETMGR_SUCCESS;
  }

  rc = iwlan_client->releaseQmiClient();

  /* Stop the thread */
  pthread_mutex_lock(&iwlanClientMutex);
  pthread_cond_signal(&iwlanClientStopCond);
  pthread_mutex_unlock(&iwlanClientMutex);

  pthread_join(iwlanClientThread, NULL);
  NETMGR_LOG_MED("Sucessfully cleaned-up iWLAN thread");

  iwlan_client->setInitStatus(false);

  return rc;
}

} /* extern "C" */
#endif /* FEATURE_DATA_IWLAN */

