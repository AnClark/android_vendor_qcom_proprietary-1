/*
 * DESCRIPTION
 * This file runs the ABA algorithm, which contains both CABL and SVI.
 * It interacts with the pp-daemon in order to run on its own.
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 */

#include "AbaContext.h"

static void free_hist(struct mdp_histogram_data *hist)
{

    if(hist == NULL){
        LOGE("%s: Histogram data NULL", __FUNCTION__);
        return;
    }

    if (hist->c0)
        free(hist->c0);
    if (hist->c1)
        free(hist->c1);
    if (hist->c2)
        free(hist->c2);
    if (hist->extra_info)
        free(hist->extra_info);
}

static int read_hist(int32_t fb, struct mdp_histogram_data *hist)
{
    int32_t ret;

    if(hist == NULL){
        LOGE("%s: Histogram data is null!", __FUNCTION__);
        return -1;
    }

    ret = ioctl(fb, MSMFB_HISTOGRAM, hist);
    if ((ret != 0) && (errno != ENODATA) && (errno != ETIMEDOUT)
        && (errno != EPERM))
        /* ENODATA or ETIMEDOUT indicates a valid histogram failure */
        LOGD("%s: MSMFB_HISTOGRAM failed: %s", __FUNCTION__, strerror(errno));

    if (ret == 0)
        return 0;
    else
        return -errno;
}

static int32_t copyHistogram(struct mdp_histogram_data *hist, uint32_t *outhist)
{

    uint32_t offset, size;
    int32_t ret = 0;

    offset = hist->bin_cnt;
    size = hist->bin_cnt * 4;
    switch (gHwInfo.nPriDisplayHistComp) {
    case 3:
        memcpy(outhist + offset * 2, hist->c2, size);
    case 2:
        memcpy(outhist + offset, hist->c1, size);
    case 1:
        memcpy(outhist, hist->c0, size);
        break;
    default:
        ret = -1;
    }
    return ret;
}

static void conv3channel(struct fb_cmap *cmap, uint32_t *outmap)
{
    int32_t i;
    __u16 *r, *g, *b;

    if ((cmap == NULL) || (cmap == NULL)) {
        LOGE("%s: Invalid parameters passed", __FUNCTION__);
        return;
    }

    /* map LUT */
    r = cmap->red;
    g = cmap->green;
    b = cmap->blue;
    for (i = 0; i < BL_LUT_SIZE; i++) {
        r[i] = (outmap[i] & 0xFF);
        g[i] = (outmap[i] & 0xFF);
        b[i] = (outmap[i] & 0xFF);
    }
    return;

}

static int32_t outCmap(int32_t fb, uint32_t *outcmap, fb_cmap mColorMap)
{
    struct msmfb_mdp_pp lut;
    struct fb_cmap cmap;
    uint32_t *cmap_data;
    int32_t i, ret = -1;
    cmap = mColorMap;
    cmap.start = 0;
    cmap.len = BL_LUT_SIZE;
    char strbuf[MAX_DBG_MSG_LEN]="";
    size_t strbuf_len = 0;

    conv3channel(&cmap,outcmap);

    switch (gHwInfo.nPriDisplayHistBins) {
    default:
        ret = ioctl(fb, MSMFB_SET_LUT, &cmap);
        break;
    case 256:
        cmap_data = (uint32_t *)malloc(cmap.len * sizeof(uint32_t));
        if (!cmap_data)
            goto err_mem;

        snprintf(strbuf, cmap.len * sizeof(char), "%s", "LUT\n");
        for (i = 0; (uint32_t)i < cmap.len; i++) {
            strbuf_len = strlen(strbuf);
            cmap_data[i] = cmap.red[i] & 0xFF;
            snprintf(strbuf+strbuf_len,(cmap.len - strbuf_len) * sizeof(char),
                              "%d ", cmap_data[i]);
        }

        lut.op = mdp_op_lut_cfg;
        lut.data.lut_cfg_data.lut_type = mdp_lut_hist;
        lut.data.lut_cfg_data.data.hist_lut_data.block = gHwInfo.nPriDisplayHistBlock;
        lut.data.lut_cfg_data.data.hist_lut_data.ops = MDP_PP_OPS_WRITE |
                                                        MDP_PP_OPS_ENABLE;
        lut.data.lut_cfg_data.data.hist_lut_data.len = cmap.len;
        lut.data.lut_cfg_data.data.hist_lut_data.data = cmap_data;

        ret = ioctl(fb, MSMFB_MDP_PP, &lut);
err_mem:
        free(cmap_data);
        break;
    }
    return ret;
}

int calculateDiffALS(int currALS, int prevALS){
    int ret = 0;
    float calcVal;
    if (!prevALS)
        return (currALS > 0) ? 1 : 0;
    calcVal = fabsf((prevALS - currALS) / prevALS);
    if(calcVal > SVI_ALS_RATIO_THR)
        return 1;
    if (fabsf(prevALS - currALS) > SVI_ALS_LIN_THR)
        ret = 1;
    return ret;
}

