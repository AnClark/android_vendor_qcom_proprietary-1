/*
 * Copyright (c) 2013, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __LIBMMI_COMPONENT__
#define __LIBMMI_COMPONENT__

#define MAX_DEFAULT_TEXT_LEN 80

typedef void (*mmi_cb_t) (void *);

typedef struct {
    char r;
    char g;
    char b;
    char a;
} mmi_color_t;

typedef struct {
    int x;
    int y;
} mmi_point_t;

#endif
