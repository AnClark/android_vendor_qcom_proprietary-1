/*
 * DESCRIPTION
 * This file contains functions for accessing some of the postproc features
 * such as Picture Adjustment. It communicates with pp-daemon for receiving
 * and sending commands.
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 */

#include "PostProc.h"

int PostProc::write_hsic_values() {
    int ret = FAILED;

    struct compute_params op_params;
    struct mdp_overlay_pp_params overlay_pp_params;
    struct msmfb_mdp_pp pp;

    op_params.operation = PP_OP_PA;
    op_params.params.pa_params = mNewCfg.pa_cfg;
    ret = display_pp_compute_params(&op_params, &overlay_pp_params);

    pp.op = mdp_op_pa_cfg;
    pp.data.pa_cfg_data.block = mBlockType;
    pp.data.pa_cfg_data.pa_data = overlay_pp_params.pa_cfg;

    ret = ioctl(FBFd, MSMFB_MDP_PP, &pp);
    if (ret) {
        LOGE_IF(mDebug, "%s calling ioctl failed", __func__);
    }
    return ret;
}

void PostProc::init_cc_matrix()
{
    /* Identity matrix for the CC calculation */
    mNewCfg.conv_cfg.cc_matrix[0][0]=1;
    mNewCfg.conv_cfg.cc_matrix[0][1]=0;
    mNewCfg.conv_cfg.cc_matrix[0][2]=0;
    mNewCfg.conv_cfg.cc_matrix[0][3]=0;

    mNewCfg.conv_cfg.cc_matrix[1][0]=0;
    mNewCfg.conv_cfg.cc_matrix[1][1]=1;
    mNewCfg.conv_cfg.cc_matrix[1][2]=0;
    mNewCfg.conv_cfg.cc_matrix[1][3]=0;

    mNewCfg.conv_cfg.cc_matrix[2][0]=0;
    mNewCfg.conv_cfg.cc_matrix[2][1]=0;
    mNewCfg.conv_cfg.cc_matrix[2][2]=1;
    mNewCfg.conv_cfg.cc_matrix[2][3]=0;

    mNewCfg.conv_cfg.cc_matrix[3][0]=0;
    mNewCfg.conv_cfg.cc_matrix[3][1]=0;
    mNewCfg.conv_cfg.cc_matrix[3][2]=0;
    mNewCfg.conv_cfg.cc_matrix[3][3]=1;

}

int PostProc::get_saved_hsic_config() {
    char property[PROPERTY_VALUE_MAX];
    int ret = FAILED;
    int32_t hue = 0, intensity = 0;
    float sat = 0, contrast = 0;

    ret = parse_pa_data(&hue, &sat, &intensity, &contrast);
    if (ret)
        return ret;

    if (gMDP5) {
        mNewCfg.pa_cfg.hue = hue;
        mNewCfg.pa_cfg.sat = sat;
        mNewCfg.pa_cfg.intensity = intensity;
        mNewCfg.pa_cfg.contrast = contrast;

        mNewCfg.pa_cfg.ops = MDP_PP_OPS_ENABLE | MDP_PP_OPS_WRITE;
    } else {
        mNewCfg.conv_cfg.hue = hue;
        mNewCfg.conv_cfg.sat = sat;
        mNewCfg.conv_cfg.intensity = intensity;
        mNewCfg.conv_cfg.contrast = contrast;

        mNewCfg.conv_cfg.ops = 1;
    }
    return 0;
}

void PostProc::copy_config() {
    memcpy(&mCurCfg, &mNewCfg, sizeof(display_pp_cfg));
}

int32_t PostProc::compare_config() {
    return memcmp(&mNewCfg, &mCurCfg, sizeof(display_pp_cfg));
}

void PostProc::print_values(const char *str, display_pp_cfg *tmp) {
    if (!tmp || !str) {
        LOGE("NULL pointer");
        return;
    }
    LOGD("----- %s -----", str);
    if (gMDP5) {
        LOGD("\t Hue: %f", tmp->pa_cfg.hue);
        LOGD("\t Saturation: %f", tmp->pa_cfg.sat);
        LOGD("\t Intensity: %d", tmp->pa_cfg.intensity);
        LOGD("\t Contrast: %f", tmp->pa_cfg.contrast);
        LOGD("\t Options: 0x%08x", tmp->pa_cfg.ops);
    } else {
        LOGD("\t Hue: %d", tmp->conv_cfg.hue);
        LOGD("\t Saturation: %f", tmp->conv_cfg.sat);
        LOGD("\t Intensity: %d", tmp->conv_cfg.intensity);
        LOGD("\t Contrast: %f", tmp->conv_cfg.contrast);
        LOGD("\t Options: 0x%08x", tmp->conv_cfg.ops);
    }
}

int32_t PostProc::start_pp() {
    int ret = FAILED;

    ret = display_pp_init();
    if(ret != SUCCESS){
        LOGE("Failed to initialize fb");
        return ret;
    }

    if (!gMDP5) {
        init_cc_matrix();
        ret = display_pp_conv_init(mBlockType, NULL);
        if (ret) {
            LOGE("Failed to initialize pp");
            return ret;
        }
    }
    /* set status to on */
    mStatus = PP_ON;
    return 0;
}

