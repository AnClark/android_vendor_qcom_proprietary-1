/*
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef _SCREENREFRESHER_H
#define _SCREENREFRESHER_H

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

#include "PPDaemonUtils.h"
#include "lib-postproc.h"

#define REFRESHER_STATE_ENABLE 0x1
#define REFRESHER_STATE_CONFIG 0x2

class ScreenRefresher {
    uint32_t mState;
    uint32_t mFrames;
    uint32_t mMS;
    uint32_t mRefCount;
    pthread_t mThread;
    pthread_mutex_t mLock;
    pthread_cond_t  mWaitCond;
    void ProcessRefreshWork();
    static void *refresher_thrd_func(void *obj) {
        reinterpret_cast<ScreenRefresher *>(obj)->ProcessRefreshWork();
        return NULL;
    }
public:
    bool mDebug;
    ScreenRefresher(): mState(0), mFrames(0), mMS(0), mRefCount(0), mDebug(false){
        pthread_mutex_init(&mLock, NULL);
        pthread_cond_init(&mWaitCond, NULL);
        /* set the flags based on property */
        char property[PROPERTY_VALUE_MAX];
        if (property_get("debug.refresh.logs", property, 0) > 0 && (atoi(property) > 0 )) {
            mDebug = true;
        }
    }
    ~ScreenRefresher() {
        pthread_mutex_destroy(&mLock);
        pthread_cond_destroy(&mWaitCond);
    }

    int Control(bool bEnable);
    int Refresh(uint32_t nFrames, uint32_t nMS);
    int Notify(int notification_type);
};

#endif /* _SCREENREFRESHER_H */
