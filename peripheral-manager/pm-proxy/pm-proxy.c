/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include "pm-service.h"
#include "mdm_detect.h"
#define LOG_TAG "PerMgrProxy"
#include <cutils/log.h>


void *pm_proxy_handle;
void pm_proxy_event_notifier(void *client_cookie, enum pm_event event) {
	//Dont really care much here about events..Just ack whatever comes in.
	pm_client_event_acknowledge(client_cookie, event);
}
int main(int argc, char **argv)
{
	struct dev_info devinfo;
	int i, rcode;
	enum pm_event event;

	ALOGE("Starting pm-proxy service");
	if (get_system_info(&devinfo) != RET_SUCCESS) {
		ALOGE("Failed to get device configuration");
		goto error;
	}
	for (i = 0; i < devinfo.num_modems; i++) {
		if (devinfo.mdm_list[i].type == MDM_TYPE_INTERNAL) {
			ALOGI("Found internal modem");
			rcode = pm_client_register(pm_proxy_event_notifier,
						pm_proxy_handle,
						devinfo.mdm_list[i].mdm_name,
						"PM-PROXY-THREAD",
						&event,
						&pm_proxy_handle);
			ALOGI("pm-proxy-thread Voting for internal modem");
			if (rcode == PM_RET_SUCCESS)
				rcode = pm_client_connect(pm_proxy_handle);
			if (rcode != PM_RET_SUCCESS)
				ALOGE("pm-proxy-thead vote fail");
		}
	}
	do {
		sleep(500000);
	} while(1);
error:
	return RET_FAILED;
}
