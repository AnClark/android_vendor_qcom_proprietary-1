/*
 * DESCRIPTION
 * This file runs the daemon for postprocessing features.
 * It listens to the socket for client connections and controls the features
 * based on commands it received from the clients.
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 */

#include "DaemonContext.h"
#include <time.h>

int32_t DaemonContext::reply(bool result, const int32_t& fd) {
    char buf[32];
    memset(buf, 0x00, sizeof(buf));

    if (fd < 0)
        return 0;

    if (result)
        snprintf(buf, sizeof(buf), "Success\n");
    else
        snprintf(buf, sizeof(buf), "Failure\n");
    int32_t ret = write(fd, buf, strlen(buf));
    if(ret == -1) {
        LOGE("Failed to reply back: %s", strerror(errno));
    }
    return ret;
}

int32_t DaemonContext::reply(const char *reply, const int32_t& fd) {
    if (NULL == reply) {
        LOGE("Reply string is NULL!");
        return -1;
    }

    if (fd < 0)
        return 0;

    int32_t ret = write(fd, reply, strlen(reply));
    if(ret == -1) {
        LOGE("Failed to reply back: %s", strerror(errno));
    }
    return ret;
}

int DaemonContext::ProcessCABLMsg(char* buf) {
    int32_t result = 1;

    LOGD_IF(mDebug, "PROCESS_CABL_MSG(): Command : %s", buf);
    if(!strncmp(buf, CMD_CABL_ON, strlen(CMD_CABL_ON))) {
        pthread_mutex_lock(&mCABLOpLock);
        if(bAbaEnabled){
            mABA->SetCABLFeature();
            result = mABA->CABLControl(true);
        } else {
            result = mCABL->start_cabl();
        }
        pthread_mutex_lock(&mCtrlLock);
        if (!result)
            mCtrlStatus |= cabl_bit;
        pthread_mutex_unlock(&mCtrlLock);
        pthread_mutex_unlock(&mCABLOpLock);
    } else if (!strncmp(buf, CMD_CABL_OFF, strlen(CMD_CABL_OFF))) {
        pthread_mutex_lock(&mCABLOpLock);
        if(bAbaEnabled){
            result = mABA->CABLControl(false);
        } else {
            mCABL->stop_cabl();
            result = 0;
        }
        pthread_mutex_lock(&mCtrlLock);
        mCtrlStatus &= ~cabl_bit;
        pthread_mutex_unlock(&mCtrlLock);
        pthread_mutex_unlock(&mCABLOpLock);
    } else if (!strncmp(buf, CMD_CABL_SET, strlen(CMD_CABL_SET))) {
        char *c_lvl = NULL, *tmp = NULL, *bufr;
        tmp = strtok_r(buf, " ", &bufr);
        while (tmp != NULL) {
            c_lvl = tmp;
            tmp = strtok_r(NULL, " ", &bufr);
        }
        if (c_lvl == NULL) {
            LOGE("Invalid quality level!");
            return -1;
        }
        if (!strcmp(c_lvl, CABL_LVL_AUTO)) {
            pthread_mutex_lock(&mCABLOpLock);
            if(bAbaEnabled){
                mABA->SetQualityLevel(ABL_QUALITY_HIGH);
                result = AbaSetQualityLevel(mABA->GetHandle(),
                (AbaQualityLevelType) ABL_QUALITY_HIGH);
            } else {
                pthread_mutex_lock(&mCABL->mCABLLock);
                mCABL->mPowerSaveLevel = ABL_QUALITY_HIGH;
                mCABL->mPowerLevelChange = true;
                result = 0;
                pthread_mutex_unlock(&mCABL->mCABLLock);
            }
            if (!result) {
                property_set("hw.cabl.level", c_lvl);
                if(bAbaEnabled){
                    mABA->SetDefaultQualityMode(AUTO_QL_MODE);
                } else {
                    pthread_mutex_lock(&mCABL->mCABLLock);
                    mCABL->mOEMParams.default_ql_mode = AUTO_QL_MODE;
                    pthread_mutex_unlock(&mCABL->mCABLLock);
                }
            }
            pthread_mutex_unlock(&mCABLOpLock);
        } else {
            if(bAbaEnabled){
                CABLQualityLevelType i_lvl = (CABLQualityLevelType) ql_string2int(c_lvl);
                if((int)i_lvl < 0){
                    LOGE("Invalid quality level!");
                    result = -1;
                } else {
                    if (i_lvl == mABA->GetQualityLevel()) {
                        LOGD_IF(mDebug, "ABA CABL Power level has not changed!");
                        pthread_mutex_lock(&mCABLOpLock);
                        mABA->SetDefaultQualityMode(USER_QL_MODE);
                        pthread_mutex_unlock(&mCABLOpLock);
                        result = 0;
                    } else {
                        pthread_mutex_lock(&mCABLOpLock);
                        result = AbaSetQualityLevel(mABA->GetHandle(), (AbaQualityLevelType) i_lvl);
                        if (!result) {
                            property_set("hw.cabl.level", c_lvl);
                            mABA->SetQualityLevel((CABLQualityLevelType) i_lvl);
                            mABA->SetDefaultQualityMode(USER_QL_MODE);
                        }
                        pthread_mutex_unlock(&mCABLOpLock);
                    }
                }
            } else {
                int i_lvl = ql_string2int(c_lvl);
                if(i_lvl < 0){
                    LOGE("Invalid quality level!");
                    result = -1;
                } else {
                    pthread_mutex_lock(&mCABLOpLock);
                    pthread_mutex_lock(&mCABL->mCABLLock);
                    if (i_lvl == mCABL->mPowerSaveLevel) {
                        LOGD_IF(mDebug, "CABL Power level has not changed!");
                        mCABL->mOEMParams.default_ql_mode = USER_QL_MODE;
                        result = 0;
                    } else {
                        property_set("hw.cabl.level", c_lvl);
                        mCABL->mPowerSaveLevel = i_lvl;
                        mCABL->mOEMParams.default_ql_mode = USER_QL_MODE;
                        mCABL->mPowerLevelChange = true;
                        result = 0;
                    }
                    pthread_mutex_unlock(&mCABL->mCABLLock);
                    pthread_mutex_unlock(&mCABLOpLock);
                }
            }
        }
    }
    return result;
}

