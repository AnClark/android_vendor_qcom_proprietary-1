/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.util.Log;

import com.android.internal.telephony.TelephonyIntents;

public class DefaultReceiver extends BroadcastReceiver {
    private static final String TAG = "DefaultReceiver";
    private static final String APP_INSTALLED = "installed";
    private static final String HOST_CODE_ENABLE = "6266344";
    private static final String HOST_CODE_DISABLE = "36266344";
    private static final String HOST_CODE_QENABLE = "76266344";

    @Override
    public void onReceive(Context context, Intent intent) {
        // TODO Auto-generated method stub
        String action = intent.getAction();
        log("Received " + action);

        if (Intent.ACTION_BOOT_COMPLETED.equals(action)) {
            setMBNAppState(context, getMBNAppStateFromSP(context));
            context.startService(new Intent(context, MBNTestService.class));
        } else if (TelephonyIntents.SECRET_CODE_ACTION.equals(action)) {
            String host = intent.getData() != null
                    ? intent.getData().getHost() : null;

            if (HOST_CODE_ENABLE.equals(host)) {
                //*#*#mconfig#*#*
                setMBNAppState(context, true);
                context.startActivity(new Intent(context, MBNFileLoad.class)
                        .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            } else if (HOST_CODE_DISABLE.equals(host)) {
                // *#*#dconfig#*#* ?
                setMBNAppState(context, false);
            } else if (HOST_CODE_QENABLE.equals(host)) {
                // *#*#qmconfig#*#*
                setMBNAppState(context, true);
                context.startActivity(new Intent(context, MBNFileLoad.class)
                        .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            }
        }
    }

    private void setMBNAppState(Context context, boolean install) {
        if (context == null) {
            log("setMBNAppState, context null");
            return;
        }
        PackageManager pm = context.getPackageManager();
        if (pm == null) {
            log("setMBNAppState, PackageManager null");
            return;
        }

        ComponentName cn = new ComponentName(context.getPackageName(), context.getPackageName() +
                ".MBNFileLoad");

        int state = install ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED
                : PackageManager.COMPONENT_ENABLED_STATE_DISABLED;
        pm.setComponentEnabledSetting(cn, state, PackageManager.DONT_KILL_APP);
        SharedPreferences sp = context.getSharedPreferences(context.getPackageName(),
                Context.MODE_PRIVATE);

        sp.edit().putBoolean(APP_INSTALLED, install).apply();
    }

    private boolean getMBNAppStateFromSP(Context context) {
        if (context == null ) {
            log("getMBNAppStateFromSP, context null");
            return false;
        }
        SharedPreferences sp = context.getSharedPreferences(context.getPackageName(),
                Context.MODE_PRIVATE);
        return sp.getBoolean(APP_INSTALLED, false);
    }

    private void log(String msg) {
        Log.d(TAG, "MBNTest_ " + msg);
    }
}
