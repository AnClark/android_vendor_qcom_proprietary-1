/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.net.ConnectivityManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.RemoteException;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;

import com.android.internal.telephony.Phone;
import static com.android.internal.telephony.PhoneConstants.MAX_PHONE_COUNT_TRI_SIM;
import com.android.internal.telephony.TelephonyProperties;
import com.qualcomm.qti.modemtestmode.IMBNTestService;
import android.telephony.TelephonyManager;

public class MBNFileActivate extends Activity {
    private final String TAG = "MBNFileActivate";
    private final int EVENT_QCRIL_HOOK_READY = 1;
    private final int EVENT_DISMISS_REBOOT_DIALOG = 2;
    private final int NEED_REBOOT = 1;

    private MBNMetaInfo mCurMBN = null;
    private ArrayList<MBNMetaInfo> mMBNMetaInfoList = null;

    private HashMap<String, String> mCarrierNetworkModeMap = null;

    private ListView mMetaListView = null;
    private MetaInfoListAdapter mMetaAdapter = null;
    private int mMetaInfoChoice = 0;
    private IMBNTestService mMBNTestService = null;
    private boolean mIsCommercial = false;
    private boolean mIsBound = false;
    private ConnectivityManager mConnService;
    private TelephonyManager mTelephonyManager;
    private ProgressDialog mProgressDialog;