int DaemonContext::ProcessADMsg(const char* buf){
    int32_t result = 1;

    LOGD_IF(mDebug, "PROCESS_AD_MSG(): Command : %s", buf);

    if (!strncmp(buf, CMD_AD_CALIB_ON, strlen(CMD_AD_CALIB_ON))) {
        pthread_mutex_lock(&mADOpLock);
        if (mAD.ADStatus() == AD_ON) {
            /* stop ad first before turning on ad_calib mode */
            result = mAD.ADControl(this, false);
            if (result) {
                pthread_mutex_unlock(&mADOpLock);
                return result;
            }
        }
        result = mAD.ADControl(this, true, ad_mode_calib, MDP_LOGICAL_BLOCK_DISP_0);
        pthread_mutex_unlock(&mADOpLock);
    } else if (!strncmp(buf, CMD_AD_CALIB_OFF, strlen(CMD_AD_CALIB_OFF))) {
        pthread_mutex_lock(&mADOpLock);
        if ( mAD.ADStatus() == AD_CALIB_ON ) {
            result = mAD.ADControl(this, false);
        }
        pthread_mutex_unlock(&mADOpLock);
    } else if (!strncmp(buf, CMD_AD_ON, strlen(CMD_AD_ON))) {
        ad_mode mode;
        int display_id = MDP_LOGICAL_BLOCK_DISP_0;
        uint32_t flag = 0;
        char c_mode[32];
        sscanf(buf, CMD_AD_ON ";" "%d" ";" "%d" ";" "%u",(int*)&mode, &display_id, &flag);
        if(!mSplitDisplay && flag != 0) {
           LOGE("Invalid input, target doesn't support AD split mode");
           return result;
        }
        pthread_mutex_lock(&mADOpLock);
        if (mAD.IsADInputValid(mode, display_id)) {
            if (mAD.ADStatus() == AD_CALIB_ON) {
            /* stop ad_calib first before turning on ad mode */
                result = mAD.ADControl(this, false);
                if (result) {
                    pthread_mutex_unlock(&mADOpLock);
                    return result;
                }
            }
            if (mAD.ADStatus() == AD_ON && (mode != mAD.ADMode() || mAD.mFlags != flag)) {
                result = mAD.ADControl(this, false);
                if (result) {
                    pthread_mutex_unlock(&mADOpLock);
                    return result;
                }
            }
            mAD.mFlags = (MDSS_PP_SPLIT_MASK & flag);
            result = mAD.ADControl(this, true, mode, display_id);
            pthread_mutex_lock(&mCtrlLock);
            if (!result) {
                snprintf(c_mode, (size_t) sizeof(c_mode), "%d", mode);
                property_set("hw.ad.mode",c_mode);
                mCtrlStatus |= ad_bit;
            }
            pthread_mutex_unlock(&mCtrlLock);
        }
        pthread_mutex_unlock(&mADOpLock);
    } else if (!strncmp(buf, CMD_AD_OFF, strlen(CMD_AD_OFF))) {
        pthread_mutex_lock(&mADOpLock);
        if (mAD.ADStatus() == AD_ON) {
            result = mAD.ADControl(this, false);
            pthread_mutex_lock(&mCtrlLock);
            if (!result)
                mCtrlStatus &= ~ad_bit;
            pthread_mutex_unlock(&mCtrlLock);
        } else {
            LOGD_IF(mDebug, "AD is already off!");
            result = 0;
        }
        pthread_mutex_unlock(&mADOpLock);
    } else if (!strncmp(buf, CMD_AD_INIT, strlen(CMD_AD_INIT))) {
        char *params = NULL;
        params = strchr(buf, ';');
        if (params == NULL) {
            LOGE("Invalid format for input command");
        } else {
            params = params + 1;
            if (mAD.ADStatus() == AD_CALIB_ON) {
                pthread_mutex_lock(&mADOpLock);
                result = mAD.ADInit(params);

                if (mAD.mRefresher) {
                    LOGD_IF(mAD.mRefresher->mDebug, "Calling refresh()");
                    mAD.mRefresher->Refresh(1, AD_REFRESH_INTERVAL);
                }

                if (mAD.ADLastSentALSValue() >= 0)
                    mAD.ADUpdateAL(mAD.ADLastSentALSValue(), AD_REFRESH_CNT);
                pthread_mutex_unlock(&mADOpLock);
            } else {
                LOGE("AD calibration mode is not ON!!");
            }
        }
    } else if (!strncmp(buf, CMD_AD_CFG, strlen(CMD_AD_CFG))) {
        char *params = NULL;
        params = strchr(buf, ';');
        if (params == NULL) {
            LOGE("Invalid format for input command");
        } else {
            params = params + 1;
            if (mAD.ADStatus() == AD_CALIB_ON) {
                pthread_mutex_lock(&mADOpLock);
                result = mAD.ADConfig(params);

                if (mAD.mRefresher) {
                    LOGD_IF(mAD.mRefresher->mDebug, "Calling refresh()");
                    mAD.mRefresher->Refresh(1, AD_REFRESH_INTERVAL);
                }

                if (mAD.ADLastSentALSValue() >= 0)
                    mAD.ADUpdateAL(mAD.ADLastSentALSValue(), AD_REFRESH_CNT);
                pthread_mutex_unlock(&mADOpLock);
            } else {
                LOGE("AD calibration mode is not ON!!");
            }
        }
    } else if (!strncmp(buf, CMD_AD_INPUT, strlen(CMD_AD_INPUT))) {
        int32_t lux_value;
        uint32_t enableManualInput = 0;
        sscanf(buf, CMD_AD_INPUT ";" "%d" ";" "%u", &lux_value, &enableManualInput);
        if ((mAD.ADStatus() == AD_CALIB_ON) || (mAD.ADStatus() == AD_ON)) {
            pthread_mutex_lock(&mADOpLock);
            if (enableManualInput == 1) {
                mAD.ADControl(this, CONTROL_PAUSE);
                mAD.mLastManualInput = lux_value;
            } else {
                mAD.ADControl(this, CONTROL_RESUME);
            }

            result = mAD.ADUpdateAL(lux_value, AD_REFRESH_CNT);

            pthread_mutex_unlock(&mADOpLock);
        } else {
            LOGE("AD is not ON!!");
        }
    } else if (!strncmp(buf, CMD_AD_ASSERTIVENESS, strlen(CMD_AD_ASSERTIVENESS)) ||
            !strncmp(buf, CMD_AD_STRLIMIT, strlen(CMD_AD_STRLIMIT))) {
        pthread_mutex_lock(&mADOpLock);
        if (mAD.ADStatus() == AD_ON) {
            int32_t value;
            if (!strncmp(buf, CMD_AD_ASSERTIVENESS, strlen(CMD_AD_ASSERTIVENESS))) {
                sscanf(buf, CMD_AD_ASSERTIVENESS";" "%d", &value);
                if (mAD.mAssertivenessSliderValue == value) {
                    LOGD_IF(mDebug, "Input assertiveness is the same as current one!");
                    pthread_mutex_unlock(&mADOpLock);
                    return 0;
                }
                mAD.mAssertivenessSliderValue = value;
            } else if (!strncmp(buf, CMD_AD_STRLIMIT, strlen(CMD_AD_STRLIMIT))) {
                sscanf(buf, CMD_AD_STRLIMIT";" "%d", &value);
                if ((value >= AD_STRLIMT_MIN) && (value <= AD_STRLIMT_MAX)) {
                    if (mAD.ADGetStrLimit() == value) {
                        LOGD_IF(mDebug, "Strength limit is the same as current one!");
                        pthread_mutex_unlock(&mADOpLock);
                        return 0;
                    }
                    mAD.ADSetStrLimit(value);
                } else {
                    LOGE("AD strength limit out of range!");
                    pthread_mutex_unlock(&mADOpLock);
                    return -1;
                }
            }

            result = mAD.ADSetupMode();
            if (result)
                LOGE("Failed to set the assertiveness!");

            if (mAD.mRefresher) {
                LOGD_IF(mAD.mRefresher->mDebug, "Calling refresh()");
                mAD.mRefresher->Refresh(1, AD_REFRESH_INTERVAL);
            }

            result = mAD.ADUpdateAL(mAD.ADCurrALSValue(), AD_REFRESH_CNT);

        } else
            LOGE("AD is not ON, cannot set assertiveness or strength limit");
        pthread_mutex_unlock(&mADOpLock);
    } else {
        LOGE("Unsupported AD message.");
    }

    return result;
}

