/*
 * Copyright (c) 2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __SYSTEM_CORE_MMI_LANG__
#define __SYSTEM_CORE_MMI_LANG__

using namespace std;

typedef enum {
    EN = 0,
    ZH_rCN = 1,
    MAX_SUPPORT_LANG
} lang_type;

class mmi_lang {
  public:
    static int load(lang_type lang);
    static const char *query(const char *key);

  private:
    static hash_map <string, string> strings_map;
};
#endif
