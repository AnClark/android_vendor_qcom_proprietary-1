/*
 * Copyright (c) 2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include <stdio.h>
#include <mmi_module_manage.h>
#include "mmi_utils.h"
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include "diagcmd.h"
#include "diag_nv.h"

static const char *TAG = "Sysinfo";
static int module_ret = -1;
struct device_info_t {
    unsigned char modem_version[256];
    unsigned char imei_code[256];
    unsigned char oem_item[8][256];
};
static struct device_info_t device_info;
static sem_t g_sem;

int module_main(mmi_module * mod);
static void pass(void *btn);
static void fail(void *btn);

extern "C" void __attribute__ ((constructor)) register_module(void);
void register_module(void) {
    mmi_module::register_module(TAG, module_main);
}

static void pass(void *btn) {
    module_ret = 0;
    sem_post(&g_sem);
}

static void fail(void *btn) {
    module_ret = -1;
    sem_post(&g_sem);
}

void initial_info() {
    memset(&device_info, 0, sizeof(struct device_info_t));
    diag_send_cmd(DIAG_EXT_BUILD_ID_F, device_info.modem_version, sizeof(device_info.modem_version));
    diag_nv_read(NV_UE_IMEI_I, device_info.imei_code, sizeof(device_info.imei_code));
    diag_nv_read(NV_OEM_ITEM_1_I, device_info.oem_item[0], sizeof(device_info.oem_item[0]));
    diag_nv_read(NV_OEM_ITEM_2_I, device_info.oem_item[1], sizeof(device_info.oem_item[1]));
    diag_nv_read(NV_OEM_ITEM_3_I, device_info.oem_item[2], sizeof(device_info.oem_item[2]));
}

void display_info(mmi_module * mod) {
    char temp[256] = { 0 };
    unsigned char (*p)[256] = device_info.oem_item;
    diag_ext_build_id_rsp_type *p_modem_v;
    mmi_window *window = new mmi_window();
    int w = window->get_width();
    int h = window->get_height();
    int last_text_y = h / 10;
    int last_text_x = w / 10;
    mmi_point_t point;

    point.x = last_text_x;
    point.y = last_text_y;

    p_modem_v = (diag_ext_build_id_rsp_type *) device_info.modem_version;
    ALOGI("Version:%x", device_info.modem_version[0]);

    mmi_text *text = new mmi_text(point, p_modem_v->ver_strings);

    mod->add_text(text);

    point.y += 25;
    charArray_to_hexArray((char *) device_info.imei_code, sizeof(device_info.imei_code), 5, 8, (char *) temp,
                          sizeof(temp), true);
    ALOGI("UE IMEI:%s", temp);
    text = new mmi_text(point, temp);
    mod->add_text(text);

    point.y += 25;
    memset(&temp, 0, sizeof(temp));
    strlcpy(temp, "OEM ITEM1:", 11);
    strlcat(temp, (char *) (p[0] + 3), sizeof(temp));
    ALOGI("UE oem item 0::%s", temp);
    text = new mmi_text(point, temp);
    mod->add_text(text);

    point.y += 25;
    temp[0] = '\0';
    strlcpy(temp, "OEM ITEM2:", 11);
    strlcat(temp, (char *) (p[1] + 3), sizeof(temp));
    ALOGI("UE oem item 1::%s", temp);
    text = new mmi_text(point, temp);
    mod->add_text(text);

    point.y += 25;
    temp[0] = '\0';
    strlcpy(temp, "OEM ITEM3:", 11);
    strlcat(temp, (char *) (p[2] + 3), sizeof(temp));
    ALOGI("UE oem item 2::%s", temp);
    text = new mmi_text(point, temp);
    mod->add_text(text);
    invalidate();
}

static void init_ui(mmi_module * mod) {
    display_info(mod);
    mod->add_btn_pass(pass);
    mod->add_btn_fail(fail);
    invalidate();
}

static void clean_ui(mmi_module * mod) {
    mod->clean_source();
}

static int init(mmi_module * mod) {
    int res = 0;

    module_ret = -1;
    sem_init(&g_sem, 0, 0);
    initial_info();
    if(mod->get_run_mode() == TEST_MODE_UI) {
        init_ui(mod);
    }

    return res;
}

static void deinit(mmi_module * mod) {
    sem_wait(&g_sem);
    if(mod->get_run_mode() == TEST_MODE_UI) {
        clean_ui(mod);
    }
}

int module_main(mmi_module * mod) {
    int res = CASE_SUCCESS;

    if(mod == NULL) {
        ALOGE("Invalid param,NULL module\n");
        return CASE_FAIL;
    }

    if((res = init(mod)) != 0) {
        ALOGE("module init fail.\n");
        return res;
    }

    deinit(mod);
    return module_ret;
}
