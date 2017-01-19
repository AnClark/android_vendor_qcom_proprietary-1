/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package com.gsma.services.nfc;

import java.util.HashMap;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.content.ComponentName;
import android.os.Handler;
import android.os.Message;
import android.os.Messenger;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import com.gsma.services.nfc.OffHostService;

public final class NfcController {

    private static final String TAG = "GsmaNfcController";
    final static boolean DBG = true;

    static HashMap<Context, NfcController> sNfcControllerMap = new HashMap<Context, NfcController>();

    private NfcController mNfcController;
    public Context mContext;
    private Callbacks mPendingCallbacks = null;
    MyConnection mConn = null;

    // Card Emulation mode has been disabled
    public static final int CARD_EMULATION_DISABLED = 0;
    // Card Emulation mode has been enabled
    public static final int CARD_EMULATION_ENABLED = 1;
    // a problem occured during the enabling/disabling process
    public static final int CARD_EMULATION_ERROR = 256;

    public interface Callbacks {
        // called when process for enabling the NFC Controller is finished
        public abstract void onEnableNfcController (boolean success);

        // called when process for enabling/disabling Card Emulation Mode is finished
        public abstract void onCardEmulationMode (int status);

        // called when process of getDefaultController is finished
        public abstract void onGetDefaultController (NfcController nfcController);
    }

    static final int MSG_RESULT_SUCCESS = 1;
    static final int MSG_RESULT_FAILED = 0;
    static final int MSG_RESULT_ENABLE_NFCC = 1;
    static final int MSG_RESULT_ENABLE_CE_MODE = 2;
    static final int MSG_RESULT_DISABLE_CE_MODE = 3;