static void initialize_cmap(struct fb_cmap *cmap)
{

    if(cmap == NULL){
        LOGE("%s: Colormap struct NULL", __FUNCTION__);
        return;
    }

    cmap->red = 0;
    cmap->green = 0;
    cmap->blue = 0;
    cmap->transp = 0;

    cmap->start = 0;
    cmap->len = BL_LUT_SIZE;
    cmap->red = (__u16 *)malloc(cmap->len * sizeof(__u16));
    if (!cmap->red) {
        LOGE("%s: can't malloc cmap red!", __FUNCTION__);
        goto fail_rest1;
    }

    cmap->green = (__u16 *)malloc(cmap->len * sizeof(__u16));
    if (!cmap->green) {
        LOGE("%s: can't malloc cmap green!", __FUNCTION__);
        goto fail_rest2;
    }

    cmap->blue = (__u16 *)malloc(cmap->len * sizeof(__u16));
    if (!cmap->blue) {
        LOGE("%s: can't malloc cmap blue!", __FUNCTION__);
        goto fail_rest3;
    }

    return;

fail_rest3:
    free(cmap->blue);
fail_rest2:
    free(cmap->green);
fail_rest1:
    free(cmap->red);
}

void AbaContext::FilteredSignal(){
    pthread_mutex_lock(&mAbaLock);
    if(mSignalToWorker == WAIT_SIGNAL) {
        if (pRefresher){
            LOGD_IF(mDebug, "%s: Calling ScreenRefresher->Refresh", __func__);
            pRefresher->Refresh(1,16);
        }
        pthread_cond_signal(&mAbaCond);
    }
    mSignalToWorker = NO_WAIT_SIGNAL;
    pthread_mutex_unlock(&mAbaLock);
}

void AbaContext::initHW() {
    uint32_t i,j;
    mAbaHwInfo.uHistogramBins = gHwInfo.nPriDisplayHistBins;
    mAbaHwInfo.uHistogramComponents = gHwInfo.nPriDisplayHistComp;
    mAbaHwInfo.uBlock = gHwInfo.nPriDisplayHistBlock;
    mAbaHwInfo.uLUTSize = BL_LUT_SIZE;
    mAbaHwInfo.uFactor = HIST_PROC_FACTOR;

    i = mAbaHwInfo.uHistogramBins;
    j = 0;
    while (i > 0) {
        j += (i&0x1)?1:0;
        i = i >> 1;
    }

    if ((mAbaHwInfo.uHistogramBins <= 256) && (1 == j)) {
        i = mAbaHwInfo.uHistogramBins;
        do {
            i = i >> 1;
            mAbaHwInfo.uFactor--;
        } while (i > 1);
        mAbaHwInfo.uHalfBin = (mAbaHwInfo.uFactor)?(1<<(mAbaHwInfo.uFactor-1)):0;
    }

    mInputHistogram = (uint32_t *)malloc(mAbaHwInfo.uHistogramBins *
        mAbaHwInfo.uHistogramComponents * sizeof(uint32_t));
    if (!mInputHistogram) {
        LOGE("%s: can't malloc mInputHistogram!", __FUNCTION__);
        goto fail_hist;
    }

    mOutputHistogram = (uint32_t *)malloc(mAbaHwInfo.uHistogramBins *
        mAbaHwInfo.uHistogramComponents * sizeof(uint32_t));
    if (!mOutputHistogram) {
        LOGE("%s: can't malloc mOutputHistogram!", __FUNCTION__);
        goto fail_hist;
    }

    minLUT = (uint32_t *) malloc((mAbaHwInfo.uLUTSize) * sizeof(uint32_t));
    if (!minLUT) {
        LOGE("%s: can't malloc minLUT!", __FUNCTION__);
        goto fail_hist;
    }

    hist.c0 = 0;
    hist.c1 = 0;
    hist.c2 = 0;
    hist.block = mAbaHwInfo.uBlock;
    hist.bin_cnt = mAbaHwInfo.uHistogramBins;
    hist.c0 = (uint32_t *)malloc(hist.bin_cnt * sizeof(uint32_t));
    if (!hist.c0) {
        LOGE("%s: can't malloc red cmap!", __FUNCTION__);
        goto fail_hist;
    }

    hist.c1 = (uint32_t *)malloc(hist.bin_cnt * sizeof(uint32_t));
    if (!hist.c1) {
        LOGE("%s: can't malloc green cmap!", __FUNCTION__);
        goto fail_hist;
    }

    hist.c2 = (uint32_t *)malloc(hist.bin_cnt * sizeof(uint32_t));
    if (!hist.c2) {
        LOGE("%s: can't malloc blue cmap!", __FUNCTION__);
        goto fail_hist;
    }

    hist.extra_info = (uint32_t *)malloc(2 * sizeof(uint32_t));
    if (!hist.extra_info) {
        LOGE("%s: can't malloc extra info!", __FUNCTION__);
        goto fail_hist;
    }
    return;

fail_hist:
    free_hist(&hist);

    if (mInputHistogram)
        free(mInputHistogram);
    if (mOutputHistogram)
        free(mOutputHistogram);
    if (minLUT)
        free(minLUT);

    return;
}

