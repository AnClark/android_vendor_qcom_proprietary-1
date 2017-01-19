/*
 * Copyright (c) 2013-2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include <mmi_window.h>
#include <mmi_module_manage.h>
#include <mmi_utils.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "mmi_lang.h"

#define LENGTH 100

#define DEFAULT_SYS_VOLTAGE_NOW  "/sys/class/power_supply/battery/voltage_now"
#define DEFAULT_SYS_HEALTH  "/sys/class/power_supply/battery/health"
#define DEFAULT_SYS_STATUS  "/sys/class/power_supply/battery/status"

static mmi_module *g_module;
static int module_ret;
static pthread_t processThreadPid;
static mmi_text *health, *status, *voltage;
static mmi_button *btn_show;
static mmi_button *led_check;

int module_main(mmi_module * mod);
void pass(void *);
void fail(void *);

void __attribute__ ((constructor)) register_module(void);

void register_module(void) {
    g_module = mmi_module::register_module("battery", module_main);
}

void signalHandler(int signal) {
    pthread_exit(NULL);
}

static int read_status(const char *filepath, char *buf, int size) {
    int fd, len;

    fd = open(filepath, O_RDONLY);
    if(fd == -1) {
        ALOGE("%s open fail\n", filepath);
        return -1;
    }

    len = read(fd, buf, size - 1);
    // Last character is a new line. We don't want it.
    buf[len - 1] = '\0';
    close(fd);
    return 0;
}

/**Test process or hardware init*/
void *processThread(void *) {
    signal(SIGUSR1, signalHandler);
    char buf[LENGTH];
    char str[256] = { 0 };
    while(1) {
        if(read_status(DEFAULT_SYS_HEALTH, buf, sizeof(buf)))
            break;
        snprintf(str, sizeof(str), "%s: %s", mmi_lang::query("battery_health"), buf);
        health->set_text(str);

        if(read_status(DEFAULT_SYS_STATUS, buf, sizeof(buf)))
            break;
        snprintf(str, sizeof(str), "%s: %s", mmi_lang::query("battery_status"), buf);
        status->set_text(str);

        if(strncmp(buf, "Charging",8) == 0) {
            led_check->set_text(mmi_lang::query("charging_indicate"));
            led_check->set_color(255, 0, 0, 255);
        } else if(strncmp(buf, "Charged",7) == 0) {
            led_check->set_text(mmi_lang::query("dischage_indicate"));
            led_check->set_color(255, 0, 0, 255);
        } else {
            led_check->set_text("");
            led_check->set_color(0, 0, 0, 1);
        }

        if(read_status(DEFAULT_SYS_VOLTAGE_NOW, buf, sizeof(buf)))
            break;
        snprintf(str, sizeof(str), "%s: %c.%c%cV", mmi_lang::query("battery_voltage"), buf[0], buf[1], buf[2]);
        voltage->set_text(str);
        invalidate();

        usleep(1000 * 1000);
    }
    return NULL;
}

int auto_test() {
    char buf[LENGTH];

    if(read_status(DEFAULT_SYS_HEALTH, buf, sizeof(buf)))
        return CASE_FAIL;
    if(read_status(DEFAULT_SYS_STATUS, buf, sizeof(buf)))
        return CASE_FAIL;
    g_module->addStringResult("State", (string) buf);
    if(read_status(DEFAULT_SYS_VOLTAGE_NOW, buf, sizeof(buf)))
        return CASE_FAIL;
    g_module->addStringResult("Voltage", (string) buf);
    return USER_PASS_PCBA_PASS;
}

void pass(void *) {
    module_ret = 0;
    pthread_kill(processThreadPid, SIGUSR1);
}

void fail(void *) {
    module_ret = -1;
    pthread_kill(processThreadPid, SIGUSR1);
}

void initUI() {
    mmi_window *window = new mmi_window();
    int width = window->get_width();
    int height = window->get_height();

    mmi_point_t text_point;

    text_point.x = width / 10;
    text_point.y = height * 3 / 20;
    health = new mmi_text(text_point, "reading ...");

    text_point.x = width / 10;
    text_point.y += height / 20;
    status = new mmi_text(text_point, "reading ...");

    text_point.x = width / 10;
    text_point.y += height / 20;
    voltage = new mmi_text(text_point, "reading ...");

    g_module->add_text(health);
    g_module->add_text(status);
    g_module->add_text(voltage);


    mmi_rect_t rect = { 10, 0, width - 10, height / 10 };
    btn_show = new mmi_button(rect, mmi_lang::query("battery_title"), NULL);
    btn_show->set_color(0, 125, 125, 255);
    g_module->add_btn(btn_show);

    rect = { 10, height / 2, width - 10, height / 10};
    led_check = new mmi_button(rect, "", NULL);
    led_check->set_color(0, 0, 0, 1);
    g_module->add_btn(led_check);

    g_module->add_btn_pass(pass);
    g_module->add_btn_fail(fail);
}

/**do some init work for data, UI but not hardware */
void init() {
    module_ret = -1;
    initUI();
    ALOGI(" init complete \n");
}

/**To clean source; diable hardware; free memory*/
void finish() {
    g_module->clean_source();
}


int module_main(mmi_module * mod) {
    g_module = mod;
    // PCBA tests first. No init needed here.
    if(mod->get_run_mode() == TEST_MODE_PCBA) {
        return auto_test();
    }

    init();

    int ret = pthread_create(&processThreadPid, NULL, processThread, NULL);

    if(ret < 0)
        ALOGE("can't create pthread: %s\n", strerror(errno));
    else
        pthread_join(processThreadPid, NULL);

    finish();
    return module_ret;
}
