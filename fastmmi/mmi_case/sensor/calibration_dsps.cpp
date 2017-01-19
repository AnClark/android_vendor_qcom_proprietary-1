/*
* Copyright (c) 2014 Qualcomm Technologies, Inc. All Rights Reserved.
* Qualcomm Technologies Confidential and Proprietary.
 */

#include <pthread.h>
#include <cutils/log.h>

#include "calibration.h"
#include "hardware/sensors.h"
#include "sns_reg_api_v02.h"
#include "sns_smgr_api_v01.h"

extern "C" {
#include "sensor1.h"
}

static sensor1_handle_s *test_sensor1_handle;
static int test_result = 0;     /* Pass result from rcv() to get() */
static int test_predicate = 0;  /* Waiting for RESP message */
static pthread_mutex_t test_mutex;  /* Only one request at a time */
static pthread_cond_t test_cond;    /* Waiting for RESP message */
static uint8_t transaction_id;

static void rcv_complete(pthread_cond_t * cond, int *predicate, int *return_result, int result) {
    pthread_mutex_lock(&test_mutex);
    *return_result = result;
    *predicate = 1;
    pthread_cond_signal(cond);
    pthread_mutex_unlock(&test_mutex);
}


void rcv_test_msg(intptr_t data, sensor1_msg_header_s * msg_hdr_ptr, sensor1_msg_type_e msg_type, void *msg_ptr) {
    if(NULL == msg_hdr_ptr) {
        ALOGE("%s: received NULL msg_hdr_ptr! type: %i", __FUNCTION__, msg_type);
        return;
    }
    ALOGI("%s: Received msg w/ txn_id=%i", __FUNCTION__, msg_hdr_ptr->txn_id);

    if(SENSOR1_MSG_TYPE_RESP == msg_type)   // SNS_SMGR_SINGLE_SENSOR_TEST_RESP_V01
    {
        sns_smgr_single_sensor_test_resp_msg_v01 *msgPtr = (sns_smgr_single_sensor_test_resp_msg_v01 *) msg_ptr;

        if(msg_hdr_ptr->txn_id != transaction_id) {
            ALOGE("%s: Wrong transaction ID: %i, %i", __FUNCTION__, msg_hdr_ptr->txn_id, transaction_id);
        } else if(SNS_RESULT_FAILURE_V01 == msgPtr->Resp.sns_result_t) {
            /* If there is an error is the common response,
             * do not expect an indication */
            ALOGI("%s: Received error in common response: %i", __FUNCTION__, msgPtr->Resp.sns_err_t);
            rcv_complete(&test_cond, &test_predicate, &test_result, -1);
        } else if(SNS_SMGR_TEST_STATUS_SUCCESS_V01 == msgPtr->TestStatus) {
            rcv_complete(&test_cond, &test_predicate, &test_result, 0);
        } else if(SNS_SMGR_TEST_STATUS_PENDING_V01 != msgPtr->TestStatus) {
            // Instead of returning this error code, let us wait to see
            // if the IND msg returns a more detailed one...
            test_result = -msgPtr->TestStatus - 10;
        }
    } else if(SENSOR1_MSG_TYPE_IND == msg_type) // SNS_SMGR_SINGLE_SENSOR_TEST_IND_V01
    {
        sns_smgr_single_sensor_test_ind_msg_v01 *msgPtr = (sns_smgr_single_sensor_test_ind_msg_v01 *) msg_ptr;

        if(SNS_SMGR_TEST_RESULT_PASS_V01 == msgPtr->TestResult) {
            rcv_complete(&test_cond, &test_predicate, &test_result, 0);
        } else if(true == msgPtr->ErrorCode_valid) {
            ALOGI("%s: Received error in IND message: %i", __FUNCTION__, msgPtr->ErrorCode);
            rcv_complete(&test_cond, &test_predicate, &test_result, msgPtr->ErrorCode);
        } else {
            ALOGI("%s: Message indicates error w/o code (%i)", __FUNCTION__, test_result);
            rcv_complete(&test_cond, &test_predicate, &test_result, test_result);
        }
    } else if(SENSOR1_MSG_TYPE_BROKEN_PIPE == msg_type) {
        ALOGI("%s: Broken message pipe", __FUNCTION__);
        rcv_complete(&test_cond, &test_predicate, &test_result, -21);
    } else if(SENSOR1_MSG_TYPE_RESP_INT_ERR == msg_type) {
        ALOGI("%s: Response - Internal Error", __FUNCTION__);
        rcv_complete(&test_cond, &test_predicate, &test_result, -22);
    } else if(SENSOR1_MSG_TYPE_RETRY_OPEN == msg_type) {
        ALOGI("%s: Received retry open", __FUNCTION__);
        rcv_complete(&test_cond, &test_predicate, &test_result, -21);
    } else {
        ALOGI("%s: Received unknown message type: %d", __FUNCTION__, msg_type);
    }

    if(NULL != msg_ptr) {
        sensor1_free_msg_buf(*((sensor1_handle_s **) data), msg_ptr);
    }
}