int DaemonContext::ProcessSVIMsg(char* buf){
    int32_t result = 1;

    LOGD_IF(mDebug,"PROCESS_SVI_MSG(): Command : %s", buf);
    if (!strncmp(buf, CMD_SVI_ON, strlen(CMD_SVI_ON))) {
        LOGD_IF(mDebug, "%s:  Command received %s", __FUNCTION__, buf);
        mABA->SetSVIFeature();

        pthread_mutex_lock(&mSVIOpLock);
        result = mABA->SVIControl(true);
        pthread_mutex_lock(&mCtrlLock);

        if (!result) {
            mCtrlStatus |= svi_bit;
        }

        pthread_mutex_unlock(&mCtrlLock);
        pthread_mutex_unlock(&mSVIOpLock);
    } else if (!strncmp(buf, CMD_SVI_OFF, strlen(CMD_SVI_OFF))) {
        pthread_mutex_lock(&mSVIOpLock);
        if (mABA->IsABAStatusON(ABA_FEATURE_SVI)) {
            LOGD_IF(mDebug, "%s:  Command received %s", __FUNCTION__, buf);
            result = mABA->SVIControl(false);
            pthread_mutex_lock(&mCtrlLock);
            /*Take care of return value below*/
            AbaDisable(mABA->GetHandle(),ABA_FEATURE_SVI);
            if (!result)
                mCtrlStatus &= ~svi_bit;
            pthread_mutex_unlock(&mCtrlLock);
        }
        pthread_mutex_unlock(&mSVIOpLock);
    }
    return result;
}

