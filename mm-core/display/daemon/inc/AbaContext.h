/*
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef _ABACONTEXT_H
#define _ABACONTEXT_H

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cutils/properties.h>
#include <cutils/log.h>

#include "common_log.h"
#include <cutils/sockets.h>
#include <linux/msm_mdp.h>
#include <linux/fb.h>

#include "aba_core_api.h"
#include "aba_cabl.h"
#include "mdp_version.h"
#include "ConfigParameters.h"
#include "ScreenRefresher.h"
#include "als.h"
#include "DummySensor.h"
#include "Calib.h"
#include "NativeLightSensor.h"
#include "PPDaemonUtils.h"

#ifdef ALS_ENABLE
#include "LightSensor.h"
#endif

#define BL_LUT_SIZE  256
#define HIST_PROC_FACTOR 8

#define SVI_ALS_RATIO_THR 0.05
#define SVI_ALS_LIN_THR 35.0

#define SVI_SENSOR_PROP "ro.qcom.svi.sensortype"

enum require_signal {
    WAIT_SIGNAL,
    NO_WAIT_SIGNAL
};

enum alsupdate_status {
    ALS_UPDATE_OFF = false,
    ALS_UPDATE_ON = true
};

class AbaContext {
    // Aba parameters
    AbaHardwareInfoType mAbaHwInfo;
    bool mDebug;
    bool mWorkerRunning;
    CABLHWParams mCABLCP;
    SVIHWParams mSVICP;
    int aba_status;
    pthread_cond_t  mAbaCond;
    pthread_mutex_t mAbaLock;
    pthread_t mWorkerThread;
    require_signal mSignalToWorker;
    ScreenRefresher *pRefresher;
    static void *aba_worker_func(void *obj) {
        reinterpret_cast<AbaContext *>(obj)->ProcessAbaWorker();
        return NULL;
    }
    void FilteredSignal();
    void ProcessAbaWorker();
    void *pHandle;

    // Backlight Params & Functions
    // minimum bl level converted from bl_level_threshold range:0-255
    int is_backlight_modified(uint32_t *);
    uint32_t  mUserBLLevel;
    uint32_t mBl_lvl;
    uint32_t orig_level;

    // Histogram , LUT & Color-map
    bool mHistStatus;
    fb_cmap mColorMap;
    mdp_histogram_data hist;
    uint32_t *mInputHistogram;
    uint32_t *mOutputHistogram;
    uint32_t *minLUT;

    // CABL specific parameters
    AbaConfigInfoType eCABLConfig;
    bool mCablDebug;
    bool mCablEnable;
    CablInitialConfigType mCablOEMParams;
    CABLQualityLevelType mQualityLevel;
    int32_t auto_adjust_quality_lvl();
    pthread_mutex_t mCABLCtrlLock;

    // SVI specific parameters
    AbaConfigInfoType eSVIConfig;
    bool mSVIDebug;
    bool mSVIEnable;
    bool mSVISupported;
    SVIConfigParametersType mSVIOEMParams;
    void ProcessSVIWork();

    // ALS specific parameters
    alsupdate_status mALSUpdateThreadStatus;
    ALS *mLightSensor;
    bool bALSRunEnabled;
    bool mALSUThreadRunning;
    int mCurrALSValue;
    int mLastALSValue;
    pthread_cond_t  mALSCond;
    pthread_mutex_t mALSUpdateLock;
    pthread_mutex_t mSVICtrlLock;
    pthread_t mALSUpdateThread;
    static void *als_update_func(void *obj) {
        reinterpret_cast<AbaContext *>(obj)->ProcessAbaALSUpdater();
        return NULL;
    }
    void ProcessAbaALSUpdater();
    void SetLightSensor();
    void StartALSUpdaterThread();

public:
    //Aba Parameters
    int Notify(int notification_type);
    void initHW();
    inline void SetDebugLevel(bool t){
        mDebug = t;
    }
    inline void* GetHandle(){
        return pHandle;
    }
    inline void SetABAStatusON(int x){
        aba_status = aba_status | x;
    }
    inline void SetABAStatusOFF(int x){
        aba_status = aba_status & ~x;
    }
    inline bool IsABAStatusON(int x){
        return ( aba_status & x);
    }

    //Cabl Parameters
    int32_t CABLControl(bool);
    inline void SetDefaultQualityMode(uint32_t mode){
        mCablOEMParams.default_ql_mode = mode;
    }
    inline void SetQualityLevel(CABLQualityLevelType q){
        mQualityLevel = q;
    }
    inline CABLQualityLevelType GetQualityLevel(){
        return mQualityLevel;
    }
    inline void SetCABLFeature(){
        eCABLConfig.eFeature = ABA_FEATURE_CABL;
        eCABLConfig.ePanelType = LCD_PANEL;
    }

    //SVI Parameters
    int SVIControl(bool);
    inline void SetSVIFeature(){
        eSVIConfig.eFeature = ABA_FEATURE_SVI;
    }

    //ALS Parameters
    void StopALSUpdaterThread();
    void CleanupLightSensor();

    // ABA constructor
    AbaContext() :  mWorkerRunning(false), aba_status(0), mSignalToWorker(NO_WAIT_SIGNAL),
                pRefresher(NULL), mHistStatus(0), mCablDebug(false),
                mCablEnable(false), mSVIDebug(false), mSVIEnable(false),
                mLightSensor(NULL), bALSRunEnabled(false), mALSUThreadRunning(false),
                mCurrALSValue(0), mLastALSValue(0) {

        char property[PROPERTY_VALUE_MAX];
        int mdp_version = qdutils::MDPVersion::getInstance().getMDPVersion();
        pthread_mutex_init(&mAbaLock, NULL);
        pthread_mutex_init(&mALSUpdateLock, NULL);
        pthread_mutex_init(&mCABLCtrlLock, NULL);
        pthread_mutex_init(&mSVICtrlLock, NULL);
        pthread_cond_init(&mAbaCond, NULL);
        pthread_cond_init(&mALSCond, NULL);

        // SVI Initializations
        mSVISupported = true;
        if (1 == display_pp_svi_supported()) {
            mSVIEnable = true;
            SetLightSensor();
        }
        if (property_get("debug.svi.logs", property, 0) > 0 && (atoi(property) == 1)) {
            LOGD("SVI Debug Enabled");
            mSVIDebug = true;
        }

        //  CABL Initializations
        if (2 == display_pp_cabl_supported()) {
            mCablEnable = true;
        }
        if (property_get("debug.cabl.logs", property, 0) > 0 && (atoi(property) > 0 )) {
            mCablDebug = true;
        }

        mDebug = mSVIDebug || mCablDebug;

        AbaStatusType retval = AbaCreateSession(&pHandle);
        if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM || retval == ABA_STATUS_NOT_SUPPORTED) {
            LOGE("Create session failed retval = %d", retval);
        } else {
            LOGD_IF(mDebug, "ABA Create Session Successful");
        }
    }

    ~AbaContext() {
        pthread_mutex_destroy(&mAbaLock);
        pthread_mutex_destroy(&mALSUpdateLock);
        pthread_mutex_destroy(&mCABLCtrlLock);
        pthread_mutex_destroy(&mSVICtrlLock);
        pthread_cond_destroy(&mAbaCond);
        pthread_cond_destroy(&mALSCond);
    }
};

#endif /* _ABACONTEXT_H */