int AbaContext::is_backlight_modified(uint32_t *old_lvl)
{
    int ret;
    uint32_t temp_lvl = get_backlight_level();

    if (temp_lvl != *old_lvl) {
        ret = 1;
        LOGD_IF(mCablDebug, "%s: The BL level changed,", __FUNCTION__);
        *old_lvl = temp_lvl;
        LOGD_IF(mCablDebug, "%s: The BL level changed to %u", __FUNCTION__,
            temp_lvl);

        /* Reset the orig level only if > than the min level */
        if (temp_lvl >= mCablOEMParams.bl_min_level) {
            orig_level = temp_lvl;
            ret = AbaSetOriginalBacklightLevel(pHandle, orig_level);
            //set the new restore level
            mUserBLLevel = temp_lvl;
        }
    } else {
        ret = 0;
    }
    return ret;
}

int32_t AbaContext::auto_adjust_quality_lvl(){
    int32_t result = 0;
    char lvl[MAX_CMD_LEN];
    char property[PROPERTY_VALUE_MAX];
    if (property_get(YUV_INPUT_STATE_PROP, property, NULL) > 0) {
        if ((atoi(property) == 1) && (mQualityLevel != mCablOEMParams.video_quality_lvl)) {
            mQualityLevel = mCablOEMParams.video_quality_lvl;
            LOGD_IF(mCablDebug, "%s: Power saving level: %d", __FUNCTION__, mQualityLevel);
            pthread_mutex_lock(&mAbaLock);
            result = AbaSetQualityLevel(pHandle, (AbaQualityLevelType) mQualityLevel);
            if( result != ABA_STATUS_SUCCESS)
                LOGE("AbaSetQualityLevel failed with status = %d", result);
            pthread_mutex_unlock(&mAbaLock);
        }else if ((atoi(property) == 0) && (mQualityLevel != mCablOEMParams.ui_quality_lvl)) {
            mQualityLevel = mCablOEMParams.ui_quality_lvl;
            LOGD_IF(mCablDebug, "%s: Power saving level: %d", __FUNCTION__, mQualityLevel);
            pthread_mutex_lock(&mAbaLock);
            result = AbaSetQualityLevel(pHandle, (AbaQualityLevelType) mQualityLevel);
            if( result != ABA_STATUS_SUCCESS)
                LOGE("AbaSetQualityLevel failed with status = %d", result);
            pthread_mutex_unlock(&mAbaLock);
        }
    }
    return result;
}