static int close_sensor1(sensor1_handle_s * sensor1_handle) {
    sensor1_error_e error;

    transaction_id++;

    error = sensor1_close(sensor1_handle);
    if(SENSOR1_SUCCESS != error) {
        ALOGE("%s: sensor1_close returned %d", __FUNCTION__, error);
        return -1;
    }
    return 0;
}

int psensor_calibration(void) {
    sensor1_msg_header_s msgHdr;
    sns_smgr_single_sensor_test_req_msg_v01 *msgPtr;
    sensor1_error_e error;
    struct timespec ts;
    int resultStatus;
    int testResult;


    pthread_mutex_init(&test_mutex, NULL);
    pthread_cond_init(&test_cond, NULL);
    transaction_id = 0;

    test_result = -1;
    error = sensor1_open(&test_sensor1_handle, rcv_test_msg, (intptr_t) (&test_sensor1_handle));

    if(SENSOR1_SUCCESS != error) {
        ALOGE("%s: sensor1_open returned %d", __FUNCTION__, error);
        return -1;
    }

    error = sensor1_alloc_msg_buf(test_sensor1_handle,
                                  sizeof(sns_smgr_single_sensor_test_req_msg_v01), (void **) &msgPtr);

    if(SENSOR1_SUCCESS != error) {
        ALOGE("%s: sensor1_alloc_msg_buf returned %d", __FUNCTION__, error);
        close_sensor1(test_sensor1_handle);
        return -1;
    }

    msgHdr.service_number = SNS_SMGR_SVC_ID_V01;
    msgHdr.msg_id = SNS_SMGR_SINGLE_SENSOR_TEST_REQ_V01;
    msgHdr.msg_size = sizeof(sns_smgr_single_sensor_test_req_msg_v01);
    msgHdr.txn_id = transaction_id;

    /* Create the message */
    msgPtr->SensorID = 40;
    msgPtr->DataType = 0;
    msgPtr->TestType = SNS_SMGR_TEST_SELF_V01;
    msgPtr->SaveToRegistry_valid = true;
    msgPtr->SaveToRegistry = 1;
    msgPtr->ApplyCalNow_valid = true;
    msgPtr->ApplyCalNow = 1;

    error = sensor1_write(test_sensor1_handle, &msgHdr, msgPtr);

    if(SENSOR1_SUCCESS != error) {
        ALOGE("%s: sensor1_write returned %d", __FUNCTION__, error);
        sensor1_free_msg_buf(test_sensor1_handle, msgPtr);
        close_sensor1(test_sensor1_handle);
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 10;

    pthread_mutex_lock(&test_mutex);
    /* wait for response */
    while(1 != test_predicate) {
        resultStatus = pthread_cond_timedwait(&test_cond, &test_mutex, &ts);
        if(resultStatus == ETIMEDOUT) {
            if(0 != test_result && -1 != test_result) {
                // Being here means the RESP message included an error code,
                // but we never received an IND to confirm it.
                ALOGE("%s: Time-out: %i", __FUNCTION__, test_result);
                break;
            } else {
                ALOGE("%s: Sensor Test request timed-out", __FUNCTION__);
                break;
            }
        }
    }
    test_predicate = 0;
    testResult = test_result;
    close_sensor1(test_sensor1_handle);
    pthread_mutex_unlock(&test_mutex);

    return testResult;
}
