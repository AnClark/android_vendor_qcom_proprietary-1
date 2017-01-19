/*
 * DESCRIPTION
 * This file contains a helper class for sending display commits or screen
 * refreshes when needed for postprocessing features.
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 */

#include "ScreenRefresher.h"
#include <time.h>
#include <QServiceUtils.h>

void ScreenRefresher::ProcessRefreshWork() {
    int32_t ret =-1;
    uint32_t count = 0;
    struct timespec wait_time;

    LOGD_IF(mDebug, "%s() Entering, mState = %d", __func__, mState);
    pthread_mutex_lock(&mLock);

    while (mRefCount) {
        if (!(mState & REFRESHER_STATE_CONFIG)) {
            LOGD_IF(mDebug, " %s(): Wait for mWaitCond!", __func__);
            pthread_cond_wait(&mWaitCond, &mLock); //Wait for Refresh()
            if (mRefCount == 0) {
                LOGD_IF(mDebug, " %s(): ScreenRefresh disabled.", __func__);
                goto exit;
            }
        }

        count = 0;

        LOGD_IF(mDebug, " %s(): mState = %d, mFrames = %d", __func__, mState, mFrames);
        while (mFrames && count < mFrames ) {
            LOGD_IF(mDebug, "%s(): mFrames = %d, count = %d", __func__, mFrames, count);
            pthread_mutex_unlock(&mLock);
            ret = screenRefresh();
            if(ret < 0) {
                LOGE("%s: Failed to signal HWC", strerror(errno));
            } else {
                LOGD_IF(mDebug, "%s: screenRefresh successful!", __func__);
            }
            count++;

            clock_gettime(CLOCK_REALTIME, &wait_time);
            wait_time.tv_nsec += mMS*1000000;

            pthread_mutex_lock(&mLock);
            ret = pthread_cond_timedwait(&mWaitCond, &mLock, &wait_time);
            if (mRefCount == 0) {
                LOGD_IF(mDebug, " %s(): ScreenRefresh disabled.", __func__);
                goto exit;
            }
            if (ret == 0) {
                count = 0;
            } else if (ret == ETIMEDOUT) {
                continue;
            } else {
                LOGE("%s: pthread_cond_timedwait failed. err: %s", __func__, strerror(errno));
            }
        }
        mState &= ~REFRESHER_STATE_CONFIG;
    }
exit:
    mState &= ~(REFRESHER_STATE_ENABLE | REFRESHER_STATE_CONFIG);
    pthread_mutex_unlock(&mLock);
    LOGD_IF(mDebug, "%s() Exiting!", __func__);
    return;
}

int ScreenRefresher::Control(bool bEnable) {
    int ret = 0;

    LOGD_IF(mDebug, "%s(%d) Entering!", __func__, bEnable);
    if( bEnable ) {
        pthread_mutex_lock(&mLock);
        if( mState & REFRESHER_STATE_ENABLE ) {
            LOGD_IF(mDebug, "%s() Thread is already there", __func__);
            ret = -1;
            mRefCount = mRefCount + 1;
            pthread_mutex_unlock(&mLock);
            return ret;
        }
        /*start the invalidate thread here*/
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        LOGD_IF(mDebug, "Starting the refresh thread!");
        ret = pthread_create(&mThread, &attr, refresher_thrd_func, this);
        if (ret)
            LOGE("Failed to create screen refresher thread, error = %s", strerror(ret));
        else {
            mRefCount = mRefCount + 1;
            mState |= REFRESHER_STATE_ENABLE;
        }
        pthread_attr_destroy(&attr);
        pthread_mutex_unlock(&mLock);
    } else {
        pthread_mutex_lock(&mLock);
        if (mRefCount > 0) {
            mRefCount = mRefCount - 1;
            if (mRefCount == 0) {
                LOGD_IF(mDebug, "Stopping the refresh thread!");
                pthread_cond_signal(&mWaitCond);
            }
        }
        pthread_mutex_unlock(&mLock);
    }

    LOGD_IF(mDebug, "%s() Exiting, ret = %d!", __func__, ret);
    return ret;
}

int ScreenRefresher::Refresh(uint32_t nFrames, uint32_t nMS) {
    int ret = -1;

    LOGD_IF(mDebug, "%s() Entering!", __func__);
    pthread_mutex_lock(&mLock);
    if (mState & REFRESHER_STATE_ENABLE) {
        mFrames = nFrames;
        mMS = nMS;
        mState |= REFRESHER_STATE_CONFIG;
        pthread_cond_signal(&mWaitCond);
        LOGD_IF(mDebug, "Sending mWaitCond signal!");
        ret = 0;
    }
    pthread_mutex_unlock(&mLock);
    LOGD_IF(mDebug, "%s() Exiting, ret = %d!", __func__, ret);
    return ret;
}

int ScreenRefresher::Notify(int notification_type) {
    LOGD_IF(mDebug, "%s() Entering!", __func__);

    LOGD_IF(mDebug, "%s() Exiting, ret = -1!", __func__);
    return -1;
}