/* Single ABA Worker for CABL and SVI */
void AbaContext::ProcessAbaWorker() {

    AbaStatusType retval;
    AbaStateType CablState;
    AbaStateType SVIState;

    uint32_t old_level;
    uint32_t bl_scale_ratio = 0;
    uint32_t set_ratio;
    uint32_t *curr_hist = NULL;
    int32_t ret;
    uint32_t tmp_ALS = 1, out_ALS = 0;
    void *term;
    bool32 IsConverged = 0;

    set_ratio = mCablOEMParams.bl_min_level;
    LOGD_IF(mDebug, "%s(): Entering ", __func__);

    pthread_mutex_lock(&mAbaLock);
    if(!mWorkerRunning)
        mWorkerRunning = true;
    else{
        pthread_mutex_unlock(&mAbaLock);
        goto exit_cleanup;
    }
    pthread_mutex_unlock(&mAbaLock);

    if (IsABAStatusON(ABA_FEATURE_SVI)) {
        if((!mALSUThreadRunning) && (mSVIEnable))
            StartALSUpdaterThread();
    }

    pRefresher = new ScreenRefresher;
    if (pRefresher){
        LOGD_IF(mDebug, "%s: Calling ScreenRefresher->Control(true)", __func__);
        pRefresher->Control(1);
    }

    old_level = mUserBLLevel;
    set_ratio = BL_SCALE_MAX;

    LOGD_IF(mDebug, "%s: Starting worker thread", __FUNCTION__);

    pthread_mutex_lock(&mAbaLock);
    while (IsABAStatusON(ABA_FEATURE_CABL | ABA_FEATURE_SVI)){
        LOGD_IF(mDebug, "%s: In outer", __FUNCTION__);

        if (mHistStatus == 0) {
            mSignalToWorker = WAIT_SIGNAL;
            LOGD_IF(mDebug, "%s: Waiting for signal", __FUNCTION__);
            pthread_cond_wait(&mAbaCond, &mAbaLock);
            LOGD_IF(mDebug, "%s: AbaWorker is signalled", __FUNCTION__);
        }
        pthread_mutex_unlock(&mAbaLock);

        AbaGetState((pHandle), &CablState,ABA_FEATURE_CABL);
        if((CablState== ABA_STATE_ACTIVE) &&
            (IsABAStatusON(ABA_FEATURE_CABL))){
            if(AUTO_QL_MODE == mCablOEMParams.default_ql_mode) {
                    ret = auto_adjust_quality_lvl();
                    if (ret)
                        LOGE("%s: adjust_quality_level failed", __FUNCTION__);
            }
        }

        pthread_mutex_lock(&mAbaLock);
        while (IsABAStatusON(ABA_FEATURE_CABL | ABA_FEATURE_SVI)){

            if (mHistStatus == 0) {
                pthread_mutex_unlock(&mAbaLock);
                break;
            }
            pthread_mutex_unlock(&mAbaLock);

            AbaGetState((pHandle), &CablState,ABA_FEATURE_CABL);
            AbaGetState((pHandle), &SVIState,ABA_FEATURE_SVI);
            if ((CablState == SVIState) && (SVIState == ABA_STATE_ACTIVE))
                LOGD_IF(mDebug, "Both SVI and CABL active");
            else if (CablState == ABA_STATE_ACTIVE)
                LOGD_IF(mDebug, "CABL is only active");
            else if (SVIState == ABA_STATE_ACTIVE)
                LOGD_IF(mDebug, "SVI is only active");

            if (!IsABAStatusON(ABA_FEATURE_CABL | ABA_FEATURE_SVI)){
                pthread_mutex_lock(&mAbaLock);
                break;
            }

            if (IsABAStatusON(ABA_FEATURE_SVI)) {
                if((!mALSUThreadRunning) && (mSVIEnable))
                    StartALSUpdaterThread();
            }
            pthread_mutex_lock(&mALSUpdateLock);
            tmp_ALS = mCurrALSValue;
            mLastALSValue = mCurrALSValue;
            pthread_mutex_unlock(&mALSUpdateLock);

            retval = AbaSetAmbientLightLevel((pHandle), tmp_ALS, &out_ALS);
            if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM
                || retval == ABA_STATUS_NOT_SUPPORTED) {
                LOGE("%s: AbaSetAmbientLightLevel() failed with ret = %d",
                    __FUNCTION__, retval);
                pthread_mutex_lock(&mAbaLock);
                continue;
            }
            LOGD_IF(mDebug, "%s: Input ALS = %u, Output ALS = %u", __func__,
                tmp_ALS, out_ALS);

            pthread_mutex_lock(&mAbaLock);
            if (mHistStatus == 1){
                pthread_mutex_unlock(&mAbaLock);
                is_backlight_modified(&old_level);
                ret = read_hist(FBFd, &hist);
                if(ret != 0) {
                    LOGD_IF(mDebug, "%s: Do Histogram read failed - ret = %d",
                        __FUNCTION__, ret);
                    pthread_mutex_lock(&mAbaLock);
                    continue;
                }

                if(!copyHistogram(&hist, mInputHistogram)){
                    curr_hist = mInputHistogram;
                    //Preprocess Histogram only if not MDP5
                    if(!gMDP5) {

                        retval = AbaPreprocessHistogram((pHandle),
                        mInputHistogram, mOutputHistogram);

                        if(retval == ABA_STATUS_FAIL ||
                            retval == ABA_STATUS_BAD_PARAM ||
                                        retval == ABA_STATUS_NOT_SUPPORTED) {
                            LOGE("%s: PreProcess failed retval = %d",
                                __FUNCTION__,retval);
                            pthread_mutex_lock(&mAbaLock);
                            continue;
                        }
                        curr_hist = mOutputHistogram;
                    }
                }
                LOGD_IF(mDebug,"%s: Backlight value= %d", __FUNCTION__,mBl_lvl);
            } else {
                pthread_mutex_unlock(&mAbaLock);
            }

            retval = AbaProcess(pHandle, curr_hist, minLUT, &mBl_lvl);
            if (retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM ||
                        retval == ABA_STATUS_NOT_SUPPORTED) {
                LOGE("%s: AbaProcess failed ret = %d", __FUNCTION__, retval);
                pthread_mutex_lock(&mAbaLock);
                continue;
            }

            if (mBl_lvl != set_ratio) {
                LOGD_IF(mCablDebug, "User level = %03d Set Ratio = %03u \
                    Scale Ratio = %04u", mUserBLLevel, set_ratio,
                    bl_scale_ratio);

                AbaGetState((pHandle), &CablState, ABA_FEATURE_CABL);
                if((CablState == ABA_STATE_ACTIVE) &&
                    (IsABAStatusON(ABA_FEATURE_CABL))){
                    if (is_backlight_modified(&old_level)) {
                        pthread_mutex_lock(&mAbaLock);
                        continue;
                    }
                }

                set_backlight_scale(mBl_lvl, mCablOEMParams.bl_min_level);
                set_ratio = mBl_lvl;

            }

            retval = AbaIsConverged(pHandle, &IsConverged);
            if(retval != ABA_STATUS_SUCCESS){
                    IsConverged = 0;
            }

            if(!IsConverged) {
                LOGD_IF(mDebug, "Updating LUT and calling Refresh!");
                ret = outCmap(FBFd, minLUT, mColorMap);
                if (ret != 0) {
                    LOGE("%s: outCmap() failed", __FUNCTION__);
                    pthread_mutex_lock(&mAbaLock);
                    continue;
                }
                if (pRefresher)
                    pRefresher->Refresh(1,16);
            }

            pthread_mutex_lock(&mAbaLock);
        }
        pthread_mutex_unlock(&mAbaLock);
        /*
         * Immediate unlock and lock to provide any thread waiting for lock chance to acquire
        */
        pthread_mutex_lock(&mAbaLock);
    }
    pthread_mutex_unlock(&mAbaLock);

    if (pRefresher){
        LOGD_IF(mDebug, "%s: Calling ScreenRefresher->Control(0)", __func__);
        pRefresher->Control(0);
    }

    /* Cleanup  */
    pthread_mutex_lock(&mAbaLock);
    mWorkerRunning = false;
    pthread_mutex_unlock(&mAbaLock);

    free_cmap(&mColorMap);

    pthread_mutex_lock(&mAbaLock);
    mHistStatus = 0;
    pthread_mutex_unlock(&mAbaLock);

    set_backlight_scale(BL_SCALE_MAX, mCablOEMParams.bl_min_level);

    //Stop the running ALS Updater Thread
    if((mSVIEnable) && (mALSUThreadRunning))
        StopALSUpdaterThread();

    if(pRefresher)
        delete pRefresher;

    LOGD_IF(mDebug, "%s(): Exiting ", __func__);

    pthread_join(pthread_self(),&term);
    pthread_exit(NULL);
    return;

