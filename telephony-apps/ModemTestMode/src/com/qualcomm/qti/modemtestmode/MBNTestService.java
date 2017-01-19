/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

import android.app.AlertDialog;
import android.app.Service;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.RemoteException;
import android.text.TextUtils;
import android.util.Log;
import android.view.WindowManager;
import com.android.internal.telephony.TelephonyProperties;
import android.os.SystemProperties;
import android.provider.Settings;

import com.qualcomm.qti.modemtestmode.IMBNTestService;

public class MBNTestService extends Service {
    private static final String TAG = "MBNTestService";
    private static final int EVENT_QCRIL_HOOK_READY = 1;
    private static final int EVENT_QCRIL_HOOK_UNREG = 2;

    private String mModemMode = null;


    private Handler mHandler = new Handler() {

        @Override
        public void dispatchMessage(Message msg) {
            switch (msg.what) {
            case EVENT_QCRIL_HOOK_READY:
                log("EVENT_QCRIL_HOOK_READY");
                MBNAppGlobals.getInstance().unregisterQcRilHookReady(mHandler);
                if (!isModeMatched()) {
                    log("Multi-Sim Mode unmatched. Alerting...");
                    showMismatchDialog();
                }
                break;
            default:
                log("Unexpected event:" + msg.what);
                break;
            }
        }
    };

    private final IMBNTestService.Stub mBinder = new IMBNTestService.Stub() {

        @Override
        public void setProperty(String property, String value)
                throws RemoteException {
            log ("set " + property + " :" + value);
            SystemProperties.set(property, value);
        }

    };

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        log("Service is started");
        MBNAppGlobals.getInstance().registerQcRilHookReady(mHandler,
                EVENT_QCRIL_HOOK_READY, null);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        MBNAppGlobals.getInstance().unregisterQcRilHookReady(mHandler);
    }

    private boolean isModeMatched() {
        boolean matched = true;
        String telMode = MBNTestUtils.getPropertyValue(
                TelephonyProperties.PROPERTY_MULTI_SIM_CONFIG);
        String mbnConfig = MBNAppGlobals.getInstance().getMBNConfig(0);
        String metaInfo = MBNAppGlobals.getInstance().getMetaInfoForConfig(mbnConfig);

        log("telMode:" + telMode + " mbnConfig:" + mbnConfig + " metaInfo:" + metaInfo);

        // TODO one of mbn id or meta info can be NULL?
        if (TextUtils.isEmpty(MBNTestUtils.getMultiSimMode(mbnConfig)) ||
                TextUtils.isEmpty(MBNTestUtils.getMultiSimMode(metaInfo))) {
            log("Fail to find MultiMode from MBN or Meta Info");
            return true;
        }

        if (telMode == null) {
            telMode = ""; //in case fail get multi mode situation.
        }

        // Can't use toLower
        if (telMode.equals(MBNTestUtils.getMultiSimMode(mbnConfig)) ||
                telMode.equals(MBNTestUtils.getMultiSimMode(metaInfo))) {
            return true;
        }

        mModemMode = TextUtils.isEmpty(MBNTestUtils.getMultiSimMode(mbnConfig))
                ?   MBNTestUtils.getMultiSimMode(metaInfo) :
                    MBNTestUtils.getMultiSimMode(mbnConfig);

        return false;
    }

    private void showMismatchDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        AlertDialog alertDialog = builder
                .setMessage(R.string.mismacth_hit)
                .setPositiveButton(R.string.ok, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialogInterface, int i) {
                        try {
                            mBinder.setProperty(TelephonyProperties.
                                    PROPERTY_MULTI_SIM_CONFIG, mModemMode);
                        } catch (RemoteException e) {
                        }
                        MBNTestUtils.rebootSystem(MBNTestService.this);
                    }
                })
                .setCancelable(false)
                .create();
        alertDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_DIALOG);
        alertDialog.show();
    }

    private void log(String msg) {
        Log.d(TAG, "MBNTest_ " + msg);
    }
}
