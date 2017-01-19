/*
 * Copyright (c) 2014, Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "mmi_utils.h"
#include "mmi_lang.h"

using namespace std;

static const char *lang_filepath[] = {
    "/etc/mmi/strings.xml",
    "/etc/mmi/strings-zh-rCN.xml",
};

hash_map <string, string> mmi_lang::strings_map;

int mmi_lang::load(lang_type lang) {

    if(lang < 0 || lang >= MAX_SUPPORT_LANG)
        return -1;

    xmlDocPtr doc;
    xmlNodePtr curNode;
    char *name = NULL;
    char *value = NULL;

    /*clear map before initialize */
    strings_map.clear();

    doc = xmlReadFile(lang_filepath[lang], "UTF-8", XML_PARSE_RECOVER);
    if(NULL == doc) {
        ALOGI("Document not parsed successfully.\n ");
        return -1;
    }

    curNode = xmlDocGetRootElement(doc);
    if(NULL == curNode) {
        ALOGI("empty document \n");
        xmlFreeDoc(doc);
        return -1;
    }


    if(xmlStrcmp(curNode->name, BAD_CAST "resources")) {
        ALOGI("document of the wrong type, root node != root \n");
        xmlFreeDoc(doc);
        return -1;
    }

    curNode = curNode->xmlChildrenNode;
    while(curNode != NULL) {
        if(!xmlStrcmp(curNode->name, BAD_CAST "string")) {
            name = (char *) xmlGetProp(curNode, BAD_CAST "name");
            value = (char *) xmlNodeGetContent(curNode);
            if(name != NULL && value != NULL) {
                strings_map[name] = value;
                xmlFree(value);
                ALOGI("newNode after: %s -%s \n", name, strings_map[name].c_str());
            }
        }
        curNode = curNode->next;
    }

    xmlFreeDoc(doc);
    return 0;
}

const char *mmi_lang::query(const char *key) {
    return strings_map[key].c_str();
}
