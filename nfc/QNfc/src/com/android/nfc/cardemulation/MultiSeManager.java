/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package com.android.nfc.cardemulation;

import com.android.nfc.Debug;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.nfc.cardemulation.ApduServiceInfo;
import android.nfc.cardemulation.CardEmulation;
import android.util.Log;
import android.nfc.cardemulation.AidGroup;
import android.os.UserHandle;

import com.google.android.collect.Maps;

import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.PriorityQueue;
import java.util.TreeMap;
import com.android.nfc.QSecureElementManager;

public class MultiSeManager {
    static final String TAG = "MultiSeManager";

    static final boolean DBG = true;
    static final String PPSE_AID = "325041592E5359532E4444463031";
    static final char MAX_DB_SZ = 40;

    byte [] mPpseResponse = null;

    final Context mContext;
    CardEmulationManager mCardEmulationManager;
    public RegisteredServicesCache mServiceCache;
    // public CardEmulationInterface mCardEmulationInterface;
    private static MultiSeManager mService;
    private static boolean mPpseRespSent = false;

    //NCI MULTI SE CONTROL SUB CMD
    static final int NCI_GET_MAX_PPSE_SZ = 0x00;
    static final int NCI_SET_PPSE_RSP    = 0x01;
    static final int NCI_CLR_PRV_PPSE_RSP = 0x02;
    static final int NCI_SET_DEFAULT_PPSE = 0x03;

    //Database Object
    class myDataBase{
        String aid;
        String seName;
        int pr;
        int pw;
    };

    static int mDbCount;
    static myDataBase[] mSeDataBase = new myDataBase[MAX_DB_SZ];

    public MultiSeManager(Context context, CardEmulationManager cardEmulationManager) {
        mContext = context;
        mCardEmulationManager = cardEmulationManager;
        mServiceCache = cardEmulationManager.mServiceCache;
        //mCardEmulationManager.getNfcCardEmulationInterface();

        mService = this;
        //Initialie the Database
        for (int i=0; i < MAX_DB_SZ; i++) {
            mSeDataBase[i] = new myDataBase();
        }
    }

    public boolean multiSeRegisterAid(List<String> aids, ComponentName paymentService,
                                         List<String> seName, List<String> priority, List<String> powerState) {

        String category = CardEmulation.CATEGORY_PAYMENT;
        AidGroup aidGroup = new AidGroup(aids, category);
        int userId = UserHandle.myUserId();

        //dump received aid list & other
        if (DBG) Log.d(TAG,"print... the received list ");
        if (DBG) dumpList(aids,seName,priority,powerState);

        //save the received aid list & other to database & print
        if (DBG) Log.d(TAG,"save the list in database ");
        multiSeDataBase(aids,seName,priority,powerState);

        if (DBG) Log.d(TAG,"component info:" + paymentService);

        try {
            Log.d(TAG, "Register the AIDs");
            mCardEmulationManager.getNfcCardEmulationInterface().registerAidGroupForService(userId,paymentService,aidGroup);
        } catch(Exception e) {
            Log.e(TAG, "Register AID failed.");
        }

        return true;
    }


