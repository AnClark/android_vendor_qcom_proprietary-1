/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package com.gsma.services.nfc;

import android.content.Context;
import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.graphics.drawable.Drawable;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import com.gsma.services.nfc.AidGroup;

public final class OffHostService {

    private static final String TAG = "GsmaOffHostService";
    final static boolean DBG = true;

    private OffHostService mOffHostService;
    public Context mContext;

    private volatile IGsmaService mGsmaService;
    boolean mIsBound = false;
    MyConnection mConn = null;

    public class MyConnection implements ServiceConnection {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
            if (DBG) Log.d(TAG, "onServiceConnected");
            mGsmaService = IGsmaService.Stub.asInterface(service);
        }

        @Override
        public void onServiceDisconnected(ComponentName className) {
            if (DBG) Log.d(TAG, "onServiceDiscnnected");
            mGsmaService = null;
        }
    };

    void bindService(Context context) {
        if (DBG) Log.d(TAG, "bindService()");
        mIsBound = ServiceUtils.bindService(context, mConn);
    }

    void unbindService(Context context) {
        if (DBG) Log.d(TAG, "unbindService()");
        if (mIsBound) {
            context.unbindService(mConn);
            mIsBound = false;
        }
    }

    public OffHostService (Context context) {
        mContext=context;
        if (mConn == null ) {
            mConn = new MyConnection();
            bindService(context);
        }
    }

    // Update the Android Framework with all pending updates.
    public static void commit() {

    }

    // Create a new empty group of AIDs for the "Off-Host" service.
    public AidGroup defineAidGroup(String description, String category){
        return null;
    }

    //Delete an existing AID group from the "Off-Host" service.
    public void  deleteAidGroup(AidGroup group){

    }

    //Return a list of the AID groups linked to the "Off-Host" service.
    public AidGroup[] getAidGroups() {
        return null;
    }

    //Return the banner linked to the "Off-Host" service.
    public Drawable getBanner (){
        return null;
    }

    //Return the description of the "Off-Host" service.
    public String getDescription (){
        return null;
    }

    //Return the Secure Element name which holds the "Off-Host" service.
    public String getLocation (){
        return null;
    }

    //Set a banner for the "Off-Host" service.
    public void setBanner(Drawable banner){
    }
}

