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

public final class AidGroup {

    private static final String TAG = "GsmaAidGroup";
    final static boolean DBG = true;


    private AidGroup mAidGroup;
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

    public AidGroup (Context context) {
        mContext=context;
        if (mConn == null ) {
            mConn = new MyConnection();
            bindService(context);
        }
    }

    // Add a new AID to the current group.
    public static void addNewAid(String Aid) {
    }

    //Return the category of the group of AIDs.
    public String getCategory(){
        return null;
    }

    //Return the description of the group of AIDs
    public String getDescription (){
        return null;
    }

    //Remove an AID from the current group.
    public void removeAid(String Aid){
    }
}

