/*
* Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
* Qualcomm Technologies Confidential and Proprietary.
*
* Not a Contribution.
* Apache license notifications and license are retained
* for attribution purposes only.
*/

 /*
  * Copyright (C) 2008 The Android Open Source Project
  *
  * Licensed under the Apache License, Version 2.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License at
  *
  *      http://www.apache.org/licenses/LICENSE-2.0
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  */

#include "common.h"
#include <utils/Timers.h>
#include "hardware/sensors.h"
#include "mmi_utils.h"

static struct sensors_poll_device_t *device = NULL;
static struct sensors_module_t *module = NULL;
static struct sensor_t const *mList = NULL;
static const size_t numEvents = 16;
static int devCount = 0;
static sensors_event_t buffer[numEvents];

char const *getSensorName(int type) {
    switch (type) {
    case SENSOR_TYPE_ACCELEROMETER:
        return "Acc";
    case SENSOR_TYPE_MAGNETIC_FIELD:
        return "Mag";
    case SENSOR_TYPE_ORIENTATION:
        return "Ori";
    case SENSOR_TYPE_GYROSCOPE:
        return "Gyr";
    case SENSOR_TYPE_LIGHT:
        return "Lux";
    case SENSOR_TYPE_PRESSURE:
        return "Bar";
    case SENSOR_TYPE_TEMPERATURE:
        return "Tmp";
    case SENSOR_TYPE_PROXIMITY:
        return "Prx";
    case SENSOR_TYPE_GRAVITY:
        return "Grv";
    case SENSOR_TYPE_LINEAR_ACCELERATION:
        return "Lac";
    case SENSOR_TYPE_ROTATION_VECTOR:
        return "Rot";
    case SENSOR_TYPE_RELATIVE_HUMIDITY:
        return "Hum";
    case SENSOR_TYPE_AMBIENT_TEMPERATURE:
        return "Tam";
    }
    return "ukn";
}

void drawThisText(mmi_module * mModule, char *mText, int startX, int startY, bool clean) {
    mmi_window *window = new mmi_window();
    int width = window->get_width();
    int height = window->get_height();
    mmi_text *text;
    mmi_point_t point = { startX * width / 20, startY * height / 20 };

    text = new mmi_text(point, mText);
    if(clean)
        mModule->clean_text();
    mModule->add_text(text);
}

int initSensor(int senNum) {
    int err,i;
    static int sensor_test_initialized;

    if(!sensor_test_initialized) {
        err = hw_get_module(SENSORS_HARDWARE_MODULE_ID, (hw_module_t const **) &module);
        if(err != 0) {
            ALOGE("hw_get_module() failed (%s)\n", strerror(-err));
            return -1;
        }

        err = sensors_open(&module->common, &device);
        if(err != 0) {
            ALOGE("sensors_open() failed (%s)\n", strerror(-err));
            return -1;
        }
        sensor_test_initialized = 1;
    }
    devCount = module->get_sensors_list(module, &mList);
    for(i = 0; i < devCount; i++) {
        ALOGI("Deactivating all sensor after open,current index: %d", i);
        err = device->activate(device, mList[i].handle, 0);
        if(err != 0) {
            ALOGE("deactivate() for '%s'failed (%s)\n", mList[i].name, strerror(-err));
            goto error;
        }
    }
    for(i = 0; i < devCount; i++) {
        if(mList[i].type == senNum) {
            device->setDelay(device, mList[i].handle, ms2ns(DELAY_500_MS));
            ALOGI("Activating sensor %d", senNum);
            err = device->activate(device, mList[i].handle, 1);
            if(err != 0) {
                ALOGE("activate() for '%s'failed (%s)\n", mList[i].name, strerror(-err));
                goto error;
            }

            break;
        }
    }
    return 0;
  error:
    ALOGE("InitSensor failed");
    return -1;
}

int deinitSensor(int senNum) {
    int err = -1;

    if((device == NULL) || (module == NULL) || (mList == NULL)) {
        ALOGE("DeInitSensor NULL check failed\n");
        return -1;
    }

    if(senNum < 0) {
        ALOGE("Invalid sensor number %d passed to deinitSensor", senNum);
        return -1;
    }
    for(int i = 0; i < devCount; i++) {
        if(mList[i].type == senNum) {
            ALOGI("Deactivating sensor %d", senNum);
            err = device->activate(device, mList[i].handle, 0);
            if(err != 0) {
                ALOGE("deactivate() for '%s'failed (%s)\n", mList[i].name, strerror(-err));
                break;
            }
        }
    }
    return err;
}

int testThisSensor(int sensor_type, mmi_module * mod) {
    int err = -1;
    char sensorDataToPrint[256];

    if((device == NULL) || (module == NULL))
        return -1;

    int n = device->poll(device, buffer, numEvents);

    if(n < 0) {
        ALOGE("poll() failed (%s)\n", strerror(-err));
    }

    for(int i = 0; i < n; i++) {
            const sensors_event_t & data = buffer[i];
            if (sensor_type == data.type) {
                switch (data.type) {
                case SENSOR_TYPE_ACCELEROMETER:
                case SENSOR_TYPE_MAGNETIC_FIELD:
                case SENSOR_TYPE_ORIENTATION:
                case SENSOR_TYPE_GYROSCOPE:
                case SENSOR_TYPE_GRAVITY:
                case SENSOR_TYPE_LINEAR_ACCELERATION:
                case SENSOR_TYPE_ROTATION_VECTOR:
                    // Skip drawing when in PCBA mode. Probably display is
                    // missing or nobody will look there
                if(mod->get_run_mode() == TEST_MODE_UI) {
                    snprintf(sensorDataToPrint,
                             sizeof(sensorDataToPrint),
                             "value=<%5.1f,%5.1f,%5.1f>\n", data.data[0], data.data[1], data.data[2]);
                    drawThisText(mod, (char *) getSensorName(data.type), 4, 4, true);
                    drawThisText(mod, sensorDataToPrint, 4, 5, false);
                        invalidate();
                    }
                mod->addFloatResult("X", data.data[0]);
                mod->addFloatResult("Y", data.data[1]);
                mod->addFloatResult("Z", data.data[2]);
                ALOGI("value=<%5.1f,%5.1f,%5.1f>\n", data.data[0], data.data[1], data.data[2]);


                err = 0;
                break;
            case SENSOR_TYPE_LIGHT:
            case SENSOR_TYPE_PRESSURE:
            case SENSOR_TYPE_TEMPERATURE:
            case SENSOR_TYPE_PROXIMITY:
            case SENSOR_TYPE_RELATIVE_HUMIDITY:
            case SENSOR_TYPE_AMBIENT_TEMPERATURE:
                // Skip drawing when in PCBA mode. Probably display is
                // missing or nobody will look there
                if(mod->get_run_mode() == TEST_MODE_UI) {
                    snprintf(sensorDataToPrint, sizeof(sensorDataToPrint), "value=%f\n", data.data[0]);
                    drawThisText(mod, (char *) getSensorName(data.type), 4, 4, true);
                    drawThisText(mod, sensorDataToPrint, 4, 5, false);
                        invalidate();
                }
                mod->addFloatResult("Value", data.data[0]);
                ALOGI("value=%f\n", data.data[0]);

                err = 0;
                break;
            default:
                ALOGE("Data received, but sensor is unknown... returning");
                break;
            }
        }
    }
    return err;
}
