/*
 * DESCRIPTION
 * This file holds utility functions as a PPDaemon library. Any class
 * can include this file for common interaction with pp-daemon.
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 */

#include "PPDaemonUtils.h"

display_hw_info gHwInfo;
bool gMDP5;
int gMDPVersion;

void inspectHW(){
    gHwInfo.nPriPanelMode =  qdutils::MDPVersion::getInstance().getPanelType();
    gMDPVersion = qdutils::MDPVersion::getInstance().getMDPVersion();
    switch(gMDPVersion) {
        case qdutils::MDP_V3_0:
        case qdutils::MDP_V3_0_3:
        case qdutils::MDP_V3_0_4:
        case qdutils::MDP_V3_1:
        case qdutils::MDP_V4_0:
        case qdutils::MDP_V4_1:
            gHwInfo.nPriDisplayHistBins = 32;
            gHwInfo.nPriDisplayHistComp = 3;
            gHwInfo.nPriDisplayHistBlock = MDP_BLOCK_DMA_P;
            break;
        case qdutils::MDP_V4_2:
        case qdutils::MDP_V4_3:
        case qdutils::MDP_V4_4:
            gHwInfo.nPriDisplayHistBins = 128;
            gHwInfo.nPriDisplayHistComp = 3;
            gHwInfo.nPriDisplayHistBlock = MDP_BLOCK_DMA_P;
            break;
        case qdutils::MDSS_V5:
            gHwInfo.nPriDisplayHistBins = 256;
            gHwInfo.nPriDisplayHistComp = 1;
            gHwInfo.nPriDisplayHistBlock = MDP_LOGICAL_BLOCK_DISP_0;
            gMDP5 = true;
            break;
        case qdutils::MDP_V_UNKNOWN:
        case qdutils::MDP_V2_2:
        default:
            gHwInfo.nPriDisplayHistBins = 0;
            gHwInfo.nPriDisplayHistComp = 0;
            gHwInfo.nPriDisplayHistBlock = 0;
            break;
    }
}

bool isHDMIPrimary (void) {

    char isPrimaryHDMI = '0';
    /* read HDMI sysfs nodes */
    FILE *fp = fopen(HDMI_PRIMARY_NODE, "r");

    if (fp) {
        fread(&isPrimaryHDMI, 1, 1, fp);
        if (isPrimaryHDMI == '1'){
            /* HDMI is primary */
            LOGD("%s: HDMI is primary display", __FUNCTION__);
            fclose(fp);
            return true;
        } else {
            /* Should never happen */
            LOGE("%s: HDMI_PRIMARY_NODE is: %c", __FUNCTION__, isPrimaryHDMI);
            fclose(fp);
            return false;
        }
    } else {
        /* HDMI_PRIMARY_NODE not present */
        LOGD("%s: HDMI is not primary display", __FUNCTION__);
        return false;
    }
}


void free_cmap(struct fb_cmap *cmap)
{

    if(cmap == NULL){
        LOGE("%s: Colormap struct NULL", __FUNCTION__);
        return;
    }

    if (cmap->red)
        free(cmap->red);
    if (cmap->green)
        free(cmap->green);
    if (cmap->blue)
        free(cmap->blue);
}

void print_hist(struct mdp_histogram_data *hist)
{
    uint32_t *r, *b, *g, i;

    char strbuf[MAX_DBG_MSG_LEN]="";
    size_t strbuf_len = 0;

    if(hist == NULL){
        LOGE("%s: Histogram data NULL", __FUNCTION__);
        return;
    }

    r = hist->c0;
    g = hist->c1;
    b = hist->c2;

    snprintf(strbuf, MAX_DBG_MSG_LEN * sizeof(char), "%s", "hist all (R,G,B)\n");
    for (i = 0; i < hist->bin_cnt; i++) {
        strbuf_len = strlen(strbuf);
        snprintf(strbuf+strbuf_len,(MAX_DBG_MSG_LEN - strbuf_len) * sizeof(char),
                          "%d %d %d,", *r, *g, *b);
        r++;
        g++;
        b++;
    }  //end for loop

}

int32_t stopHistogram() {
    int32_t ret;
    uint32_t block = gHwInfo.nPriDisplayHistBlock;
    ret = ioctl(FBFd, MSMFB_HISTOGRAM_STOP, &block);
    if (ret < 0) {
        LOGE("MSMFB_HISTOGRAM_STOP failed!");
    }
    return ret;
}

int32_t startHistogram() {
    int32_t ret;
    struct mdp_histogram_start_req req;

    req.block = gHwInfo.nPriDisplayHistBlock;
    req.frame_cnt = 1;
    req.bit_mask = 0x0;
    req.num_bins = gHwInfo.nPriDisplayHistBins;

    ret = ioctl(FBFd, MSMFB_HISTOGRAM_START, &req);
    if (ret < 0) {
        LOGE("MSMFB_HISTOGRAM_START failed!");
    }
    return ret;
}

int set_backlight_scale(uint32_t bl_scale, uint32_t bl_min_level) {
    int32_t ret;
    struct msmfb_mdp_pp backlight;

    backlight.op = mdp_bl_scale_cfg;
    backlight.data.bl_scale_data.min_lvl = bl_min_level;
    if (bl_scale > BL_SCALE_MAX)
        bl_scale = BL_SCALE_MAX;
    backlight.data.bl_scale_data.scale = bl_scale;
    ret = ioctl(FBFd, MSMFB_MDP_PP, &backlight);
    if (ret)
        LOGE("FAILED TO SET BACKLIGHT SCALE, %s", strerror(errno));

    return ret;
}

uint32_t get_backlight_level(void) {
    uint32_t level = 0;
    ssize_t bytes;
    char buffer[MAX_BACKLIGHT_LEN];
    memset(buffer, 0, MAX_BACKLIGHT_LEN);
    bytes = pread(BLFd, buffer, sizeof (char) * MAX_BACKLIGHT_LEN, 0);
    if (bytes > 0)
        sscanf(buffer, "%u", &level);
    else
        LOGE("BL FD read failure: bytes = %d error = %s ", bytes,
            strerror(errno));
    return level;
}

int ql_string2int(char* c_lvl){
    int ret = -1;
    if (!strcmp(c_lvl, CABL_LVL_LOW)) {
        ret = ABL_QUALITY_LOW;
    } else if (!strcmp(c_lvl, CABL_LVL_MEDIUM)) {
        ret = ABL_QUALITY_NORMAL;
    } else if (!strcmp(c_lvl, CABL_LVL_HIGH)) {
        ret = ABL_QUALITY_HIGH;
    } else if (!strcmp(c_lvl, CABL_LVL_AUTO)) {
        ret = ABL_QUALITY_AUTO;
    }
    return ret;
}

int tokenize_params(char *inputParams, const char *delim, const int minTokenReq,
                                        char* tokenStr[], int *idx){
    char *tmp_token = NULL, *inputParams_r;
    int ret = 0, index = 0;
    if (!inputParams) {
        ret = -1;
        goto err;
    }

    tmp_token = strtok_r(inputParams, delim, &inputParams_r);
    while (tmp_token != NULL) {
        tokenStr[index++] = tmp_token;
        if (index < minTokenReq) {
            tmp_token = strtok_r(NULL, delim, &inputParams_r);
        }else{
            break;
        }
    }
    *idx = index;
err:
    return ret;
}