exit_cleanup:
    pthread_mutex_lock(&mAbaLock);
    if(mWorkerRunning)
        mWorkerRunning = false;
    pthread_mutex_unlock(&mAbaLock);

    //Stop the running ALS Updater Thread
    if((mSVIEnable) && (mALSUThreadRunning))
        StopALSUpdaterThread();

    if(pRefresher)
        delete pRefresher;

    LOGE_IF(mDebug, "%s(): Exiting with error", __func__);

    pthread_join(pthread_self(),&term);
    pthread_exit(NULL);
}

int AbaContext::Notify(int notification_type) {
    int32_t ret = 0;
    uint32_t level = 0;
    LOGD_IF(mDebug, "Starting %s ", __func__);
    pthread_mutex_lock(&mAbaLock);
    if (notification_type == NOTIFY_TYPE_UPDATE && mHistStatus == 0) {
        pthread_mutex_unlock(&mAbaLock);
        level = get_backlight_level();
        if (level <= mCablOEMParams.bl_min_level) {
            LOGE_IF(mCablDebug, "New BL level %d lower than min level %u,"
                    " Skip this update for calc",
                    level, mCablOEMParams.bl_min_level);
            return -1;
        }
        if (!IsABAStatusON(ABA_FEATURE_CABL | ABA_FEATURE_SVI))
            return ret;
        LOGD_IF(mDebug, "Start notification received, start histogram");
        ret = startHistogram();
        if (0 == ret) {
            pthread_mutex_lock(&mAbaLock);
            mHistStatus = 1;
            pthread_mutex_unlock(&mAbaLock);
            LOGD_IF(mDebug, "%s: Filtered signal called from Notify Start", __FUNCTION__);
            FilteredSignal();
        }
    } else if (mHistStatus == 1 &&
        (notification_type == NOTIFY_TYPE_SUSPEND ||
        notification_type == NOTIFY_TYPE_NO_UPDATE)) {
        LOGD_IF(mDebug, "Stop notification received, stop histogram");
        if(!stopHistogram()){
            mHistStatus = 0;
        }
        pthread_mutex_unlock(&mAbaLock);
        if((notification_type == NOTIFY_TYPE_SUSPEND) && (mSVIEnable)){
            StopALSUpdaterThread();
        }

    } else {
        pthread_mutex_unlock(&mAbaLock);
    }
    return ret;
}