void DaemonContext::ProcessControlWork() {
    LOGD_IF(mDebug, "Starting control loop");
    int ret = 0, tmp_ctrl_status = 0;
    int screenOn = screenStatus;

    pthread_mutex_lock(&mCtrlLock);
    /* assume we are holding lock when loop evaluates */
    while ((mCtrlStatus & cabl_bit) || (mCtrlStatus & svi_bit) || (mCtrlStatus & ad_bit)) {
        if (mCtrlStatus != tmp_ctrl_status) {
            if (tmp_ctrl_status & cabl_bit && !(mCtrlStatus & cabl_bit)){
                if (bAbaEnabled){
                    mABA->Notify(NOTIFY_TYPE_SUSPEND);
                } else {
                    mCABL->Notify(NOTIFY_TYPE_SUSPEND);
                }
            }
            else if (!(tmp_ctrl_status & cabl_bit) && (mCtrlStatus & cabl_bit)
                                                        && screenStatus == 1) {
                if (bAbaEnabled){
                    mABA->Notify(NOTIFY_TYPE_UPDATE);
                } else {
                    mCABL->Notify(NOTIFY_TYPE_UPDATE);
                }
            }
            if (tmp_ctrl_status & svi_bit && !(mCtrlStatus & svi_bit)){
                if(bAbaEnabled)
                    mABA->Notify(NOTIFY_TYPE_SUSPEND);
            }
            else if (!(tmp_ctrl_status & svi_bit) && (mCtrlStatus & svi_bit)
                                                        && screenStatus == 1) {
                if (bAbaEnabled)
                    mABA->Notify(NOTIFY_TYPE_UPDATE);
            }
            if (tmp_ctrl_status & ad_bit && !(mCtrlStatus & ad_bit))
                mAD.Notify(NOTIFY_TYPE_SUSPEND);
            else if (!(tmp_ctrl_status & ad_bit) && (mCtrlStatus & ad_bit)
                                                        && screenOn == 1) {
                mAD.Notify(NOTIFY_TYPE_UPDATE);
            }
            tmp_ctrl_status = mCtrlStatus;
        }

        pthread_mutex_unlock(&mCtrlLock);
        uint32_t update_notify;
        if (screenStatus == 0) {
            update_notify = NOTIFY_UPDATE_START;
            ret =  ioctl(FBFd, MSMFB_NOTIFY_UPDATE, &update_notify);
            int tmp_notify = update_notify;
            if (ret != 0) {
                if (errno != ETIMEDOUT)
                    LOGE("MSMFB_NOTIFY_UPDATE start ioctl failed");
                pthread_mutex_lock(&mCtrlLock);
                continue;
            }
            if ((tmp_ctrl_status & cabl_bit) || (tmp_ctrl_status & svi_bit)){
                if(bAbaEnabled){
                    if(mABA->Notify(tmp_notify) == -1){
                        pthread_mutex_lock(&mCtrlLock);
                        continue;
                    }
                } else {
                   mCABL->Notify(tmp_notify);
                }
            }
            if (tmp_notify == NOTIFY_TYPE_UPDATE) {
                screenStatus = 1;
                screenOn = 1;
            } else if (tmp_notify == NOTIFY_TYPE_SUSPEND) {
                screenOn = 0;
            }
            if (tmp_ctrl_status & ad_bit)
                mAD.Notify(tmp_notify);
        }
        else if (screenStatus == 1) {
            update_notify = NOTIFY_UPDATE_STOP;
            ret =  ioctl(FBFd, MSMFB_NOTIFY_UPDATE, &update_notify);
            int tmp_notify = update_notify;
            if (ret != 0) {
                if (errno != ETIMEDOUT)
                    LOGE("MSMFB_NOTIFY_UPDATE stop ioctl failed");
                pthread_mutex_lock(&mCtrlLock);
                continue;
            }
            screenStatus = 0;
            if (tmp_notify == NOTIFY_TYPE_SUSPEND){
                screenOn = 0;
            }
            if (tmp_ctrl_status & cabl_bit){
                if(bAbaEnabled){
                    mABA->Notify(tmp_notify);
                } else {
                    mCABL->Notify(tmp_notify);
                }
            }
            else if (tmp_ctrl_status & svi_bit) {
                if(bAbaEnabled)
                    mABA->Notify(tmp_notify);
            }
            if (tmp_ctrl_status & ad_bit)
                mAD.Notify(tmp_notify);
        }
        pthread_mutex_lock(&mCtrlLock);
    }
    if (mCtrlStatus != tmp_ctrl_status) {
        if (tmp_ctrl_status & cabl_bit && !(mCtrlStatus & cabl_bit)){
            if (bAbaEnabled){
                mABA->Notify(NOTIFY_TYPE_SUSPEND);
            } else {
                mCABL->Notify(NOTIFY_TYPE_SUSPEND);
            }
        }
        if (tmp_ctrl_status & ad_bit && !(mCtrlStatus & ad_bit))
            mAD.Notify(NOTIFY_TYPE_SUSPEND);
        tmp_ctrl_status = mCtrlStatus;
    }
    pthread_mutex_unlock(&mCtrlLock);
    return;
}

int DaemonContext::ProcessDebugMsg(char* buf){
    LOGD("ProcessDebugMsg(): Command : %s", buf);

    if (!strncmp(buf, CMD_DEBUG_DAEMON_ON, strlen(CMD_DEBUG_DAEMON_ON))) {
        mDebug = true;
    } else if (!strncmp(buf, CMD_DEBUG_DAEMON_OFF, strlen(CMD_DEBUG_DAEMON_OFF))) {
        mDebug = false;
    } else if (!strncmp(buf, CMD_DEBUG_CABL_ON, strlen(CMD_DEBUG_CABL_ON))) {
        if(bAbaEnabled){
            mABA->SetDebugLevel(true);
        } else {
            mCABL->mDebug = true;
        }
    } else if (!strncmp(buf, CMD_DEBUG_CABL_OFF, strlen(CMD_DEBUG_CABL_OFF))) {
        if(bAbaEnabled){
            mABA->SetDebugLevel(false);
        } else {
            mCABL->mDebug = false;
        }

    } else if (!strncmp(buf, CMD_DEBUG_AD_ON, strlen(CMD_DEBUG_AD_ON))) {
        mAD.mDebug = true;
    } else if (!strncmp(buf, CMD_DEBUG_AD_OFF, strlen(CMD_DEBUG_AD_OFF))) {
        mAD.mDebug = false;
    } else if (!strncmp(buf, CMD_DEBUG_PP_ON, strlen(CMD_DEBUG_PP_ON))) {
        mPostProc.mDebug = true;
    } else if (!strncmp(buf, CMD_DEBUG_PP_OFF, strlen(CMD_DEBUG_PP_OFF))) {
        mPostProc.mDebug = false;
    } else {
        LOGE("Unsupported debug message.");
        return -1;
    }
    return 0;
}