    public byte[] generatePpseResponse() {
        if (DBG) Log.d(TAG, "generatePpseResponse");

        int totalLength = 0;
        int appLabelLength = 0;
        String appLabel = null;
        String seName = null;
        String mseName = null;
        String mseCheck = null;
        String sPriority = null;
        boolean moreSecureElements = false;
        byte [] mPrioritycnt = new byte[] {(byte)0x01};

        ArrayList<String> aidsInPPSE = new ArrayList<String>();
        ArrayList<String> appLabels = new ArrayList<String>();
        ArrayList<String> priorities = new ArrayList<String>();

        mPpseResponse = null;

        // For each AID, find interested services
        for (Map.Entry<String, RegisteredAidCache.AidResolveInfo> aidEntry:
                mCardEmulationManager.mAidCache.mAidCache.entrySet()) {
            String aid = aidEntry.getKey();
            RegisteredAidCache.AidResolveInfo resolveInfo = aidEntry.getValue();
            ApduServiceInfo service;

            if (aid.equals(PPSE_AID)) {
                if (DBG) Log.d(TAG, "Skip PPSE AID");
                continue;
            }

            if (resolveInfo.defaultService != null) {
                // There is a default service set
                service = resolveInfo.defaultService;

                //Check if two/more SEs available for selected AIDs
                //send PPSE resp only if two/more SEs available
                mseName = resolveInfo.defaultService.getSeName();
                if(mseName.equals("MULTISE")) {
                    seName = getAidMappedSe(aid);
                    Log.d(TAG, "Selected SE: " + seName);
                    if ((moreSecureElements == false) && (mseCheck != null) &&
                        (mseCheck.equals(seName) == false)) {
                        moreSecureElements = true;
                        if (DBG) Log.d(TAG, "More SEs Selected, 1st: " + mseCheck + "  2nd: " + seName);
                    }
                    mseCheck = seName;
                }
                else
                    return mPpseResponse;

            } else if (resolveInfo.services.size() >= 1) {
                // More than one service
                service = resolveInfo.services.get(0);
            } else {
                continue;
            }

            // TO DO: May rework on priority assignment with super wallet
            appLabelLength = service.getDescription().length();
            appLabel = service.getDescription();

            mPrioritycnt[0] = (byte)(getAidMappedPriority(aid));
            sPriority = bytesToString(mPrioritycnt);

            // ((0x61 + LEN + (0x4F + LEN + (AID) + 0x50 + LEN + (LABEL) + 0x87 + LEN + (PRI))))
            // PPSE entry size = 9 bytes of constants in header data + hex encoded aid payload + label and (255 + 4) payload and TLV size.
            // 9

            if ((totalLength + 9 + (aid.length()/2) + appLabelLength) > (1024)) {
                if (DBG) Log.d(TAG, "Too much data for PPSE response");
                break;
            }


            aidsInPPSE.add(aid);
            appLabels.add(appLabel);
            priorities.add(sPriority);

            totalLength += 9 + (aid.length()/2) + appLabelLength;
        }

        if ((aidsInPPSE.size() == 0) || (moreSecureElements == false)) {
            if (DBG) Log.d(TAG, "No AID for PPSE Response");
            return mPpseResponse;
        }

        int nextWrite = 0;
        mPpseResponse = new byte[totalLength];

        // adding AID/Priority
        for (int xx = 0; xx < aidsInPPSE.size(); xx++) {

            String aid = aidsInPPSE.get(xx);
            byte[] aidBytes = hexStringToByteArray(aid);

            appLabel = appLabels.get(xx);
            if (appLabel == null)
                appLabelLength = 0;
            else
                appLabelLength = appLabel.length();

            sPriority = priorities.get(xx);
            byte[] priorityBytes = hexStringToByteArray(sPriority);
            int priority = priorityBytes[0];

            if (DBG) Log.d(TAG, "AID = " + aid + ", appLabel = " + appLabel + ", priority = " + priority);

            // (0x61 + LEN + (0x4F + LEN + (AID) + 0x50 + LEN + (LABEL) + 0x87 + LEN + (PRI)))
            // FCI Template
            //
            // Directory Entry
            mPpseResponse[nextWrite++] = (byte)0x61;
            mPpseResponse[nextWrite++] = (byte)(7 + aidBytes.length + appLabelLength);
            // DF Name (AID)
            mPpseResponse[nextWrite++] = (byte)0x4F;
            mPpseResponse[nextWrite++] = (byte)(aidBytes.length);
            for (int i = 0; i < aidBytes.length; i++) {
                mPpseResponse[nextWrite++] = aidBytes[i];
            }
            // Application Label
            mPpseResponse[nextWrite++] = (byte)0x50;
            mPpseResponse[nextWrite++] = (byte)appLabelLength;
            for (int i = 0; i < appLabelLength; i++) {
                mPpseResponse[nextWrite++] = (byte)appLabel.charAt(i);
            }
            // Priority Indicator
            mPpseResponse[nextWrite++] = (byte)0x87;
            mPpseResponse[nextWrite++] = (byte)0x01;
            mPpseResponse[nextWrite++] = (byte)priority;
        }

        if (DBG) Log.d(TAG, " mPpseResponse = 0x" + bytesToString(mPpseResponse));
        return mPpseResponse;
    }

    String dumpEntry(Map.Entry<String, RegisteredAidCache.AidResolveInfo> entry) {
        StringBuilder sb = new StringBuilder();
        sb.append("    \"" + entry.getKey() + "\"\n");
        ApduServiceInfo defaultServiceInfo = entry.getValue().defaultService;
        ComponentName defaultComponent = defaultServiceInfo != null ?
                defaultServiceInfo.getComponent() : null;

        for (ApduServiceInfo serviceInfo : entry.getValue().services) {
            sb.append("        ");
            if (serviceInfo.getComponent().equals(defaultComponent)) {
                sb.append("*DEFAULT* ");
            }
            sb.append(serviceInfo.getComponent() +
                    " (Description: " + serviceInfo.getDescription() + ")\n");
        }
        return sb.toString();
    }