/* SetLightSensor() should be called only when mSVIEnable is true*/
void AbaContext::SetLightSensor() {
    int ret;
    char property[PROPERTY_VALUE_MAX];

    if (property_get(SVI_SENSOR_PROP, property, 0) > 0) {
        int type = atoi(property);
        if (type == 0) {
            mLightSensor = new ALS();
        } else if (type == 1) {
            mLightSensor = new DummySensor();
        } else if (type == 2) {
            mLightSensor = new NativeLightSensor();
#ifdef ALS_ENABLE
        } else if (type == 3) {
            mLightSensor = new LightSensor();
#endif
        } else {
            LOGE("Invalid choice for sensor type, initializing the default sensor class!");
            mLightSensor = new ALS();
        }
    } else {
#ifdef ALS_ENABLE
        mLightSensor  = new LightSensor();
#else
        mLightSensor = new NativeLightSensor();
#endif
    }

    // Registering the ALS
    ret = mLightSensor->ALSRegister();
    if( ret ) {
        LOGE("%s(): ALSRegister() Failed : Return Value = %d", __func__, ret);
    }
}

/* CleanupLightSensor() should be called only when mSVIEnable is true*/
void AbaContext::CleanupLightSensor() {
    int ret;
    //CleanUp ALS
    ret = mLightSensor->ALSCleanup();
    if( ret ) {
        LOGE("%s(): ALSCleanup() Failed : Return Value = %d", __func__, ret);
    }
    delete mLightSensor;
}

/* StopALSUpdaterThread should be called only when mSVIEnable is true*/
void AbaContext::StopALSUpdaterThread(){

    LOGD_IF(mDebug, "%s(): Entering ", __func__);
    int ret;

    //Disabling ALS
    ret = mLightSensor->ALSDeRegister();
    if (ret) {
        LOGE("%s(): ALSDeRegister() Failed : Return Value = %d",__func__,ret);
    } else {
        pthread_mutex_lock(&mALSUpdateLock);
        bALSRunEnabled = false;
        pthread_mutex_unlock(&mALSUpdateLock);
    }
    //Stopping the ALS Updater thread
    pthread_mutex_lock(&mALSUpdateLock);
    mALSUpdateThreadStatus = ALS_UPDATE_OFF;
    //Setting mCurrALSValue to 1 for moving algo to CABL side
    mCurrALSValue = 1;
    pthread_mutex_unlock(&mALSUpdateLock);

    LOGD_IF(mDebug, "%s(): Exiting ", __func__);
}

/* StartALSUpdaterThread should be called only when mSVIEnable is true*/
void AbaContext::StartALSUpdaterThread(){

    LOGD_IF(mDebug, "%s(): Entering ", __func__);
    int ret_val;
    int32_t err;

    //Enabling ALS
    ret_val = mLightSensor->ALSRun(false);
    if(ret_val) {
        LOGE("%s(): ALSRun() Failed : Return Value = %d",__func__, ret_val);
    } else{
        bALSRunEnabled = true;
    }

    //Checkiung if the ALS Updation thread is running or not
    pthread_mutex_lock(&mALSUpdateLock);
    if (mALSUThreadRunning){
        pthread_mutex_unlock(&mALSUpdateLock);
        goto exit_func;
    }
    pthread_mutex_unlock(&mALSUpdateLock);

    //Creating a separate thread for ALS Updation
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* update the Thread status */
    pthread_mutex_lock(&mALSUpdateLock);
    mALSUThreadRunning = true;
    mALSUpdateThreadStatus = ALS_UPDATE_ON;
    pthread_mutex_unlock(&mALSUpdateLock);

    LOGD_IF(mDebug, "%s: Trying to create a new thread", __func__);
    err = pthread_create(&mALSUpdateThread, &attr, als_update_func, this);
    if (err) {
        LOGE("%s: ALSUpdate: Failed to start the updater thread", __FUNCTION__);
        mALSUThreadRunning = false;
        mALSUpdateThreadStatus = ALS_UPDATE_OFF;
    }
    pthread_attr_destroy(&attr);

exit_func:

    LOGD_IF(mDebug, "%s(): Exiting ", __func__);
}

