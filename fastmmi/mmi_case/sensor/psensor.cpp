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
#include "mmi_config.h"

#include "calibration.h"
#define SENSOR_TIMEOUT 5
#define DEFAULT_CALIBRATION_STATUS "off"

struct input_params {
    char calibration[16];
};

static const char *TAG = "PSensor";
static const int SENSOR = SENSOR_TYPE_PROXIMITY;
static pthread_t processThreadPid;
static int module_ret = -1;
static hash_map < string, string > paras;
static struct input_params input;

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

static int do_calibration() {
    int ret = 0;

    ret = deinitSensor(SENSOR);
    if(ret == 0) {
        usleep(500 * 1000);     /*Waiting for adsp deactive the psensor */
        ret = psensor_calibration();
        if(ret != 0) {
            ALOGE("Fail to do_calibration");
            return ret;
        }
    } else {
        ALOGE("Fail to deactive");
        return ret;
    }
    ret = initSensor(SENSOR);
    if(ret == 0)
        usleep(500 * 1000);     /*Waiting for adsp active the psensor */

    return ret;
}

static void calibration(void *btn) {
    int ret = 0;
    mmi_button *pbtn = (mmi_button *) btn;
    mmi_module *mod = pbtn->get_launch_module();

    pbtn->set_text("Do Calibration ...");
    invalidate();
    ret = do_calibration();
    if(ret == 0) {
        pbtn->set_text("Calibration success");
        mod->addStringResult("Calibration", "success");
    } else {
        pbtn->set_text("Calibration fail");
        mod->addStringResult("Calibration", "failure");
    }
    invalidate();
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
    pthread_kill(processThreadPid, SIGUSR1);
}

static void get_input(mmi_module * mod, input_params * params) {
    if(params != NULL) {
        parse_parameter(mmi_config::query_config_value(mod->get_domain(), "parameter"), paras);
        get_para_value(paras, "calibration", params->calibration, sizeof(params->calibration),
                       DEFAULT_CALIBRATION_STATUS);
        ALOGI("Do alibration mode: %s \n", params->calibration);
    }
}

static int init_ui(mmi_module * mod, input_params * params) {
    int ret = 0;

    mmi_point_t point = { gr_fb_width() / 4, gr_fb_height() / 3 };
    mmi_text *text = new mmi_text(point, "Initializing...");

    mod->add_text(text);
    mod->add_btn_pass(pass);
    mod->add_btn_fail(fail);
    invalidate();

    if(params != NULL) {
        if(!strncmp(params->calibration, "on", 2)) {
            mmi_rect_t rect = { 10, gr_fb_height() * 3 / 5, gr_fb_width() - 20, gr_fb_height() / 5 - 60 };
            mmi_button *btn = new mmi_button(rect, "Do Calibration", calibration);

            btn->set_launch_module(mod);
            btn->set_color(0, 125, 125, 255);
            mod->add_btn(btn);

            ret = do_calibration();
            if(ret == 0) {
                btn->set_text("Calibration success");
                mod->addStringResult("Calibration", "success");
            } else {
                btn->set_text("Calibration fail");
                mod->addStringResult("Calibration", "failure");
            }
            invalidate();
        }
    }
    return ret;
}

static void clean_ui(mmi_module * mod) {
    mod->clean_source();
}

static int init(mmi_module * mod) {
    int res = 0;
    struct input_params *parameters = NULL;

    module_ret = -1;

    res = initSensor(SENSOR);
    if(res) {
        return PCBA_SENSOR_READ_FAIL;
    }

    get_input(mod, &input);
    if(mod->get_run_mode() == TEST_MODE_UI) {
        res = init_ui(mod, &input);
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
    int ret;
    struct timespec ts;

    if(!strncmp(input.calibration, "on", 2)) {

        ret = do_calibration();
        if(ret == 0) {
            mod->addStringResult("Calibration", "success");
        } else {
            mod->addStringResult("Calibration", "failure");
        }
    }
    ALOGI("%s: calibration result:%d \n", __FUNCTION__, ret);

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