    public void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.println("    AID cache entries: ");
        for (Map.Entry<String, RegisteredAidCache.AidResolveInfo> entry : mCardEmulationManager.mAidCache.mAidCache.entrySet()) {
            pw.println(dumpEntry(entry));
        }
        //pw.println("    Service preferred by foreground app: " + mPreferredForegroundService);
        //pw.println("    Preferred payment service: " + mPreferredPaymentService);
        pw.println("");
        //mRoutingManager.dump(fd, pw, args);
        pw.println("");
    }


    static byte[] hexStringToByteArray(String s) {
       int len = s.length();
       byte[] data = new byte[len / 2];
       for (int i = 0; i < len; i += 2) {
           data[i / 2] = (byte) ((Character.digit(s.charAt(i), 16) << 4)
                                + Character.digit(s.charAt(i+1), 16));
       }
       return data;
    }

    public void processPpse() {
       byte [] ppse_rsp = generatePpseResponse();
       if (ppse_rsp != null) {
           if (DBG) Log.d(TAG,"Sending PPSE response");
           QSecureElementManager.getInstance().multiSeControlCmd(ppse_rsp,NCI_SET_PPSE_RSP);
           mPpseRespSent = true;
       } else if(mPpseRespSent) {
           if (DBG) Log.d(TAG,"Clear previous PPSE response");
           QSecureElementManager.getInstance().multiSeControlCmd(ppse_rsp,NCI_CLR_PRV_PPSE_RSP);
           mPpseRespSent = false;
       }
    }

    static boolean dumpList(List<String> aids, List<String> seName, List<String> priority, List<String> powerState) {

        Log.d(TAG,"dump_list: aid size =" + aids.size());

        for (String aid : aids) {
            Log.d(TAG, "Aid:" + aid);
        }
        for (String se : seName) {
            Log.d(TAG, "SE:" + se);
        }
        for (String pr : priority) {
            Log.d(TAG, "Priority:" + pr);
        }
        for (String pw : powerState) {
            Log.d(TAG, "PowerState:" + pw);
        }
        return true;
    }

    static boolean multiSeDataBase(List<String> aids, List<String> seName, List<String> priority, List<String> powerState) {
        int i = 0;
        mDbCount = aids.size();
        if (mDbCount > MAX_DB_SZ)
            mDbCount = MAX_DB_SZ;

        i=0;
        for (String aid : aids) {
            mSeDataBase[i++].aid = aid;
            if (i >= (MAX_DB_SZ - 1))
                break;
        }

        i=0;
        for (String se : seName) {
            mSeDataBase[i++].seName = se;
            if (i >= (MAX_DB_SZ - 1))
                break;
        }

        i=0;
        for (String pr : priority) {
            mSeDataBase[i++].pr = Integer.parseInt(pr);
            if (i >= (MAX_DB_SZ - 1))
                break;
        }

        i=0;
        for (String pw : powerState) {
            mSeDataBase[i++].pw = Integer.parseInt(pw);
            if (i >= (MAX_DB_SZ - 1))
                break;
        }

        //print the saveddata base
        if(DBG) {
            Log.d(TAG, "Print the stored data base");
            for(i=0; i < mDbCount; i++) {
                Log.d(TAG, "aid: " +  mSeDataBase[i].aid + "  SE:" + mSeDataBase[i].seName +
                          "  Priority:" + mSeDataBase[i].pr + "  PowerState:" + mSeDataBase[i].pw);
            }
        }
        return true;
    }

    static String getAidMappedSe(String aids) {
        int i = 0;
        if (DBG) Log.d(TAG, "Find the SE name from the given AID");

        for(i=0; i < mDbCount; i++) {
            if(aids.equals(mSeDataBase[i].aid))
                return mSeDataBase[i].seName;
        }
        return null;
    }

    static int getAidMappedPriority(String aids) {
        int i = 0;
        if (DBG) Log.d(TAG, "Find the Priority from the given AID");

        for(i=0; i < mDbCount; i++) {
            if(aids.equals(mSeDataBase[i].aid))
                return mSeDataBase[i].pr;
        }
        return 0;
    }

    static int getAidMappedPowerState(String aids) {
        int i = 0;
        if (DBG) Log.d(TAG, "Find the Priority from the given AID");
        for(i=0; i < mDbCount; i++) {
            if(aids.equals(mSeDataBase[i].aid))
                return mSeDataBase[i].pw;
        }
        return 0;
    }

    static String bytesToString(byte[] bytes) {
        final char[] hexChars = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
        char[] chars = new char[bytes.length * 2];
        int byteValue;
        for (int j = 0; j < bytes.length; j++) {
            byteValue = bytes[j] & 0xFF;
            chars[j * 2] = hexChars[byteValue >>> 4];
            chars[j * 2 + 1] = hexChars[byteValue & 0x0F];
        }
        return new String(chars);
    }

    public static MultiSeManager getInstance() {
        if (DBG) Log.d(TAG, "getInstance");
        return mService;
    }
}