void DaemonContext::ProcessADPollWork() {
    int32_t ret = -1, index = -1;
    char buffer[AD_FILE_LEN + 1] = {0};
    char ADFilePath[MAX_FB_NAME_LEN];
    char ad_cmd[SHORT_CMD_LEN];
    bool ad_enabled = false;
    struct pollfd ad_poll_fd;
    LOGD_IF(mDebug, "%s() Entering", __func__);

    ret = SelectFB(MDP_LOGICAL_BLOCK_DISP_2, &index);
    if (ret == 0 && index >= 0) {
        memset(buffer, 0, sizeof(buffer));
        memset(ADFilePath, 0, sizeof(ADFilePath));
        snprintf(ADFilePath, sizeof(ADFilePath), "/sys/class/graphics/fb%d/ad", index);
        LOGD_IF(mDebug, "Polling on %s", ADFilePath);
        ad_fd = open(ADFilePath, O_RDONLY);
        if (ad_fd < 0) {
            LOGE("Unable to open fd%d/ad node  err:  %s", index, strerror(errno));
            return;
        }
    }

    ad_poll_fd.fd = ad_fd; //file descriptor
    ad_poll_fd.events = POLLPRI | POLLERR; //requested events
    while (true) {
        if ((ret = poll(&ad_poll_fd, 1, -1)) < 0) {//infinite timeout
            LOGE("Error in polling AD node: %s.", strerror(errno));
            break;
        } else {
            if (ad_poll_fd.revents & POLLPRI) {
                memset(buffer, 0, AD_FILE_LEN);
                ret = pread(ad_fd, buffer, AD_FILE_LEN, 0);
                if (ret > 0) {
                    buffer[AD_FILE_LEN] = '\0';
                    ad_enabled = atoi(&buffer[0]) == 1;
                    LOGD_IF(mDebug, "Requested AD Status from AD node: %d", ad_enabled);
                    //turn on/off ad according to the value of ad_enabled
                    memset(ad_cmd, 0, sizeof(ad_cmd));
                    if (ad_enabled)
                        snprintf(ad_cmd, SHORT_CMD_LEN, "%s;%d;%d",
                                CMD_AD_ON, ad_mode_auto_str, MDP_LOGICAL_BLOCK_DISP_2);
                    else
                        snprintf(ad_cmd, SHORT_CMD_LEN, "%s", CMD_AD_OFF);
                    ret = ProcessCommand(ad_cmd, SHORT_CMD_LEN, -1);
                    if (ret)
                        LOGE("Unable to process command for AD, ret = %d", ret);

                } else if (ret == 0) {
                    LOGE_IF(mDebug, "No data to read from AD node.");
                } else {
                    LOGE("Unable to read AD node, %s", strerror(errno));
               }
            }
        }
    }

    LOGD_IF(mDebug, "Closing the ad node.");
    close(ad_fd);

    LOGD_IF(mDebug, "%s() Exiting", __func__);
    return;
}