    private BroadcastReceiver mBroadcastReceiver = new BroadcastReceiver() {

        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            log("Received: " + action);
            if (MBNTestUtils.PDC_DATA_ACTION.equals(action)) {
                byte[] result = intent.getByteArrayExtra(MBNTestUtils.PDC_ACTIVATED);
                int sub = intent.getIntExtra(MBNTestUtils.SUB_ID, MBNTestUtils.DEFAULT_SUB);
                ByteBuffer payload = ByteBuffer.wrap(result);
                payload.order(ByteOrder.nativeOrder());
                int error = payload.get();
                log("Sub:" + sub + " activated:" + new String(result) + " error code:" + error);
                int what = EVENT_DISMISS_REBOOT_DIALOG;
                int needReboot = 0;
                if (error == MBNTestUtils.LOAD_MBN_SUCCESS) {
                    needReboot = NEED_REBOOT;
                }
                Message msg = mHandler.obtainMessage(what);
                msg.arg1 = needReboot;
                msg.sendToTarget();
            }
        }

    };

    private Handler mHandler = new Handler() {
        @Override
        public void dispatchMessage(Message msg) {
            switch (msg.what) {
            case EVENT_QCRIL_HOOK_READY:
                log("EVENT_QCRIL_HOOK_READY");
                MBNAppGlobals.getInstance().unregisterQcRilHookReady(mHandler);
                setActivityView();
                //TODO need set View here.
                break;
            case EVENT_DISMISS_REBOOT_DIALOG:
                log("EVENT_DISMISS_REBOOT_DIALOG");
                //Only set config when activating successfully.
                if (msg.arg1 == NEED_REBOOT) {
                    setConfig();
                    MBNTestUtils.rebootSystem(MBNFileActivate.this);
                } else {
                    mProgressDialog.dismiss();
                    MBNPrompt.showToast(MBNFileActivate.this, "Failed to activate MBN");
                }
                break;
            default:
                break;
            }
        }
    };

    private ServiceConnection mMBNServiceConnection = new ServiceConnection() {

        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            mMBNTestService = (IMBNTestService) IMBNTestService.Stub.asInterface(service);
            mIsBound = true;
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            mMBNTestService = null;
            mIsBound = false;
        }

    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.mbn_activate);
        mTelephonyManager = (TelephonyManager) this.getSystemService(Context.TELEPHONY_SERVICE);
        bindService(new Intent(MBNFileActivate.this, MBNTestService.class),
                mMBNServiceConnection, Context.BIND_AUTO_CREATE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        MBNAppGlobals.getInstance().registerQcRilHookReady(mHandler, EVENT_QCRIL_HOOK_READY, null);
        // Other Activity is also using the broadcast.
        IntentFilter filter = new IntentFilter();
        filter.addAction(MBNTestUtils.PDC_DATA_ACTION);
        registerReceiver(mBroadcastReceiver, filter);
    }

    @Override
    protected void onPause() {
        super.onPause();
        unregisterReceiver(mBroadcastReceiver);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unbindService(mMBNServiceConnection);
    }

    private void setActivityView() {
        if (getMBNInfo() == false) {
            log("Failed to get MBN info");
            return;
        }

        mCarrierNetworkModeMap = new HashMap<String, String>();
        String[] carriers = getResources().getStringArray(R.array.carriers);
        String[] networkMode = getResources().getStringArray(R.array.carriers_network_mode);
        for (int i = 0; i < Math.min(carriers.length, networkMode.length); i++) {
            mCarrierNetworkModeMap.put(carriers[i].toLowerCase(), networkMode[i]);
        }
        TextView mbnIdTextView = (TextView) findViewById(R.id.mbn_id);
        mbnIdTextView.setText("Meta: " + mCurMBN.getMetaInfo() +
                "\nDevice Type: " + mCurMBN.getDeviceType() +
                "\nMulti-Mode:" + mCurMBN.getMultiMode() +
                "\nCarrier: " + mCurMBN.getCarrier() +
                "\nSource: " + mCurMBN.getSourceString());
        Button mbnValidate = (Button) findViewById(R.id.mbn_validate);
        mbnValidate.setOnClickListener(new Button.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent intent = new Intent(MBNFileActivate.this, MBNTestValidate.class);
                startActivity(intent);
            }
        });

        if (mCurMBN.getMBNId() != null) {
            mbnValidate.setVisibility(View.VISIBLE);
        }

        mMetaListView = (ListView) findViewById(R.id.meta_info_list);
        mMetaAdapter = new MetaInfoListAdapter(this, R.layout.meta_info_list, mMBNMetaInfoList);
        mMetaListView.setAdapter(mMetaAdapter);
        mMetaListView.setChoiceMode(ListView.CHOICE_MODE_SINGLE);

        if (mCurMBN.getMetaInfo() != null) {
            for (int i = 0; i < mMBNMetaInfoList.size(); i++) {
                if (mMBNMetaInfoList.get(i).getMBNId().equals(mCurMBN.getMBNId())) {
                    mMetaInfoChoice = i;
                    break;
                }
            }
            //log("choice:---- :" + mMetaInfoChoice);
        }
        mMetaListView.setItemChecked(mMetaInfoChoice, true);
        mMetaListView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view,
                    int position, long id) {
                mMetaInfoChoice = position;
            }
        });

        Button exitButton = (Button) findViewById(R.id.exit);
        exitButton.setVisibility(View.VISIBLE);
        exitButton.setOnClickListener(new Button.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });
        Button activateButton = (Button) findViewById(R.id.activate);
        activateButton.setVisibility(View.VISIBLE);
        activateButton.setOnClickListener(new Button.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (isSameWithActivated()) {
                    MBNPrompt.showToast(MBNFileActivate.this,
                            "Currently the activated MBN is " + mCurMBN.getMetaInfo());
                } else {
                    showActivatingDialog();
                }
            }
        });
    }

    private void showActivatingDialog() {
        Dialog dialog = new ActivateDialog(MBNFileActivate.this);
        dialog.setTitle(R.string.activate_hint);
        dialog.setCanceledOnTouchOutside(false);
        dialog.show();
    }

    private class ActivateDialog extends Dialog implements View.OnClickListener {
        private Button mCancelBtn, mRebootBtn;

        public ActivateDialog(Context context) {
            super(context);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            setContentView(R.layout.activate_dialog);

            MBNMetaInfo tmp = mMBNMetaInfoList.get(mMetaInfoChoice);
            TextView mbnIdTV = (TextView) findViewById(R.id.mbn_id);
            mbnIdTV.setText("Meta: " + tmp.getMetaInfo());

            TextView sourceTV = (TextView) findViewById(R.id.mbn_source);
            sourceTV.setText("Source: " + tmp.getSourceString());

            TextView mobileData = (TextView) findViewById(R.id.mobile_data);
            TextView roamingData = (TextView) findViewById(R.id.data_roaming);
            if (tmp.getMetaInfo().toLowerCase().contains("commercial")) {
                mIsCommercial = true;
                mobileData.setVisibility(View.GONE);
                roamingData.setVisibility(View.GONE);
            }

            mCancelBtn = (Button) findViewById(R.id.cancel);
            mCancelBtn.setOnClickListener(this);

            mRebootBtn = (Button) findViewById(R.id.confirm);
            mRebootBtn.setOnClickListener(this);
        }

        @Override
        public void onClick(View v) {
            switch (v.getId()) {
            case R.id.cancel:
                dismiss();
                break;
            case R.id.confirm:
                showProgressDialog();
                int subMask = 1;
                if (mMBNMetaInfoList.get(mMetaInfoChoice).getMultiModeNumber() == 2) {
                    subMask = 1 + 2;
                }
                MBNAppGlobals.getInstance().selectConfig(mMBNMetaInfoList.
                        get(mMetaInfoChoice).getMBNId(), subMask);
                break;
            }
        }
    }

    private void showProgressDialog() {
        mProgressDialog = new ProgressDialog(this);
        mProgressDialog.setMessage(getResources().getString(R.string.Activating_hint));
        mProgressDialog.setCancelable(false);
        mProgressDialog.show();
    }

    private boolean isSameWithActivated() {
        if (mMBNMetaInfoList.get(mMetaInfoChoice).getMBNId().equals(mCurMBN.getMBNId())) {
            return true;
        }
        return false;
    }

    private void setConfig() {
        setNetworkMode(mMBNMetaInfoList.get(mMetaInfoChoice).getCarrier(),
                mMBNMetaInfoList.get(mMetaInfoChoice).getMultiModeNumber());
        try {
            mMBNTestService.setProperty(TelephonyProperties.PROPERTY_MULTI_SIM_CONFIG,
                    mMBNMetaInfoList.get(mMetaInfoChoice).getMultiMode().toLowerCase());
        } catch (RemoteException e) {
        }

        setupData(mIsCommercial);
    }

    private boolean getMBNInfo() {

        String mbn = MBNAppGlobals.getInstance().getMBNConfig(0);
        String meta = MBNAppGlobals.getInstance().getMetaInfoForConfig(mbn);
        mCurMBN = new MBNMetaInfo(mbn, meta);
        log("Current MBN:" + mCurMBN);

        String[] availableMBNIds = MBNAppGlobals.getInstance().getAvailableConfigs(null);
        if (availableMBNIds == null || availableMBNIds.length == 0) {
            log("Failed to get availableConfigs");
            showConfigPrompt();
            return false;
        }

        mMBNMetaInfoList = new ArrayList<MBNMetaInfo>();
        for (int i = 0; i < availableMBNIds.length; i++) {
            String metaInfo = MBNAppGlobals.getInstance().getMetaInfoForConfig(availableMBNIds[i]);
            log("Meta Info:" + metaInfo + " MBN ID:" + availableMBNIds[i]);
            if (metaInfo == null) {
                // When META is null, no need to add list.
                continue;
            }
            MBNMetaInfo tmp = new MBNMetaInfo(availableMBNIds[i], metaInfo);
            mMBNMetaInfoList.add(tmp);
        }

        if (mMBNMetaInfoList.size() == 0) {
            log("All meta info are not correct");
            return false;
        }
        // TODO need sort MBNMetaInfo
        return true;
    }

    private void showConfigPrompt() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setMessage(R.string.check_config_hint)
        .setPositiveButton(R.string.ok, new DialogInterface.OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                finish();
            }
        });
        builder.show();
    }

    public void setNetworkMode(String carrier, int multiMode) {
        log("carrier:" + carrier + " maps:" + mCarrierNetworkModeMap);
        if (mCarrierNetworkModeMap.containsKey(carrier.toLowerCase())) {
            String[] nw = mCarrierNetworkModeMap.get(carrier.toLowerCase()).split(",");
            int nw1, nw2;
            if (nw.length >= 2) {
                nw1 = Integer.parseInt(nw[0]);
                nw2 = Integer.parseInt(nw[1]);
            } else {
                // set to default mode
                nw1 = Phone.NT_MODE_WCDMA_PREF;
                nw2 = Phone.NT_MODE_GSM_ONLY;
            }
            if (multiMode == 2) {
                log("MultiMode, NetworkMode Sub1:" + nw1 + " Sub2:" + nw2);
                TelephonyManager.putIntAtIndex(this.getContentResolver(),
                        Settings.Global.PREFERRED_NETWORK_MODE, 0, nw1);
                TelephonyManager.putIntAtIndex(this.getContentResolver(),
                        Settings.Global.PREFERRED_NETWORK_MODE, 1, nw2);
            } else {
                log("SSSS, NetworkMode Sub1:" + nw1);
                Settings.Global.putInt(this.getContentResolver(),
                        Settings.Global.PREFERRED_NETWORK_MODE, nw1);
            }
        }
    }

    public void setupData(boolean isCommercial) {
        if (isCommercial) {
            setMobileData(true);
            setDataRoaming(true);
        } else {
            setMobileData(false);
            setDataRoaming(false);
        }
    }

    private void setMobileData(boolean enable){
        log("setMobileData enable:" + enable);
        // Use Default subscription
        mTelephonyManager.setDataEnabled(enable);
        for (int i = 0; i < MAX_PHONE_COUNT_TRI_SIM; i++) {
            Settings.Global.putInt(getContentResolver(),
                    Settings.Global.MOBILE_DATA + i, enable ? 1 : 0);
        }
    }

    private void setDataRoaming(boolean enable) {
        log("setDataRoaming enable:" + enable);
        Settings.Global.putInt(getContentResolver(),
                Settings.Global.DATA_ROAMING, enable ? 1 : 0);
        for (int i = 0; i < MAX_PHONE_COUNT_TRI_SIM; i++) {
            Settings.Global.putInt(getContentResolver(),
                    Settings.Global.DATA_ROAMING + i, enable ? 1 : 0);
        }
    }

    private void log(String msg) {
        Log.d(TAG, "MBNTest_" + msg);
    }

}
