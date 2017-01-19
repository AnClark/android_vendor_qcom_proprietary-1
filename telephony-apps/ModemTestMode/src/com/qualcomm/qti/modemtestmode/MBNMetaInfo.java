/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

public class MBNMetaInfo {
    //Expected like xxx-3CSFB-DSDS-CMCC
    private final int META_MIN_DASH_SIZE = 3;
    private String mMBNId;
    private String mMetaInfo;
    private String mDeviceType = "Unknown";
    private String mCarrier = "Unknown";
    private String mMultiMode = "Unknown";
    private byte[] mQcVersion;

    //default 0->can't get multisim configure, 1->ssss, 2->dsdx
    private int mMultiModeNumber = 0;
    private int mSource = MBNTestUtils.MBN_FROM_UNKNOWN;

    public MBNMetaInfo(String mbnId, String metaInfo) {
        this.mMBNId = mbnId;
        this.mMetaInfo = metaInfo;
        setSource(mbnId);
        // get all info from meta instead of MBNID
        setDeviceCarrierMultiMode(metaInfo);
    }

    private void setMultiModeNumber(String mode) {
        if (mode == null) {
            return;
        }
        if (mode.toLowerCase().contains("ss") || mode.toLowerCase().contains("singlesim")) {
            this.mMultiModeNumber = 1;
            this.mMultiMode = "SSSS";
        } else if (mode.toLowerCase().contains("da")) {
            this.mMultiModeNumber = 2;
            this.mMultiMode = "DSDA";
        } else if (mode.toLowerCase().contains("ds")) {
            this.mMultiModeNumber = 2;
            this.mMultiMode = "DSDS";
        }
    }

    public void setQcVersion(byte[] version) {
        this.mQcVersion = version;
    }

    public void setDeviceCarrierMultiMode(String meta) {
        if (meta == null) {
            return;
        }

        String[] tempName;
        if (mSource == MBNTestUtils.MBN_FROM_PC) {
            tempName = meta.split("_");
        } else {
            tempName = meta.split("-");
        }

        int len = tempName.length;
        if (len >= META_MIN_DASH_SIZE) {
            if (mSource == MBNTestUtils.MBN_FROM_PC) {
                this.mCarrier = tempName[0];
                this.mDeviceType = tempName[1];
                setMultiModeNumber(tempName[2].toLowerCase());
            } else {
                this.mCarrier = tempName[len-1];
                this.mDeviceType = tempName[len-3];
                setMultiModeNumber(tempName[len-2].toLowerCase());
            }
        }
        return;
    }

    private void setSource(String meta) {
        if (meta == null) {
            return;
        }
        if (meta.startsWith(MBNTestUtils.GOLDEN_MBN_ID_PREFIX)) {
            this.mSource = MBNTestUtils.MBN_FROM_GOLDEN;
        } else if (meta.startsWith(MBNTestUtils.APP_MBN_ID_PREFIX)) {
            this.mSource = MBNTestUtils.MBN_FROM_APP;
        } else {
            this.mSource = MBNTestUtils.MBN_FROM_PC;
        }
    }

    public String getCarrier() {
        return mCarrier;
    }

    public int getMultiModeNumber() {
        return mMultiModeNumber;
    }

    public String getDeviceType() {
        return mDeviceType;
    }

    public String getMultiMode() {
        return mMultiMode;
    }

    public byte[] getQcVersion() {
        return mQcVersion;
    }

    public String getMBNId() {
        return mMBNId;
    }

    public String getMetaInfo() {
        return mMetaInfo;
    }

    public int getSource() {
        return mSource;
    }

    public String getSourceString() {
        switch (mSource) {
        case MBNTestUtils.MBN_FROM_GOLDEN:
            return "Golden";
        case MBNTestUtils.MBN_FROM_APP:
            return "Application";
        case MBNTestUtils.MBN_FROM_PC:
            return "PC";
        default:
            return "Unknown";
        }
    }

    @Override
    public String toString() {
        return "Meta Info:" + mMetaInfo + " MBN ID:" + mMBNId + " Source:" + getSourceString();
    }

}
