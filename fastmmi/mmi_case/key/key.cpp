/*
 * Copyright (c) 2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include <stdio.h>
#include <unistd.h>
#include <mmi_window.h>
#include <mmi_module_manage.h>
#include <semaphore.h>
#include <mmi_key.h>

#include "mmi_utils.h"
#include "mmi_config.h"
#include <hash_map>
#include <pthread.h>
#include <errno.h>
#include "mmi_lang.h"

#define HW_KEYS_TIMEOUT 6

static const char *TAG = "KEY";
static hash_map < string, string > paras;
static mmi_module *g_module;
static int g_retval;
static sem_t g_sem;

int keys_pressed;
int timeout;

enum {
    MENU = 0,
    HOME,
    BACK,
    SEARCH,
    VOLUME_UP,
    VOLUME_DOWN,
    CAMERA_SNAPSHOT,
    MAX_KEY_NUM
};

const char *panel_btn_name[] = { "menu", "home", "back", "search", "volumeup", "volumedown", "camsnapshot" };

static mmi_window *window;
static class mmi_button *btn_list[MAX_KEY_NUM];
static mmi_key_cb_t btn_cb_list[MAX_KEY_NUM];

int module_main(mmi_module * mod);
void __attribute__ ((constructor)) register_module(void);
void register_module(void) {
    g_module = mmi_module::register_module(TAG, module_main);
}

void pass(void *);
void fail(void *);

static int key_to_index(int key) {

    int ret = -1;

    switch (key) {
    case KEY_BACK:
        ret = BACK;
        break;

    case KEY_HOME:
    case KEY_HOMEPAGE:
        ret = HOME;
        break;

    case KEY_MENU:
        ret = MENU;
        break;

    case KEY_VOLUMEUP:
        ret = VOLUME_UP;
        break;

    case KEY_VOLUMEDOWN:
        ret = VOLUME_DOWN;
        break;

    case KEY_CAMERA_SNAPSHOT:
        ret = CAMERA_SNAPSHOT;
        break;

    default:
        break;
    }

    return ret;
}
static void cb_key(int key) {
    btn_list[key_to_index(key)]->set_color(255, 0, 0, 255);
    invalidate();
}

static void cb_volume_up_auto(int key) {
    keys_pressed &= ~(1 << VOLUME_UP);
    if(keys_pressed == 0) {
        g_retval = 0;
        sem_post(&g_sem);
    }
}

static void cb_volume_down_auto(int key) {
    keys_pressed &= ~(1 << VOLUME_DOWN);
    if(keys_pressed == 0) {
        g_retval = 0;
        sem_post(&g_sem);
    }
}

static void cb_cam_auto(int key) {
    keys_pressed &= ~(1 << CAMERA_SNAPSHOT);
    if(keys_pressed == 0) {
        g_retval = 0;
        sem_post(&g_sem);
    }
}
void pass(void *) {
    g_retval = 0;
    sem_post(&g_sem);
}

void fail(void *) {
    g_retval = -1;
    sem_post(&g_sem);
}

void add_btn(char *p) {

    int i;
    mmi_point_t text_point;
    mmi_text *text_lebal;
    int width = window->get_width();
    int height = window->get_height();

    mmi_rect_t rect;

    for(i = 0; i < MAX_KEY_NUM - 1; i++) {
        if(!strncmp(panel_btn_name[i], p, strlen(panel_btn_name[i])))
            break;
    }

    ALOGI("set btn %s . \n", panel_btn_name[i]);

    rect.x = width / 9;
    rect.y = height / 20;
    rect.w = width * 1 / 3;
    rect.h = height / 10;

    switch (i) {
    case MENU:
        rect.x = width / 9;
        rect.y = height / 20;
        btn_list[MENU] = new mmi_button(rect, mmi_lang::query("menu"), NULL);
        g_module->add_btn(btn_list[MENU]);
        break;

    case HOME:
        rect.x = width / 9;
        rect.y = height * 4 / 20;
        btn_list[HOME] = new mmi_button(rect, mmi_lang::query("home"), NULL);
        g_module->add_btn(btn_list[HOME]);
        break;

    case BACK:
        rect.x = width / 9;
        rect.y = height * 7 / 20;
        btn_list[BACK] = new mmi_button(rect, mmi_lang::query("back"), NULL);
        g_module->add_btn(btn_list[BACK]);
        break;

    case VOLUME_UP:
        rect.x = width * 5 / 9;
        rect.y = height / 20;
        btn_list[VOLUME_UP] = new mmi_button(rect, mmi_lang::query("volumeup"), NULL);
        g_module->add_btn(btn_list[VOLUME_UP]);
        break;

    case VOLUME_DOWN:
        rect.x = width * 5 / 9;
        rect.y = height * 4 / 20;
        btn_list[VOLUME_DOWN] = new mmi_button(rect, mmi_lang::query("volumedown"), NULL);
        g_module->add_btn(btn_list[VOLUME_DOWN]);
        break;

    case CAMERA_SNAPSHOT:
        rect.x = width * 5 / 9;
        rect.y = height * 7 / 20;
        btn_list[CAMERA_SNAPSHOT] = new mmi_button(rect, mmi_lang::query("camsnapshot"), NULL);
        g_module->add_btn(btn_list[CAMERA_SNAPSHOT]);
        break;
    case SEARCH:
        break;
    default:
        break;
    }
    btn_list[i]->set_color(0, 125, 125, 255);
}

void init_key() {
    btn_cb_list[VOLUME_UP] = mmi_key::get_key_cb(KEY_VOLUMEUP);
    btn_cb_list[VOLUME_DOWN] = mmi_key::get_key_cb(KEY_VOLUMEDOWN);
    btn_cb_list[CAMERA_SNAPSHOT] = mmi_key::get_key_cb(KEY_CAMERA_SNAPSHOT);
    mmi_key::set_key_cb(KEY_VOLUMEUP, cb_volume_up_auto);
    mmi_key::set_key_cb(KEY_VOLUMEDOWN, cb_volume_down_auto);
    mmi_key::set_key_cb(KEY_CAMERA_SNAPSHOT, cb_cam_auto);
}

void initUI(char *virtual_btn) {

    btn_cb_list[BACK] = mmi_key::get_key_cb(KEY_BACK);
    btn_cb_list[HOME] = mmi_key::get_key_cb(KEY_HOME);
    btn_cb_list[HOME] = mmi_key::get_key_cb(KEY_HOMEPAGE);
    btn_cb_list[MENU] = mmi_key::get_key_cb(KEY_MENU);
    btn_cb_list[VOLUME_UP] = mmi_key::get_key_cb(KEY_VOLUMEUP);
    btn_cb_list[VOLUME_DOWN] = mmi_key::get_key_cb(KEY_VOLUMEDOWN);
    btn_cb_list[CAMERA_SNAPSHOT] = mmi_key::get_key_cb(KEY_CAMERA_SNAPSHOT);
    mmi_key::set_key_cb(KEY_BACK, cb_key);
    mmi_key::set_key_cb(KEY_HOME, cb_key);
    mmi_key::set_key_cb(KEY_HOMEPAGE, cb_key);
    mmi_key::set_key_cb(KEY_MENU, cb_key);
    mmi_key::set_key_cb(KEY_VOLUMEUP, cb_key);
    mmi_key::set_key_cb(KEY_VOLUMEDOWN, cb_key);
    mmi_key::set_key_cb(KEY_CAMERA_SNAPSHOT, cb_key);

    window = new mmi_window();
    char *bkup_ptr;
    char *p = strtok_r(virtual_btn, ",", &bkup_ptr);

    while(p != NULL) {
        ALOGI("%s\n", p);
        add_btn(p);
        p = strtok_r(NULL, ",", &bkup_ptr);
    }
    g_module->add_window(window);
    g_module->add_btn_pass(pass);
    g_module->add_btn_fail(fail);
	invalidate();
}

void init() {
    g_retval = -1;
    sem_init(&g_sem, 0, 0);
    char virtual_btn[256] = { 0 };
    parse_parameter(mmi_config::query_config_value(g_module->get_domain(), "parameter"), paras);
    get_para_value(paras, "keys", virtual_btn, sizeof(virtual_btn), "home");
    ALOGI("keys: %s \n", virtual_btn);
    initUI(virtual_btn);
}

void finish() {
    sem_wait(&g_sem);
    g_module->clean_source();
    mmi_key::set_key_cb(KEY_BACK, btn_cb_list[BACK]);
    mmi_key::set_key_cb(KEY_HOME, btn_cb_list[HOME]);
    mmi_key::set_key_cb(KEY_HOMEPAGE, btn_cb_list[HOME]);
    mmi_key::set_key_cb(KEY_MENU, btn_cb_list[MENU]);
    mmi_key::set_key_cb(KEY_VOLUMEDOWN, btn_cb_list[VOLUME_DOWN]);
    mmi_key::set_key_cb(KEY_VOLUMEUP, btn_cb_list[VOLUME_UP]);
    mmi_key::set_key_cb(KEY_CAMERA_SNAPSHOT, btn_cb_list[CAMERA_SNAPSHOT]);
}

void clear_key() {
    mmi_key::set_key_cb(KEY_VOLUMEDOWN, btn_cb_list[VOLUME_DOWN]);
    mmi_key::set_key_cb(KEY_VOLUMEUP, btn_cb_list[VOLUME_UP]);
    mmi_key::set_key_cb(KEY_CAMERA_SNAPSHOT, btn_cb_list[CAMERA_SNAPSHOT]);
}

int add_buttons_auto(char *virtual_btn) {

    int i;
    char *save_ptr;
    char *p = strtok_r(virtual_btn, ",", &save_ptr);

    while(p != NULL) {
        ALOGI("%s\n", p);
        for(i = 0; i < MAX_KEY_NUM - 1; i++) {
            if(!strncmp(panel_btn_name[i], p, strlen(panel_btn_name[i])))
                break;
        }

        switch (i) {
        case VOLUME_UP:
            keys_pressed |= (1 << VOLUME_UP);
            break;

        case VOLUME_DOWN:
            keys_pressed |= (1 << VOLUME_DOWN);
            break;

        case CAMERA_SNAPSHOT:
            keys_pressed |= (1 << CAMERA_SNAPSHOT);
            break;

        default:
            ALOGE("Unknown key, wrong config...");
            return CASE_FAIL;
        }
        p = strtok_r(NULL, ",", &save_ptr);
    }
    return CASE_SUCCESS;
}

int auto_test() {

    struct timespec ts;

    keys_pressed = 0;
    sem_init(&g_sem, 0, 0);
    char temp[256] = { 0 };
    parse_parameter(mmi_config::query_config_value(g_module->get_domain(), "parameter"), paras);
    get_para_value(paras, "timeout", temp, sizeof(temp), "-1");
    timeout = atoi(temp);
    if(timeout < 0 || 60 < timeout) {
        timeout = HW_KEYS_TIMEOUT;
    }

    get_para_value(paras, "keys", temp, sizeof(temp), "home");
    if(add_buttons_auto(temp)) {
        return CASE_FAIL;
    }
    init_key();
    if(clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        ALOGI("Failed to get clock... exiting");
        return CASE_FAIL;
    }

    ts.tv_sec += timeout;
    if(sem_timedwait(&g_sem, &ts)) {
        ALOGE("HW keys timed out");
        if(keys_pressed & (1 << VOLUME_UP))
            return PCBA_VOLUME_UP_KEY_FAIL;
        else if(keys_pressed & (1 << VOLUME_DOWN))
            return PCBA_VOLUME_DOWN_KEY_FAIL;
        else if(keys_pressed & (1 << CAMERA_SNAPSHOT))
            return PCBA_CAMERA_KEY_FAIL;
        return CASE_FAIL;
    }

    clear_key();
    return CASE_SUCCESS;
}

int module_main(mmi_module * mod) {

    if(mod->get_run_mode() == TEST_MODE_PCBA) {
        return auto_test();
    }
    init();
    finish();
    return g_retval;
}