int32_t DaemonContext::ProcessCommand(char *buf, const int32_t len, const int32_t& fd) {
    int result = 0;
    int ret = 0;

    if (NULL == buf) {
        LOGE("Command string is NULL!");
        result = -1;
        return result;
    }

    LOGD_IF(mDebug, "Command received: %s ", buf);
    if (!strncmp(buf, CMD_SET, strlen(CMD_SET))) {
        result = ProcessSetMsg(&buf[0]);
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_DCM_ON, strlen(CMD_DCM_ON))) {
        result = -1;
        if(!mDCM)
            mDCM = new DCM();
        if (mDCM) {
            result = mDCM->DCMControl(true);
        }
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_DCM_OFF, strlen(CMD_DCM_OFF))) {
        if (mDCM && (mDCM->mStatus == DCM_ON)) {
            result = mDCM->DCMControl(false);
            delete mDCM;
            mDCM = NULL;
        } else if (!mDCM) {
            LOGD("DCM is already off !");
            result = 0;
        } else {
            result = -1;
        }

        reply(!result, fd);
    } else if (!strncmp(buf, CMD_PP_ON, strlen(CMD_PP_ON))) {
        pthread_mutex_lock(&mPostProcOpLock);
        result = mPostProc.start_pp();
        pthread_mutex_unlock(&mPostProcOpLock);
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_PP_OFF, strlen(CMD_PP_OFF))) {
        pthread_mutex_lock(&mPostProcOpLock);
        mPostProc.stop_pp();
        pthread_mutex_unlock(&mPostProcOpLock);
        reply(true, fd);
    } else if (!strncmp(buf, CMD_PP_SET_HSIC, strlen(CMD_PP_SET_HSIC))) {
        int32_t hue, intensity;
        float sat, contrast;
        /* start the postproc module */
        sscanf(buf, CMD_PP_SET_HSIC "%d %f %d %f", &hue,
                &sat, &intensity, &contrast);
        pthread_mutex_lock(&mPostProcOpLock);
        mPostProc.start_pp();
        result = mPostProc.set_hsic(hue, sat, intensity, contrast);
        if (result){
            LOGE("Failed to set PA data");
            pthread_mutex_unlock(&mPostProcOpLock);
            reply(!result, fd);
            return result;
        }

        result = mPostProc.save_pa_data(hue, sat, intensity, contrast);
        if (result){
            LOGE("Failed to save PA data");
        }

        pthread_mutex_unlock(&mPostProcOpLock);
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_POSTPROC_STATUS, strlen(CMD_POSTPROC_STATUS))) {
        char buf[32];
        memset(buf, 0x00, sizeof(buf));
        if (mPostProc.mStatus == PP_ON)
            snprintf(buf, sizeof(buf), "Running\n");
        else
            snprintf(buf, sizeof(buf), "Stopped\n");
        reply(buf, fd);
    } else if (!strncmp(buf, CMD_CABL_STATUS, strlen(CMD_CABL_STATUS))) {
        char status[32];
        memset(status, 0x00, sizeof(status));
        if (bAbaEnabled){
            if (mABA && mABA->IsABAStatusON(ABA_FEATURE_CABL))
                snprintf(status, sizeof(status), "running\n");
            else
                snprintf(status, sizeof(status), "stopped\n");
        } else {
            if (mCABL && mCABL->eStatus == CABL_ON)
                snprintf(status, sizeof(status), "running\n");
            else
                snprintf(status, sizeof(status), "stopped\n");
        }
        reply(status, fd);
    } else if (!strncmp(buf, CMD_CABL_PREFIX, strlen(CMD_CABL_PREFIX))) {
        StartAlgorithmObjects();
        result = ProcessCABLMsg(&buf[0]);
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_SVI_PREFIX, strlen(CMD_SVI_PREFIX))) {
        StartAlgorithmObjects();
        if(bAbaEnabled)
            result = ProcessSVIMsg(&buf[0]);
        else
            result = -1;
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_AD_STATUS, strlen(CMD_AD_STATUS))) {
        char ad_status[32];
        memset(ad_status, 0x00, sizeof(ad_status));
        int def_status = 0, def_mode = -1;
        pthread_mutex_lock(&mADOpLock);
        def_status = mAD.ADStatus();
        if (def_status == AD_ON || def_status == AD_CALIB_ON)
            def_mode = mAD.ADMode();
        pthread_mutex_unlock(&mADOpLock);
        snprintf(ad_status, sizeof(ad_status), "%d;%d", def_status, def_mode);
        reply(ad_status, fd);
    } else if (!strncmp(buf, CMD_AD_PREFIX, strlen(CMD_AD_PREFIX))) {
        result = ProcessADMsg(&buf[0]);
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_OEM_PREFIX, strlen(CMD_OEM_PREFIX))) {
        /* pass the command to the OEM module */
        pp_oem_message_handler(buf, len, fd);
    } else if (!strncmp(buf, CMD_BL_SET, strlen(CMD_BL_SET))) {
        int32_t backlight;
        sscanf(buf, CMD_BL_SET ";" "%d", &backlight);
        if (mAD.ADStatus() == AD_CALIB_ON) {
            pthread_mutex_lock(&mADOpLock);
            result = mAD.ADSetCalibBL(backlight);
            pthread_mutex_unlock(&mADOpLock);
        } else {
            LOGE("AD calib is not on, start AD calib first!");
        }
        reply(!result, fd);
    } else if (!strncmp(buf, CMD_DEBUG_PREFIX, strlen(CMD_DEBUG_PREFIX))) {
        result = ProcessDebugMsg(&buf[0]);
        reply(!result, fd);
    } else {
        LOGE("Unknown command for pp daemon: %s", buf);
        result = -1;
    }
    ret = result;
    pthread_mutex_lock(&mCtrlLock);
    if (!result && !(mCtrlStatus & ctrl_bit) &&
        ((mCtrlStatus & cabl_bit) || (mCtrlStatus & svi_bit) || (mCtrlStatus & ad_bit))) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        ret = pthread_create(&mControlThrdId, &attr, &DaemonContext::control_thrd_func, this);
        if (ret)
            LOGE("Failed to start the control thread, status = %d", ret);
        else
            mCtrlStatus |= ctrl_bit;
        pthread_attr_destroy(&attr);
    }
    if (!result && (mCtrlStatus & ctrl_bit) &&
        !(mCtrlStatus & cabl_bit) && !(mCtrlStatus & svi_bit) && !(mCtrlStatus & ad_bit)) {
            void *term;
            pthread_mutex_unlock(&mCtrlLock);
            pthread_join(mControlThrdId, &term);
            pthread_mutex_lock(&mCtrlLock);
            LOGD_IF(mDebug, "control thread terminated");
            mCtrlStatus &= ~ctrl_bit;
    }
    pthread_mutex_unlock(&mCtrlLock);
    return ret;
}

int32_t DaemonContext::getListenFd() {
    /* trying to open a socket */
    mListenFd = android_get_control_socket(DAEMON_SOCKET);
    if (mListenFd < 0) {
        LOGE("Obtaining listener socket %s failed", DAEMON_SOCKET);
        return -1;
    }
    LOGD_IF(mDebug, "Acquired the %s socket", DAEMON_SOCKET);
    /* listen to the opened socket */
    if (listen(mListenFd, mNumConnect) < 0) {
        LOGE("Unable to listen on fd '%d' for socket %s",
                                  mListenFd, DAEMON_SOCKET);
        return -1;
    }
    return 0;
}

void DaemonContext::StartAlgorithmObjects() {

    if(mABA || mCABL)
        return;

    if (2 == display_pp_cabl_supported()){
        mABA = new AbaContext();
        mABA->initHW();
    } else if ((1 == display_pp_cabl_supported()) || (mBootStartCABL)){
        mCABL = new CABL();
        mCABL->initHW();
    }
}

void DaemonContext::StopAlgorithmObjects() {
    if ((bAbaEnabled) && (mABA)) {
        delete mABA;
    } else if (mCABL){
        delete mCABL;
    }
}