    class msgHandler extends Handler {
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case MSG_RESULT_ENABLE_NFCC:
                {
                    boolean isEnabled = (msg.arg1 == 1 ? true : false);
                    if (DBG) Log.d(TAG, "MSG_RESULT_ENABLE_NFCC isEnabled = " + isEnabled);

                    if (mPendingCallbacks != null) {
                        mPendingCallbacks.onEnableNfcController (isEnabled);
                        mPendingCallbacks = null;
                    }
                    break;
                }
                case MSG_RESULT_ENABLE_CE_MODE:
                {
                    boolean isSuccess = (msg.arg1 == 1 ? true : false);
                    if (DBG) Log.d(TAG, "MSG_RESULT_ENABLE_CE_MODE isSuccess = " + isSuccess);

                    if (mPendingCallbacks != null) {
                        if (isSuccess)
                            mPendingCallbacks.onCardEmulationMode (CARD_EMULATION_ENABLED);
                        else
                            mPendingCallbacks.onCardEmulationMode (CARD_EMULATION_ERROR);
                        mPendingCallbacks = null;
                    }
                    break;
                }
                case MSG_RESULT_DISABLE_CE_MODE:
                {
                    boolean isSuccess = (msg.arg1 == 1 ? true : false);
                    if (DBG) Log.d(TAG, "MSG_RESULT_DISABLE_CE_MODE isSuccess = " + isSuccess);

                    if (mPendingCallbacks != null) {
                        if (isSuccess)
                            mPendingCallbacks.onCardEmulationMode (CARD_EMULATION_DISABLED);
                        else
                            mPendingCallbacks.onCardEmulationMode (CARD_EMULATION_ERROR);
                        mPendingCallbacks = null;
                    }
                    break;
                }
                default:
                    super.handleMessage(msg);
            }
        }
    }

    final Messenger mMessenger = new Messenger(new msgHandler());

    private volatile IGsmaService mGsmaService;
    boolean mIsBound = false;

    public class MyConnection implements ServiceConnection {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
            if (DBG) Log.d(TAG, "onServiceConnected");
            mGsmaService = IGsmaService.Stub.asInterface(service);
            if (mPendingCallbacks != null)
                mPendingCallbacks.onGetDefaultController(NfcController.this);
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

    public NfcController (Context context, NfcController.Callbacks listeners) {
        mContext=context;
        mPendingCallbacks = listeners;
        if (mConn == null ) {
            mConn = new MyConnection();
            bindService(context);
        }
    }

    // Helper to return the instance of the NFC Controller in a callback function
    // context : the callling application's context
    // cb : callback to use for the default controller
    public static synchronized void getDefaultController (Context context, NfcController.Callbacks cb) {
        if (DBG) Log.d(TAG, "getDefaultController()");

        NfcController nfcController = sNfcControllerMap.get(context);
        if (nfcController == null) {
            nfcController = new NfcController(context, cb);
            sNfcControllerMap.put(context, nfcController);
        }
    }

    // Return NFC Controller's state
    public boolean isEnabled() {
        if (DBG) Log.d(TAG, "isEnabled()");
        try {
            return mGsmaService.isNfccEnabled();
        } catch (RemoteException e) {
            Log.e(TAG, "mGsmaService.isNfccEnabled(): " + e.getMessage());
            return false;
        }
    }

    // asks the system to enable the NFC Controller and registers a callback function to run
    // when the activation is finished.
    // If the device doesn't support NFC, the callback will be called immediately.
    // User input is required to enable NFC. A question will be asked if user wants to enable
    // NFC or not. This question shall be generated within the OS context.
    public void enableNfcController (NfcController.Callbacks cb) {
        if (DBG) Log.d(TAG, "enableNfcController()");
        try {
            if (mGsmaService.enableNfcc(mMessenger) == false) {
                cb.onEnableNfcController (false);
            } else {
                // store callback and call when NFC controller enabling is finished
                mPendingCallbacks = cb;
            }
        } catch (RemoteException e) {
            Log.e(TAG, "mGsmaService.enableNfcc(): " + e.getMessage());
        }
    }

    // Returns Card Emulation Mode's state
    public boolean isCardEmulationEnabled() {
        if (DBG) Log.d(TAG, "isCardEmulationEnabled()");
        try {
            return mGsmaService.isCardEmulationEnabled();
        } catch (RemoteException e) {
            Log.e(TAG, "mGsmaService.isCardEmulationEnabled() : " + e.getMessage());
            return false;
        }
    }

    // asks the system to enable the "Card Emulation" mode and registers a callback function
    // to run when the process is finished. If the device does not support NFC, the callback
    // will be called immediately. If there is no user setting for card emulation on/off, the
    // change is not persistent and shall be overriden by the following events:
    //  - Turning off and on NFC
    //  - Full power cycle of the phone
    //  - Changing active SE
    // when UICC is the active SE, only applications signed with certificates from UICC are
    // granted to call this method. When eSE is the active SE, only applications signed with
    // system certificates are granted to call this method
    public void enableCardEmulationMode (NfcController.Callbacks cb) {
        if (DBG) Log.d(TAG, "enableCardEmulationMode()");
        try {
            if (mGsmaService.isNfccEnabled() == false) {
                // IllegalStateException - if NFC adaptor is not enabled
                throw new IllegalStateException("NFC adaptor is not enabled");
            } else {
                if (mGsmaService.isCardEmulationEnabled() == true) {
                    if (DBG) Log.d(TAG, "enableCardEmulationMode(): already enabled");
                    cb.onCardEmulationMode (CARD_EMULATION_ENABLED);
                    return;
                }
                if (mGsmaService.enableCardEmulationMode(mMessenger) == false) {
                    cb.onCardEmulationMode (CARD_EMULATION_ERROR);
                } else {
                    // store callback and call when NFC controller enabling is finished
                    mPendingCallbacks = cb;
                }
            }
        } catch (RemoteException e) {
            // SecurityException - if the application is not allowed to use this API
            throw new SecurityException("application is not allowed");
        }
    }

    // asks the system to disable the "Card Emulation" mode and registers a callback function
    // to run when the process is finished. If the device does not support NFC, the callback
    // will be called immediately.
    // when UICC is the active SE, only applications signed with certificates from UICC are
    // granted to call this method. When eSE is the active SE, only applications signed with
    // system certificates are granted to call this method
    public void disableCardEmulationMode (NfcController.Callbacks cb) {
        if (DBG) Log.d(TAG, "disableCardEmulationMode()");
        try {
            if (mGsmaService.isNfccEnabled() == false) {
                throw new IllegalStateException("NFC adaptor is not enabled");
            }
            if (mGsmaService.isCardEmulationEnabled() == false) {
                if (DBG) Log.d(TAG, "enableCardEmulationMode(): already disabled");
                cb.onCardEmulationMode (CARD_EMULATION_DISABLED);
                return;
            }
            if (mGsmaService.disableCardEmulationMode(mMessenger) == false) {
                cb.onCardEmulationMode (CARD_EMULATION_ERROR);
            } else {
                // store callback and call when NFC controller enabling is finished
                mPendingCallbacks = cb;
            }
        } catch (RemoteException e) {
            // SecurityException - if the application is not allowed to use this API
            throw new SecurityException("application is not allowed");
        }
    }

    //Create a new "Off-Host" service
    public OffHostService defineOffHostService(String description, String SEName) {
        Log.e(TAG, "Method not implemented");
        return null;
    }

    //Delete an existing dynamically created "Off-Host" service.
    public void deleteOffHostService(OffHostService service) {
        Log.e(TAG, "Method not implemented");
    }

    //Return the "Off-Host" service related to the current selected "Tap&Pay" menu entry.
    public OffHostService getDefaultOffHostService() {
        Log.e(TAG, "Method not implemented");
        return null;
    }

    //Return the list of "Off-Host" services belonging to the calling application
    public OffHostService[] getOffHostServices() {
        Log.e(TAG, "Method not implemented");
        return null;
    }
}

