/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package com.gsma.services.nfc;

import android.os.Messenger;

/**
 * GSMA service interface.
 */
interface IGsmaService {

    boolean isNfccEnabled();
    boolean enableNfcc(in Messenger clientMessenger);
    boolean isCardEmulationEnabled();
    boolean enableCardEmulationMode(in Messenger clientMessenger);
    boolean disableCardEmulationMode(in Messenger clientMessenger);
    String getActiveSecureElement();
    void setActiveSecureElement(String SEName);
    void mgetPname(String packageN);
    void enableMultiReception(String SEName);

}