int DaemonContext::SelectFB(int display_id, int *fb_idx) {
    int ret = -1, index = -1, i = 0, j = 0;
    int fd_type[TOTAL_FB_NUM] = {-1, -1, -1};
    char fb_type[MAX_FB_NAME_LEN];
    char msmFbTypePath[MAX_FB_NAME_LEN];
    const char* logical_display_0[PRIMARY_PANEL_TYPE_CNT] = {HDMI_PANEL,
        LVDS_PANEL, MIPI_DSI_VIDEO_PANEL, MIPI_DSI_CMD_PANEL, EDP_PANEL};
    const char* logical_display_1[EXTERNAL_PANEL_TYPE_CNT] = {DTV_PANEL};
    const char* logical_display_2[WRITEBACK_PANEL_TYPE_CNT] = {WB_PANEL};

    for (i = 0; i < TOTAL_FB_NUM; i++) {
        memset(fb_type, 0, sizeof(fb_type));
        snprintf(msmFbTypePath, sizeof(msmFbTypePath),
        "/sys/class/graphics/fb%d/msm_fb_type", i);
        fd_type[i] = open(msmFbTypePath, O_RDONLY);
        if (fd_type[i] >= 0 ) {
            ret = read(fd_type[i], fb_type, sizeof(fb_type));
            if (ret == 0) {
                continue;
            } else if (ret < 0) {
                ret = errno;
                LOGE("Unable to read fb type file at fd_type[%d], err:  %s", i, strerror(errno));
                break;
            }

            if (display_id == MDP_LOGICAL_BLOCK_DISP_0) {
                for(j = 0; j < PRIMARY_PANEL_TYPE_CNT; j++) {
                    if(!strncmp(fb_type, logical_display_0[j] , strlen(logical_display_0[j]))) {
                        index = i;
                        ret = 0;
                        goto exit;
                    }
                }
            } else if (display_id == MDP_LOGICAL_BLOCK_DISP_1) {
                for(j = 0; j < EXTERNAL_PANEL_TYPE_CNT; j++) {
                    if(!strncmp(fb_type, logical_display_1[j], strlen(logical_display_1[j]))) {
                        index = i;
                        ret = 0;
                        goto exit;
                    }
                }
            } else if (display_id == MDP_LOGICAL_BLOCK_DISP_2) {
                for(j = 0; j < WRITEBACK_PANEL_TYPE_CNT; j++) {
                    if(!strncmp(fb_type, logical_display_2[j], strlen(logical_display_2[j]))) {
                        index = i;
                        ret = 0;
                        goto exit;
                    }
                }
            } else {
                LOGE("Unsupported display_id %d", display_id);
                ret = -1;
                break;
            }
        } else {
            LOGE("Unable to open fb type file  err:  %s", strerror(errno));
        }
    }

exit:
    for (i = 0; i < TOTAL_FB_NUM; i++) {
        if (fd_type[i] >= 0) {
            close(fd_type[i]);
        }
    }

    *fb_idx = index;
    return ret;
}

bool DaemonContext::IsSplitDisplay(int fb_fd) {
    struct fb_var_screeninfo info;
    struct msmfb_mdp_pp pp;

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &info) == -1) {
        LOGE("Error getting FB Info : %s", strerror(errno));
        return false;
    }

    if (info.xres > qdutils::MAX_DISPLAY_DIM && (qdutils::MDPVersion::getInstance().is8084()))
        mSplitDisplay = true;
    return mSplitDisplay;
}

void *handler_thread(void *data) {
    DaemonContext *context = (DaemonContext *)data;
    if (!context) {
        LOGE("Context object passed to thread is null");
        return NULL;
    }
    /* Make this thread detached */
    pthread_detach(pthread_self());
    if (context->mAcceptFd < 0) {
        LOGE("No valid accepted connection fd!");
    }
    int32_t acceptFd = context->mAcceptFd;
    context->mAcceptFd = -1;
    LOGD_IF(context->mDebug, "Started the handler for acceptFd %d", acceptFd);

    int32_t len = 0;
    struct pollfd read_fd;
    read_fd.fd = acceptFd;
    /*
     * Polling on read_ready event (POLLIN and POLLRDNORM). If poll() returns
     * a positive number of revents, then either the socket is ready for
     * reading, or the socket has been closed with possible error revents set
     * (POLLERR, POLLHUP, POLLNVAL).
     */
    read_fd.events = POLLIN | POLLRDNORM | POLLPRI;
    read_fd.revents = 0;

    while (!sigflag && (len = poll(&read_fd, 1, CONN_TIMEOUT)) >= 0) {
        if (len == 0) {
            LOGD_IF(context->mDebug, "poll() completed with no revents on fd %d",
                    acceptFd);
            continue;
        }
        else {
            LOGD_IF(context->mDebug, "poll completed with revents %hx on fd %d",
                    read_fd.revents, acceptFd);
            char buf[MAX_CMD_LEN];
            memset(buf, 0, sizeof(buf));
            /*
             * If read() is successful, then poll() contained revents of pending
             * read (POLLIN, POLLRDNORM, POLLPRI). If read() returns no data,
             * then the revents indicate an error has occurred or the connection
             * has been closed by the client.
             */
            int32_t ret = read (acceptFd, buf, sizeof(buf));
            if (ret == 0) {
                LOGD_IF(context->mDebug, "connection has closed on fd %d",
                        acceptFd);
                break;
            } else if (ret < 0) {
                LOGE("Unable to read the command %s", strerror(errno));
                break;
            }

            if ((context->mAD.isADEnabled() == AD_ENABLE_WB) &&
                    (!strncmp(buf, CMD_AD_PREFIX, strlen(CMD_AD_PREFIX)))) {
                LOGD_IF(context->mDebug, "AD on WB FB, disabling the AD on primary FB.");
                break;
            }

            if(context->ProcessCommand(buf, len, acceptFd)) {
                LOGE("Failed to process the command!");
            }
        }
    }

    if (len < 0) {
        LOGE("poll failed on fd %d: %s", acceptFd, strerror(errno));
    }

    close(acceptFd);
    return NULL;
}

