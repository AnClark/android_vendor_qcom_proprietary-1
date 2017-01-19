/*
 * Copyright (c) 2013, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __SYSTEM_CORE_MMI_DRAW__
#define __SYSTEM_CORE_MMI_DRAW__
typedef void (*p_gr_text)(int x, int y, const char *s, int bold);
void *draw_thread(void *);

#endif
