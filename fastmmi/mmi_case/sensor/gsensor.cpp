/*
 * Copyright (c) 2012-2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include <stdio.h>
#include <mmi_module_manage.h>
#include "mmi_utils.h"
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include "common.h"
#include "hardware/sensors.h"

#define SENSOR_TIMEOUT 5
static const char *TAG = "GSensor";
static const int SENSOR = SENSOR_TYPE_ACCELEROMETER;
static pthread_t processThreadPid;
static int module_ret = -1;

int module_main(mmi_module * mod);
static void pass(void *btn);
static void fail(void *btn);

extern "C" void __attribute__ ((constructor)) register_module(void);
void register_module(void) {
    mmi_module::register_module(TAG, module_main);
}

static void signalHandler(int signal) {
    pthread_exit(NULL);
}

static void *processThread(void *mod) {
    signal(SIGUSR1, signalHandler);
    while(1) {
        module_ret = testThisSensor(SENSOR, (mmi_module *) mod);
    }
    return &module_ret;
}

static void pass(void *btn) {
    module_ret = 0;
    pthread_kill(processThreadPid, SIGUSR1);
}

static void fail(void *btn) {
    module_ret = -1;
    mmi_module *mod = ((mmi_button *) btn)->get_launch_module();

    ALOGI("fail to test: %s \n", mod->get_domain());
    pthread_kill(processThreadPid, SIGUSR1);
}

static void init_ui(mmi_module * mod) {
    mmi_point_t point = { gr_fb_width() / 4, gr_fb_height() / 3 };
    mmi_text *text = new mmi_text(point, "Initializing...");

    mod->add_text(text);
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
    if(mod->get_run_mode() == TEST_MODE_UI) {
        init_ui(mod);
    }

    res = initSensor(SENSOR);
    if(res) {
        return PCBA_SENSOR_READ_FAIL;
    }
    return res;
}

static void deinit(mmi_module * mod) {
    if(mod->get_run_mode() == TEST_MODE_UI) {
        clean_ui(mod);
    }
    deinitSensor(SENSOR);
}

static void auto_test(mmi_module * mod) {

    struct timespec ts;

    if(clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        ALOGE("can't clock_gettime: %s\n", strerror(errno));
        return;
    }

    ts.tv_sec += SENSOR_TIMEOUT;
    if(mod->mod_sem_timewait(&ts)) {
        pthread_kill(processThreadPid, SIGUSR1);
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
    res = pthread_create(&processThreadPid, NULL, processThread, (void *) mod);
    if(res < 0) {
        ALOGE("can't create pthread: %s\n", strerror(errno));
    } else {
        if(mod->get_run_mode() == TEST_MODE_PCBA) {
            auto_test(mod);
        }
        pthread_join(processThreadPid, NULL);
    }

    deinit(mod);
    return module_ret;
}
