/*
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package com.qcom.gsma.services.nfc;

import java.util.HashMap;
import com.gsma.services.nfc.*;
import com.gsma.services.nfc.IGsmaService;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;

import android.os.Message;
import android.os.Messenger;
import android.os.IBinder;
import android.os.RemoteException;
import android.app.Service;
import android.nfc.NfcAdapter;
import android.util.Log;
import java.util.ArrayList;
import com.android.qcom.nfc_extras.*;

public class GsmaService extends Service {

    private static final String TAG = "GsmaNfcService";
    final static boolean DBG = true;
    static NfcAdapter sNfcAdapter = null;
    private BroadcastReceiver mNfcAdapterEventReceiver;
    private Context mContext;
    static HashMap<Context, NfcQcomAdapter> sNfcQcomAdapterMap = new HashMap();

    static NfcQcomAdapter getNfcQcomAdapter (Context context) {
        return sNfcQcomAdapterMap.get(context);
    }

    static final String ENABLE_NFCC = "ENABLE_NFCC";
    static final String ENABLE_CE_MODE = "ENABLE_CE_MODE";
    static final String DISABLE_CE_MODE = "DISABLE_CE_MODE";

    static final int MSG_RESULT_SUCCESS = 1;
    static final int MSG_RESULT_FAILED = 0;
    static final int MSG_RESULT_ENABLE_NFCC = 1;
    static final int MSG_RESULT_ENABLE_CE_MODE = 2;
    static final int MSG_RESULT_DISABLE_CE_MODE = 3;

    static HashMap<String, Messenger> sClientMessengerMap = new HashMap();
    String pack = null;

    private void sendMsg(Messenger clientMessenger, int msgId, int result) {
        Message msg = Message.obtain(null, msgId, result, 0);
        try {
            clientMessenger.send(msg);
        } catch (RemoteException e) {
            Log.e(TAG, "clientMessenger.send() failed: " + e.getMessage());
        }
    }

    private void registerNfcAdapterEvent(String action, Messenger clientMessenger) {
        if (DBG) Log.d(TAG, "register NFC Adapter event for action:" + action);

        sClientMessengerMap.put(action, clientMessenger);

        if (mNfcAdapterEventReceiver != null) {
            return;
        }

        IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(NfcAdapter.ACTION_ADAPTER_STATE_CHANGED);
        //intentFilter.addAction(NfcAdapter.ACTION_ENABLE_NFC_ADAPTER_FAILED);
        intentFilter.addAction("com.android.qcom.nfc_extras.action.ENABLE_NFC_ADAPTER_FAILED");
        //intentFilter.addAction(NfcAdapter.ACTION_CARD_EMUALTION_ENABLED);
        intentFilter.addAction("com.android.qcom.nfc_extras.action.CARD_EMUALTION_ENABLED");
        //intentFilter.addAction(NfcAdapter.ACTION_ENABLE_CARD_EMULATION_FAILED);
        intentFilter.addAction("com.android.qcom.nfc_extras.action.ENABLE_CARD_EMULATION_FAILED");
        //intentFilter.addAction(NfcAdapter.ACTION_CARD_EMUALTION_DISABLED);
        intentFilter.addAction("com.android.qcom.nfc_extras.action.CARD_EMUALTION_DISABLED");
        //intentFilter.addAction(NfcAdapter.ACTION_DISABLE_CARD_EMUALTION_FAILED);
        intentFilter.addAction("com.android.qcom.nfc_extras.action.DISABLE_CARD_EMUALTION_FAILED");

        mNfcAdapterEventReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String action = intent.getAction();
                if (DBG) Log.d(TAG, "got intent " + action);
                if (action.equals(NfcAdapter.ACTION_ADAPTER_STATE_CHANGED)) {
                    int isEnabled;
                    int adapterState = intent.getIntExtra(NfcAdapter.EXTRA_ADAPTER_STATE, 1);
                    if (adapterState == NfcAdapter.STATE_TURNING_ON) {
                        if (DBG) Log.d(TAG, "NfcAdapter.STATE_TURNING_ON");
                        // wait for STATE_ON
                        return;
                    } else if (adapterState == NfcAdapter.STATE_ON) {
                        if (DBG) Log.d(TAG, "NfcAdapter.STATE_ON");
                        isEnabled = 1;
                    } else {
                        isEnabled = 0;
                    }

                    Messenger clientMessenger = sClientMessengerMap.remove(ENABLE_NFCC);
                    if (clientMessenger != null) {
                        sendMsg(clientMessenger, MSG_RESULT_ENABLE_NFCC, MSG_RESULT_SUCCESS);
                    }
                } else if (action.equals("com.android.qcom.nfc_extras.action.ENABLE_NFC_ADAPTER_FAILED")) {
                    if (DBG) Log.d(TAG, "Failed to enable NFC Controller");

                    Messenger clientMessenger = sClientMessengerMap.remove(ENABLE_NFCC);
                    if (clientMessenger != null) {
                        sendMsg(clientMessenger, MSG_RESULT_ENABLE_NFCC, MSG_RESULT_FAILED);
                    }
                } else if (action.equals("com.android.qcom.nfc_extras.action.CARD_EMUALTION_ENABLED")) {
                    if (DBG) Log.d(TAG, "Card Emulation Mode is enabled");

                    Messenger clientMessenger = sClientMessengerMap.remove(ENABLE_CE_MODE);
                    if (clientMessenger != null) {
                        sendMsg(clientMessenger, MSG_RESULT_ENABLE_CE_MODE, MSG_RESULT_SUCCESS);
                    }
                } else if (action.equals("com.android.qcom.nfc_extras.action.CARD_EMUALTION_DISABLED")) {
                    if (DBG) Log.d(TAG, "Card Emulation Mode is disabled");

                    Messenger clientMessenger = sClientMessengerMap.remove(DISABLE_CE_MODE);
                    if (clientMessenger != null) {
                        sendMsg(clientMessenger, MSG_RESULT_DISABLE_CE_MODE, MSG_RESULT_SUCCESS);
                    }
                } else if (action.equals("com.android.qcom.nfc_extras.action.ENABLE_CARD_EMULATION_FAILED")) {
                    if (DBG) Log.d(TAG, "Enable Card Emulation Mode failed");

                    Messenger clientMessenger = sClientMessengerMap.remove(ENABLE_CE_MODE);
                    if (clientMessenger != null) {
                        sendMsg(clientMessenger, MSG_RESULT_ENABLE_CE_MODE, MSG_RESULT_FAILED);
                    }
                } else if (action.equals("com.android.qcom.nfc_extras.action.DISABLE_CARD_EMUALTION_FAILED")) {
                    if (DBG) Log.d(TAG, "Disable Card Emulation Mode failed");

                    Messenger clientMessenger = sClientMessengerMap.remove(DISABLE_CE_MODE);
                    if (clientMessenger != null) {
                        sendMsg(clientMessenger, MSG_RESULT_DISABLE_CE_MODE, MSG_RESULT_FAILED);
                    }
                }

                // if no client is waiting
                if (sClientMessengerMap.isEmpty()) {
                    unregisterNfcAdapterEvent();
                } else if (DBG) {
                    Log.d(TAG, "sClientMessengerMap.size() = " + sClientMessengerMap.size());
                }
            }
        };
        mContext.registerReceiver(mNfcAdapterEventReceiver, intentFilter);
    }

    private void unregisterNfcAdapterEvent() {
        if (DBG) Log.d(TAG, "unregister NFC Adapter event");
        mContext.unregisterReceiver(mNfcAdapterEventReceiver);
        mNfcAdapterEventReceiver = null;
    }

    private IGsmaService.Stub mGsmaServiceBinder = new IGsmaService.Stub() {
        @Override
        public boolean isNfccEnabled() {
            if (DBG) Log.d(TAG, "isNfccEnabled()");

            if (sNfcAdapter != null) {
                return sNfcAdapter.isEnabled();
            } else {
                return false;
            }
        }

        @Override
        public boolean enableNfcc(Messenger clientMessenger) {
            if (DBG) Log.d(TAG, "enableNfcc()");

            if (sNfcAdapter != null) {
                try {
                    NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                    if (nfcQcomAdapter == null) {
                        if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                        return false;
                    }

                    // NfcQcomAdapter will ask user if user wants to enable NFC or not
                    if (nfcQcomAdapter.enableNfcController() == false) {
                        return false;
                    } else {
                        // store client messenger to notify when NFC controller enabling is finished
                        // wait for intent
                        if (sClientMessengerMap.get(ENABLE_NFCC) == null) {
                            registerNfcAdapterEvent(ENABLE_NFCC, clientMessenger);
                            return true;
                        } else {
                            Log.e(TAG, "Enabling NFCC is in progress");
                            return false;
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "enableNfcc() : " + e.getMessage());
                    return false;
                }
            } else {
                return false;
            }
        }

        @Override
        public boolean isCardEmulationEnabled() {
            if (DBG) Log.d(TAG, "isCardEmulationEnabled()");

            if ((sNfcAdapter != null)&&(sNfcAdapter.isEnabled())) {
                NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                if (nfcQcomAdapter == null) {
                    if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                    return false;
                }

                return (nfcQcomAdapter.isCardEmulationEnabled());
            } else {
                return false;
            }
        }

        @Override
        public boolean enableCardEmulationMode(Messenger clientMessenger) {
            if (DBG) Log.d(TAG, "enableCardEmulationMode()");

            try {
                NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                if (nfcQcomAdapter == null) {
                    if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                    return false;
                }

                if (nfcQcomAdapter.enableCardEmulationMode() == false) {
                    return false;
                } else {
                    // store client messenger to notify when NFC controller enabling is finished
                    // wait for intent
                    if (sClientMessengerMap.get(ENABLE_CE_MODE) == null) {
                        registerNfcAdapterEvent(ENABLE_CE_MODE, clientMessenger);
                        return true;
                    } else {
                        Log.e(TAG, "Enabling NFCC is in progress");
                        return false;
                    }
                }
            } catch (Exception e) {
                // SecurityException - if the application is not allowed to use this API
                throw new SecurityException("application is not allowed");
            }
        }

        @Override
        public boolean disableCardEmulationMode(Messenger clientMessenger) {
            if (DBG) Log.d(TAG, "disableCardEmulationMode()");

            try {
                NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                if (nfcQcomAdapter == null) {
                    if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                    return false;
                }

                if (nfcQcomAdapter.disableCardEmulationMode() == false) {
                    return false;
                } else {
                    // store client messenger to notify when NFC controller enabling is finished
                    // wait for intent
                    if (sClientMessengerMap.get(DISABLE_CE_MODE) == null) {
                        registerNfcAdapterEvent(DISABLE_CE_MODE, clientMessenger);
                        return true;
                    } else {
                        Log.e(TAG, "Enabling NFCC is in progress");
                        return false;
                    }
                }
            } catch (Exception e) {
                // SecurityException - if the application is not allowed to use this API
                throw new SecurityException("application is not allowed");
            }
        }

        @Override
        public String getActiveSecureElement() {
            if (DBG) Log.d(TAG, "getActiveSecureElement()");
            try {
                NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                if (nfcQcomAdapter == null) {
                    if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                    return null;
                }
                return nfcQcomAdapter.getActiveSecureElement();
            } catch (Exception e) {
                Log.e(TAG, "getActiveSecureElement() : " + e.getMessage());
            }
            return null;
        }

        @Override
        public void setActiveSecureElement(String SEName) {
            if (DBG) Log.d(TAG, "setActiveSecureElement() " + SEName);
            try {
                NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                if (nfcQcomAdapter == null) {
                    if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                    return;
                }
                nfcQcomAdapter.setActiveSecureElement(SEName);
            } catch (Exception e) {
                Log.e(TAG, "setActiveSecureElement() : " + e.getMessage());
                // SecurityException - if the application is not allowed to use this API
                throw new SecurityException("application is not allowed");
            }
        }

        @Override
        public void mgetPname(String packageN) {
            pack = packageN;
        }

        @Override
        public void enableMultiReception(String SEName) {
            if (DBG) Log.d(TAG, "enableMultiReception() " + SEName);
            try {
                NfcQcomAdapter nfcQcomAdapter = getNfcQcomAdapter (getApplicationContext());
                if (nfcQcomAdapter == null) {
                    if (DBG) Log.d(TAG, "cannot get NfcQcomAdapter");
                    return;
                }
                nfcQcomAdapter.GsmaPack(pack);
                nfcQcomAdapter.enableMultiReception(SEName);
            } catch (Exception e) {
                Log.e(TAG, "enableMultiReception() : " + e.getMessage());
                // SecurityException - if the application is not allowed to use this API
                throw new SecurityException("application is not allowed");
            }
        }
    };

    @Override
    public void onCreate() {
        Log.d(TAG,"service created");
        final Context context = getApplicationContext();
        if (sNfcAdapter == null)
            sNfcAdapter = NfcAdapter.getDefaultAdapter(context);
        Log.d(TAG,"NfcAdapter acquired");
        new Thread(){
            public void run() {
                for(int tries =0; tries<3; tries++) {
                    try {
                        NfcQcomAdapter nfcQcomAdapter = sNfcQcomAdapterMap.get(context);
                        if (nfcQcomAdapter == null) {
                            nfcQcomAdapter = NfcQcomAdapter.getNfcQcomAdapter(context);
                            sNfcQcomAdapterMap.put(context, nfcQcomAdapter);
                        }
                        Log.d(TAG,"NfcQcomAdapter acquired");
                        return;
                    } catch (UnsupportedOperationException e) {
                        String errorMsg = "GSMA service gracefully failing to acquire NfcQcomAdapter at boot. try" + tries;
                        Log.e(TAG, errorMsg);
                        new Throwable(TAG + ": " + errorMsg, e);
                        e.printStackTrace();
                    }
                    try {
                        if(DBG) Log.d(TAG,"Waiting for QcomAdapter");

                        wait(5000);
                    } catch (Exception e) {
                        if(DBG) Log.d(TAG,"Interupted while waiting for QcomAdapter. by" + e);
                    }
                }
            }
        }.start();
    }

    @Override
    public IBinder onBind(Intent intent) {
        if (IGsmaService.class.getName().equals(intent.getAction())) {
            return mGsmaServiceBinder;
        }
    return null;
    }
}