void *listener_thread(void* data) {
    DaemonContext *context = (DaemonContext *)data;
    if (NULL == context) {
        LOGE("Context object passed to thread is null");
        return NULL;
    }
    LOGD_IF(context->mDebug, "Starting the listening thread");
    pthread_detach(pthread_self());
    /* wait for the connection */
    while (!sigflag) {
        struct sockaddr addr;
        socklen_t alen;
        alen = sizeof (addr);
        /* check for a new connection */
        int32_t acceptFd = accept(context->mListenFd,
                                            (struct sockaddr *) &addr, &alen);
        if (acceptFd > 0) {
            LOGD_IF(context->mDebug, "Accepted connection fd %d, \
                                                creating handler", acceptFd);
            /* creating the handler thread for this connection */
            pthread_t handler;
            while (context->mAcceptFd != -1) {
                LOGD("handler has not copied the data yet... wait some time");
                sleep(1);
            }
            context->mAcceptFd = acceptFd;
            if (pthread_create(&handler, NULL, handler_thread, context)) {
                LOGE("Failed to create the handler thread for Fd %d", acceptFd);
                close(acceptFd);
                break;
            }
            sched_yield();
        } else {
            LOGE("%s: Failed to accept socket connection", strerror(errno));
        }
    }
    LOGD_IF(context->mDebug, "Closing the listening socket at fd %d",
            context->mListenFd);
    close(context->mListenFd);
    return NULL;
}

int32_t DaemonContext::start() {
    /* start listening to the socket for connections*/
    int32_t err = getListenFd();
    if (err) {
        LOGE("Failed to listen to the socket");
        return err;
    }
    pthread_t thread_id;
    /* start the listener thread to handle connection requests */
    err = pthread_create(&thread_id, NULL, listener_thread, this);
    if (err) {
        LOGE("Failed to start the listener thread");
        return err;
    }

    /* start AD polling thread, which checks the content of /ad file */
    if (mAD.isADEnabled() == AD_ENABLE_WB) {
        pthread_attr_t ad_attr;
        pthread_attr_init(&ad_attr);
        LOGD_IF(mDebug, "Creating ad poll thread");
        err = pthread_create(&mADPollThrdId, &ad_attr, &ad_poll_thrd_func, this);
        if (err)
            LOGE("Failed to start the AD poll thread, err = %d", err);
        pthread_attr_destroy(&ad_attr);
    } else {
        LOGE_IF(mDebug, "AD on WB FB is not supported");
    }

    return 0;
}

int DaemonContext::ProcessSetMsg(char* buf)
{
    int ret = -1;

    if (strstr(buf, FEATURE_PCC)) {
         ret = ProcessPCCMsg(&buf[0]);
        if (ret){
            LOGE("Failed to set PCC data");
        }
    } else {
        LOGE("This message is not supported currently!");
    }
    return ret;
}

int DaemonContext::ProcessPCCMsg(char* buf)
{

    char* tokens[MIN_PCC_PARAMS_REQUIRED];
    char *temp_token = NULL;
    int ret = -1, index = 0;
    int mdp_block = 0, offset = 0;
    struct display_pp_pcc_cfg pcc_cfg;
    struct display_pcc_coeff *coeff_ptr = NULL;

    memset(tokens, 0, sizeof(tokens));
    ret = tokenize_params(buf, TOKEN_PARAMS_DELIM, MIN_PCC_PARAMS_REQUIRED, tokens, &index);
    if(ret){
        LOGE("tokenize_params failed! (Line %d)", __LINE__);
        goto err;
    }

    if (index != MIN_PCC_PARAMS_REQUIRED){
        LOGE("invalid number of params reqiuired = %d != given = %d",
                MIN_PCC_PARAMS_REQUIRED, index);
        goto err;
    }

    LOGD_IF(mDebug, "tokenize_params successful with index = %d", index);

    if (tokens[DISPLAY_INDEX]) {
        switch(atoi(tokens[DISPLAY_INDEX])) {
            case PRIMARY_DISPLAY:
                mdp_block = MDP_LOGICAL_BLOCK_DISP_0;
                break;
            case SECONDARY_DISPLAY:
                mdp_block = MDP_LOGICAL_BLOCK_DISP_1;
                break;
            case WIFI_DISPLAY:
                mdp_block = MDP_LOGICAL_BLOCK_DISP_2;
                break;
            default:
                LOGE("Display option is invalid");
                goto err;
        }
    } else {
        LOGE("Display option is not provided!");
        goto err;
    }

    pcc_cfg.ops = MDP_PP_OPS_ENABLE | MDP_PP_OPS_WRITE;
    coeff_ptr = &pcc_cfg.r;
    while(offset != (MIN_PCC_PARAMS_REQUIRED - DATA_INDEX)) {
        for(index = 0; index < PCC_COEFF_PER_COLOR; index++) {
            coeff_ptr->pcc_coeff[index] = atof(tokens[index + DATA_INDEX + offset]);
        }
        offset += PCC_COEFF_PER_COLOR;
        switch(offset){
            case PCC_COEFF_PER_COLOR:
                coeff_ptr = &pcc_cfg.g;
                break;
            case (PCC_COEFF_PER_COLOR * 2):
                coeff_ptr = &pcc_cfg.b;
                break;
            default:
                break;
        }
    }

    LOGD_IF(mDebug, "Calling user space library for PCC!!");
    ret = display_pp_pcc_set_cfg(mdp_block, &pcc_cfg);
err:
    return ret;
}