void AbaContext::ProcessAbaALSUpdater(){

    int ret_val, tmp_ALS = 1, prev_ALS = 1, diff_ALS = 0, worker_ALS = 0;
    void *term;

    LOGD_IF(mDebug, "%s(): Entering ", __func__);

    pthread_mutex_lock(&mALSUpdateLock);
    while(mALSUpdateThreadStatus == ALS_UPDATE_ON){
        if(bALSRunEnabled){
            worker_ALS = mLastALSValue;
            pthread_mutex_unlock(&mALSUpdateLock);

            tmp_ALS = mLightSensor->ALSReadSensor();
            tmp_ALS = (tmp_ALS > 0) ? tmp_ALS : 1;
            LOGD_IF(mDebug, "%s: ALS value read = %d", __func__, tmp_ALS);
            diff_ALS = calculateDiffALS(tmp_ALS, worker_ALS);
            if (diff_ALS)
                FilteredSignal();

            if (calculateDiffALS(tmp_ALS, prev_ALS)) {
                prev_ALS = tmp_ALS;
                pthread_mutex_lock(&mALSUpdateLock);
                mCurrALSValue = tmp_ALS;
                pthread_mutex_unlock(&mALSUpdateLock);
            }

        } else {
            pthread_mutex_unlock(&mALSUpdateLock);
            ret_val = mLightSensor->ALSRun(false);
            if(ret_val) {
                LOGE("%s(): ALSRun() Failed : Return Value = %d", __func__,
                    ret_val);
                pthread_mutex_lock(&mALSUpdateLock);
                continue;
            }
            bALSRunEnabled = true;
        }
        pthread_mutex_lock(&mALSUpdateLock);
    }
    pthread_mutex_unlock(&mALSUpdateLock);

    pthread_mutex_lock(&mALSUpdateLock);
    mALSUThreadRunning =false;
    pthread_mutex_unlock(&mALSUpdateLock);

    LOGD_IF(mDebug, "%s(): Exiting ", __func__);

    pthread_join(pthread_self(),&term);
    pthread_exit(NULL);
}

int32_t AbaContext::CABLControl(bool bEnable) {
    AbaStatusType retval;
    LOGD_IF(mCablDebug, "Start_CABL E");
    pthread_mutex_lock(&mCABLCtrlLock);
    if (mAbaHwInfo.uHistogramBins== 0) {
        LOGE("%s: CABL not supported on this HW!", __FUNCTION__);
        pthread_mutex_unlock(&mCABLCtrlLock);
        return -1;
    }

    if (!mCablEnable) {
        LOGE("%s: CABL not enabled!", __FUNCTION__);
        pthread_mutex_unlock(&mCABLCtrlLock);
        return -1;
    }

    if(bEnable){ //CABL ON
        if (IsABAStatusON(ABA_FEATURE_CABL)) {
            LOGD("%s: CABL is already on!", __FUNCTION__);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return 0;
        }
        retval = AbaGetDefaultParams(eCABLConfig, &mCablOEMParams,
            sizeof(mCablOEMParams));
        if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM ||
            retval == ABA_STATUS_NOT_SUPPORTED) {
            LOGE("%s: GetDefaultParams Failed retval = %d", __FUNCTION__, retval);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return retval;
        }

        /* oem specific initialization */
        mCABLCP.ConfigureCABLParameters(&mCablOEMParams);

        /* driver initialization */
        retval = AbaInit(eCABLConfig, &mCablOEMParams, sizeof(mCablOEMParams),
            &mAbaHwInfo, pHandle);
        if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM ||
            retval == ABA_STATUS_NOT_SUPPORTED) {
            LOGE("%s: AbaInit() Failed retval = %d", __FUNCTION__, retval);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return retval;
        }

        /* Get the backlight level and initialize the new level to it */
        mUserBLLevel = get_backlight_level();
        /* Following api is called only for CABL*/
        retval = AbaSetOriginalBacklightLevel(pHandle, mUserBLLevel);
        if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM ||
            retval == ABA_STATUS_NOT_SUPPORTED) {
            LOGE("%s: AbaSetOriginalBacklightLevel Failed retval = %d",
                __FUNCTION__, retval);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return retval;
        }

        /* Get the user defined power saving level */
        char property[PROPERTY_VALUE_MAX];
        if (property_get("hw.cabl.level", property, NULL) > 0) {
            CABLQualityLevelType newLvl = (CABLQualityLevelType) ql_string2int(property);
            if ((newLvl < 0) || (newLvl > ABL_QUALITY_MAX)) {
                mQualityLevel = (CABLQualityLevelType) ABA_QUALITY_HIGH;
                LOGE("%s: Invalid power saving level, setting hw.cabl.level to High",
                    __FUNCTION__);
            } else if (newLvl < ABL_QUALITY_MAX) {
                mQualityLevel = (CABLQualityLevelType) newLvl;
                LOGD_IF(mCablDebug, "Quality level: %d", mQualityLevel);
            }
        }

        retval = AbaSetQualityLevel(pHandle,(AbaQualityLevelType) mQualityLevel);
        if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM ||
            retval == ABA_STATUS_NOT_SUPPORTED) {
            LOGE("%s: AbaSetQualityLevel(CABL) failed retval = %d", __FUNCTION__,
                retval);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return retval;
        }

        retval = AbaActivate(pHandle,ABA_FEATURE_CABL);
        if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM ||
            retval == ABA_STATUS_NOT_SUPPORTED) {
            LOGE("%s: AbaActivate() failed retval = %d", __FUNCTION__, retval);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return retval;
        }

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        /* update the CABL status so threads can run */
        SetABAStatusON(ABA_FEATURE_CABL);
        LOGD_IF(mCablDebug, "Start_CABL sets CABL STATUS TO CABL_ON");
        /* Check if worker thread exists else start*/
        if(!mWorkerRunning){
            stopHistogram();
            initialize_cmap(&mColorMap);
            int32_t err = pthread_create(&mWorkerThread, &attr, aba_worker_func, this);
            if (err) {
                LOGE("%s: CABL: Failed to start the control thread", __FUNCTION__);
                SetABAStatusOFF(ABA_FEATURE_CABL);
                free_cmap(&mColorMap);
                pthread_attr_destroy(&attr);
                pthread_mutex_unlock(&mCABLCtrlLock);
                return -1;
            }
        }
        pthread_attr_destroy(&attr);
        LOGD_IF(mCablDebug, "Start_CABL X");
        pthread_mutex_unlock(&mCABLCtrlLock);
        return 0;

    } else { //CABL Stop
        LOGD_IF(mCablDebug, "Stop_CABL E");
        if (!IsABAStatusON(ABA_FEATURE_CABL)) {
            LOGD("%s: CABL is already off!", __FUNCTION__);
            pthread_mutex_unlock(&mCABLCtrlLock);
            return -1;
        }
        SetABAStatusOFF(ABA_FEATURE_CABL);
        AbaDisable(pHandle,ABA_FEATURE_CABL);
        LOGD_IF(mCablDebug, "Stopped CABL X");
    }
    pthread_mutex_unlock(&mCABLCtrlLock);
    return 0;
}

