/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package com.qualcomm.qti.telephony;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Message;
import android.os.Messenger;
import android.os.SystemProperties;
import android.os.Looper;
import android.util.Log;

import com.android.internal.util.AsyncChannel;
import com.qualcomm.qcrilhook.IQcRilHook;
import com.qualcomm.qcrilhook.QcRilHook;
import com.qualcomm.qcrilhook.QcRilHookCallback;

import java.io.File;
import java.lang.Class;
import java.lang.Exception;
import java.lang.reflect.Method;
import java.lang.reflect.Constructor;
import dalvik.system.DexClassLoader;


/**
 * System service to process DDS switch requests
 */
public class QtiDdsSwitchService extends Service {
    public static final String TAG = "QtiDdsSwitchService";

    private static final int EVENT_REGISTER = 0;
    private static final int EVENT_REQUEST_DDS_SWITCH = 1;

    Handler mHandler;
    Context mContext;
    boolean mOemHookReady = false;
    Object  mQcrilHook;
    Method mSetDDs;

    QcRilHookCallback mCallback;

    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(TAG, "onCreate");
        mContext = this;

        HandlerThread handlerThread = new HandlerThread("QcDdsSwitchHandler");
        handlerThread.start();

        mHandler = new DdsSwitchHandler(handlerThread.getLooper());
    }

    @Override
    public int onStartCommand(Intent intet, int flags, int startId) {
        Log.d(TAG, "onStart");

        mHandler.sendMessage(mHandler.obtainMessage(EVENT_REGISTER));
        //recover incase of phone process crash. START_STICKY.
        return Service.START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    void registerWithSubscriptionController(Handler handler) {

        try {

            Class cls = Class.forName(
                    "com.android.internal.telephony.dataconnection.DctController");
            Method method = cls.getDeclaredMethod("getInstance");
            Object dctController = method.invoke(null);

            Log.d(TAG, "got the dctController = " + dctController);

            Class[] param = new Class[1];
            param[0] = Messenger.class;
            Method registerMethod = cls.getDeclaredMethod("registerDdsSwitchPropService", param);

            registerMethod.invoke(dctController, new Messenger(handler));

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    void initOemHook() {
        Log.d(TAG, "initOemHook");

        try {
            String libPath = "/system/framework/qcrilhook.jar";
            File tmpDir = mContext.getDir("dex", 0);

            DexClassLoader classLoader = new DexClassLoader(libPath, tmpDir.getAbsolutePath(),
                    null, this.getClass().getClassLoader());
            Class<Object> cls = (Class<Object>) classLoader
                    .loadClass("com.qualcomm.qcrilhook.QcRilHook");

            Log.d(TAG, "class  = " + cls);
            Constructor constr  = cls.getConstructor(new Class[] {Context.class});

            mQcrilHook = constr.newInstance(mContext);
            Log.d(TAG, "mQcrilHook = " + mQcrilHook);
            mSetDDs = cls.getDeclaredMethod("qcRilSendDDSInfo", int.class, int.class);

            Log.d(TAG, "mSetDDs = " + mSetDDs);
            mOemHookReady = true;

        } catch (Exception e) {
            Log.e(TAG, "Exception = " + e);
        }
    }

    void invokeOemHookDdsSwitch(final int phoneCount, final int phoneId) {
        if(!mOemHookReady) {
            Log.d(TAG, "Oem hook not ready yet, ignore");
        } else {
            for(int i = 0; i < phoneCount; i++) {
                Log.d(TAG, "qcRilSendDDSInfo ril= " + i + ", DDS=" + phoneId);

                try {
                    mSetDDs.invoke(mQcrilHook, phoneId, i);
                } catch(Exception e) {
                    Log.e(TAG, "Exception = " + e);
                }
            }
        }

    }

    class DdsSwitchHandler extends Handler {

        public DdsSwitchHandler(Looper looper) {
            super(looper);
        }

        public void handleMessage(Message msg) {
            Log.d(TAG, "HandleMessage msg = " + msg);

            switch (msg.what) {
                case EVENT_REGISTER: {
                    Log.d(TAG, "EVENT_REGISTER");
                    if (!mOemHookReady) {
                        initOemHook();
                    }
                    registerWithSubscriptionController(this);
                    break;
                }

                case EVENT_REQUEST_DDS_SWITCH: {
                    int phoneId = msg.arg1;
                    int phoneCount = msg.arg2;
                    int result = 1;
                    Log.d(TAG, "EVENT_REQUEST_DDS_SWITCH for phoneId = " + phoneId);
                    AsyncChannel replyAc = new AsyncChannel();

                    invokeOemHookDdsSwitch(phoneCount, phoneId);
                    replyAc.replyToMessage(msg, EVENT_REQUEST_DDS_SWITCH, result);
                    Log.d(TAG, "EVENT_REQUEST_DDS_SWITCH Done.");

                    break;
                }

            }

        }
    }

}