int32_t PostProc::set_hsic(int32_t hue, float sat,
                                  int32_t intensity, float contrast) {
    if (gMDP5) {
        mNewCfg.pa_cfg.hue = hue;
        mNewCfg.pa_cfg.sat = sat;
        mNewCfg.pa_cfg.intensity = intensity;
        mNewCfg.pa_cfg.contrast = contrast;

        mNewCfg.pa_cfg.ops = MDP_PP_OPS_ENABLE | MDP_PP_OPS_WRITE;
    } else {
        mNewCfg.conv_cfg.hue = hue;
        mNewCfg.conv_cfg.sat = sat;
        mNewCfg.conv_cfg.intensity = intensity;
        mNewCfg.conv_cfg.contrast = contrast;
    }

    LOGD_IF(mDebug, "New HSIC Values: %d %.3f %d %.3f", hue,
                            sat, intensity, contrast);
    if (compare_config()) {
        LOGD_IF(mDebug, "HSIC Values have changed, applying new values");
        copy_config();
        int32_t err;
        if (gMDP5)
            err = write_hsic_values();
        else
            err = display_pp_conv_set_cfg(mBlockType, &(mNewCfg.conv_cfg));

        if(err) {
            LOGD_IF(mDebug, "Failed to set hsic values");
            return err;
        }
    }
    return 0;
}

int32_t PostProc::stop_pp() {
    int32_t err = FAILED;
    mStatus = PP_OFF;

    err = display_pp_exit();
    if (err) {
        LOGE_IF(mDebug, "Failed to close fb");
        return err;
    }
    return 0;
}

int PostProc::save_pa_data(int hue, float sat, int intensity, float contrast)
{

    int ret = FAILED, i = 0;
    FILE *fp = NULL;
    char pa_string[128] = {'\0'};
    char pp_cfg_file[256] = {'\0'};
    if (property_get(PP_CFG_FILE_PROP, pp_cfg_file, NULL) <= 0) {
        LOGE_IF(mDebug, "Posproc file property is not set");
        return ret;
    }

    fp = fopen(pp_cfg_file, "w+");
    if (!fp) {
        LOGD_IF(mDebug, "Postproc data file open failed");
        return ret;
    }
    fputs("=PA\n", fp);
    snprintf(pa_string, sizeof(pa_string), "%d %f %d %f\n", hue, sat, intensity, contrast);
    fputs(pa_string, fp);
    fclose(fp);
    return 0;
}

int PostProc::processPPDataFile()
{
    int ret = FAILED;
    ret = get_saved_hsic_config();
    if (ret) {
        LOGE_IF(mDebug, "Failed to get the saved hsic config");
        return ret;
    }

    ret = start_pp();
    if (ret) {
        LOGE_IF(mDebug, "Failed to start pp");
        return ret;
    }

    if (gMDP5)
        ret = set_hsic(mNewCfg.pa_cfg.hue, mNewCfg.pa_cfg.sat,
                    mNewCfg.pa_cfg.intensity, mNewCfg.pa_cfg.contrast);
    else
        ret = set_hsic(mNewCfg.conv_cfg.hue, mNewCfg.conv_cfg.sat,
                    mNewCfg.conv_cfg.intensity, mNewCfg.conv_cfg.contrast);

    if (ret) {
        LOGD_IF(mDebug, "Failed to set the hsic values");
        return ret;
    }

    ret = stop_pp();
    if (ret) {
        LOGE_IF(mDebug, "Failed to stop pp");
        return ret;
    }
    return 0;
}

int PostProc::parse_pa_data(int* hue, float* sat, int* intensity, float* contrast)
{
    int ret = FAILED, cnt = 0;
    char *line = NULL;
    FILE* fp;
    char pp_cfg_file[256] = {'\0'};
    if (property_get(PP_CFG_FILE_PROP, pp_cfg_file, NULL) > 0) {
        if ( -1 == access(&pp_cfg_file[0], R_OK|F_OK)) {
            LOGD_IF(mDebug, "No permission to access postproc data file \
            or file does not exists! Using default settings!!");
            return 0;
        }
    } else {
        if (property_set(PP_CFG_FILE_PROP, PP_CFG_FILE_PATH)) {
            LOGE_IF(mDebug, "Failed to set the pp cfg file property");
            return -1;
        }
        ret = strlcpy(pp_cfg_file, PP_CFG_FILE_PATH, sizeof(pp_cfg_file));
        if (ret >= sizeof(pp_cfg_file)) {
            LOGD_IF(mDebug, "PP file path & name too long to fit in.");
            return -1;
        }
    }

    fp = fopen(pp_cfg_file, "r");
    if (!fp) {
        LOGE_IF(mDebug, "Failed to open file %s",pp_cfg_file);
        return ret;
    }

    line = (char *)malloc(MAX_CMD_LEN * sizeof(char));
    if (!line) {
        LOGE("Cannot allocate memory");
        goto err;
    }

    while(fgets(line, MAX_CMD_LEN * sizeof(char), fp)) {
        if (line[0] == '\r' || line[0] == '\n' || line[0] == '#')
            continue;
        if (!strncmp(line, "=PA", strlen("=PA"))) {
            while(cnt < 1 && fgets(line, MAX_CMD_LEN * sizeof(char), fp)) {
                if (line[0] == '\r' || line[0] == '\n' || line[0] == '#')
                    continue;
                sscanf(line, "%d %f %d %f", hue,
                        sat, intensity, contrast);

                LOGD_IF(mDebug, "HSIC = %d %f %d %f", *hue, *sat, *intensity, *contrast);
                ++cnt;
            }
            LOGD_IF(mDebug, "Finish parsing PA data");
        }
    }
    ret = 0;
    free(line);
err:
    fclose(fp);
    return ret;
}
