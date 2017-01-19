/*
 * Copyright (c) 2013-2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <mmi_window.h>
#include <mmi_module_manage.h>
#include <mmi_config.h>
#include <semaphore.h>
#include "mmi_utils.h"
#include <sys/statfs.h>
#include "mmi_lang.h"
#include "mmi_state.h"


#define DEFAULT_SDCARD_DEV_PATH  "/dev/block/mmcblk1"
#define DEFAULT_MOUNT_SDCARD_CMD "mount -t vfat /dev/block/mmcblk1p1 /mnt"
#define DEFAULT_UMOUNT_SDCARD_CMD "umount /mnt"

struct input_params {
    char device[256];
};

struct output_storage_params {
    long long sdcard_capacity;
    long long sdcard_used;
    long long sdcard_free;
};

static struct input_params input;
static struct output_storage_params output;

static hash_map < string, string > paras;
static mmi_module *g_module;
static sem_t g_sem;
static int module_ret;

int module_main(mmi_module * mod);
void __attribute__ ((constructor)) register_module(void);

void pass(void *) {
    module_ret = 0;
    sem_post(&g_sem);
}

void fail(void *) {
    module_ret = -1;
    sem_post(&g_sem);
}

void register_module(void) {
    g_module = mmi_module::register_module(LOG_TAG, module_main);
}
void get_input() {
    char temp[256] = { 0 };
    memset(&input, 0, sizeof(struct input_params));
    parse_parameter(mmi_config::query_config_value(g_module->get_domain(), "parameter"), paras);
    get_para_value(paras, "device", temp, sizeof(temp), DEFAULT_SDCARD_DEV_PATH);
    strlcpy(input.device, temp, sizeof(input.device));
    ALOGI("sd config: path=%s\n", input.device);
}

bool is_sdcard_exist() {
    bool ret = false;
    int fd = open(input.device, O_RDWR);

    if(fd >= 0) {
        ret = true;
        close(fd);
    }
    return ret;
}

void mount_sdcard(void) {
    system(DEFAULT_MOUNT_SDCARD_CMD);
}

void umount_sdcard(void) {
    system(DEFAULT_UMOUNT_SDCARD_CMD);
}

void draw_this_text(char *mText, int startX, int startY) {
    mmi_text *text;
    mmi_point_t point = { startX, startY };
    text = new mmi_text(point, mText);
    g_module->add_text(text);
}

void calc_sdcard_capacity(void) {
    struct statfs st;

    if(statfs("/mnt", &st) < 0) {
        ALOGE("Fail to calculate the capacity of sdcard\n");
    } else {
        output.sdcard_capacity = (long long) st.f_blocks * (long long) st.f_bsize;
        output.sdcard_free = (long long) st.f_bfree * (long long) st.f_bsize;
        output.sdcard_used = output.sdcard_capacity - output.sdcard_free;
    }
}

void display_info(int w, int h) {
    char temp[256] = { 0 };

    snprintf(temp, sizeof(temp), "%s = %lld", mmi_lang::query("sdcard_capacity"), output.sdcard_capacity);
    draw_this_text(temp, 10, 7 * h / 20);
    snprintf(temp, sizeof(temp), "%s = %lld", mmi_lang::query("sdcard_used_space"), output.sdcard_used);
    draw_this_text(temp, 10, 8 * h / 20);
    snprintf(temp, sizeof(temp), "%s = %lld", mmi_lang::query("sdcard_free_space"), output.sdcard_free);
    draw_this_text(temp, 10, 9 * h / 20);
}

int manual_test() {
    sem_init(&g_sem, 0, 0);
    mmi_window *window = new mmi_window();
    int width = window->get_width();
    int height = window->get_height();

    g_module->add_window(window);
    mmi_rect_t rect = { 10, height / 6, width - 20, height / 6 };
    mmi_button *btn = new mmi_button(rect, "start to detect sd card", NULL);

    if(is_sdcard_exist()) {
        btn->set_color(0, 125, 125, 255);
        btn->set_text(mmi_lang::query("sdcard_present"));
        mount_sdcard();
        calc_sdcard_capacity();
        umount_sdcard();
    } else {
        btn->set_color(125, 125, 0, 255);
        btn->set_text(mmi_lang::query("sdcard_absent"));
    }

    display_info(width, height);
    g_module->add_btn(btn);
    g_module->add_btn_pass(pass);
    g_module->add_btn_fail(fail);
    invalidate();
    sem_wait(&g_sem);
    g_module->clean_source();
    return module_ret;
}

int module_main(mmi_module * mod) {
    get_input();
    case_run_mode_t mode = g_module->get_run_mode();

    g_module = mod;

    if(mode == TEST_MODE_PCBA) {
        if(is_sdcard_exist()) {
            g_module->addStringResult("DETECTED", "yes");
            return CASE_SUCCESS;
        } else {
            g_module->addStringResult("DETECTED", "no");
            return PCBA_SD_CARD_READ_FAIL;
        }
    } else if(mode == TEST_MODE_SANITY) {
        return CASE_FAIL;
    } else {
        return manual_test();
    }
}