int AbaContext::SVIControl(bool bEnable) {
    int ret = -1;
    AbaStatusType retval;
    pthread_mutex_lock(&mSVICtrlLock);

    if(mSVISupported) {
        if (bEnable) {
            if (IsABAStatusON(ABA_FEATURE_SVI)) {
                LOGD(" %s %d SVI is already on", __FUNCTION__, __LINE__);
                pthread_mutex_unlock(&mSVICtrlLock);
                return ret;
            }

            if (mSVIEnable) {
                pthread_mutex_lock(&mAbaLock);
                SetABAStatusON(ABA_FEATURE_SVI);
                pthread_mutex_unlock(&mAbaLock);

                retval = AbaGetDefaultParams(eSVIConfig, &mSVIOEMParams, sizeof(mSVIOEMParams));
                if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM || retval == ABA_STATUS_NOT_SUPPORTED) {
                    LOGE("%s: GetDefaultParams Failed retval = %d", __FUNCTION__, retval);
                    pthread_mutex_unlock(&mSVICtrlLock);
                    return retval;
                }

                /* oem specific initialization */
                mSVICP.ConfigureSVIParameters(&mSVIOEMParams);

                /* driver initialization */
                retval = AbaInit(eSVIConfig, &mSVIOEMParams, sizeof(mSVIOEMParams), &mAbaHwInfo, pHandle);
                if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM || retval == ABA_STATUS_NOT_SUPPORTED) {
                    LOGE("%s: AbaInit() Failed retval = %d", __FUNCTION__, retval);
                    pthread_mutex_unlock(&mSVICtrlLock);
                    return retval;
                }

                retval = AbaActivate(pHandle,ABA_FEATURE_SVI);
                if(retval == ABA_STATUS_FAIL || retval == ABA_STATUS_BAD_PARAM || retval == ABA_STATUS_NOT_SUPPORTED) {
                    LOGE("%s: AbaActivate(SVI) failed retval = %d", __FUNCTION__, retval);
                    pthread_mutex_unlock(&mSVICtrlLock);
                    return retval;
                } else {
                    LOGD_IF(mSVIDebug, "%s: Activated SVI feature", __FUNCTION__);
                }

                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
                /* Check if worker thread is running else start */
                if(!mWorkerRunning){
                    stopHistogram();
                    initialize_cmap(&mColorMap);
                    ret = pthread_create(&mWorkerThread, &attr, aba_worker_func, this);
                    if (ret) {
                        SetABAStatusOFF(ABA_FEATURE_SVI);
                        free_cmap(&mColorMap);
                        LOGE("%s:  Failed to start the ad_thread thread", __FUNCTION__);
                        pthread_attr_destroy(&attr);
                        pthread_mutex_unlock(&mSVICtrlLock);
                        return ret;
                    }
                }
                pthread_attr_destroy(&attr);
                StartALSUpdaterThread();
            } else {
                LOGE("%s: SVI is not enabled!", __FUNCTION__);
            }
        } else {
            if (!IsABAStatusON(ABA_FEATURE_SVI)) {
                LOGD("%s: SVI is already off!", __FUNCTION__);
                pthread_mutex_unlock(&mSVICtrlLock);
                return ret;
            }
            StopALSUpdaterThread();
            if (IsABAStatusON(ABA_FEATURE_SVI)) {
                SetABAStatusOFF(ABA_FEATURE_SVI);
                AbaDisable(pHandle,ABA_FEATURE_SVI);
            }
        }
    }
    pthread_mutex_unlock(&mSVICtrlLock);
    return 0;
}
