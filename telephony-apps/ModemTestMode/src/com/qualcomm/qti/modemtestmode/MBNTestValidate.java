/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.io.File;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.channels.FileChannel;

import android.app.Activity;
import android.app.Dialog;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.widget.ProgressBar;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.Gravity;
import android.widget.Toast;
import android.widget.Button;
import android.widget.TextView;
import android.widget.ExpandableListView;
import android.widget.BaseExpandableListAdapter;

import android.os.Bundle;
import android.os.Handler;
import android.os.Message;

import android.telephony.TelephonyManager;

public class MBNTestValidate extends Activity {
    private final String TAG = "MBNTestValidate";

    private static final int VALIDATION_INIT = 0;
    private static final int VALIDATION_IN_PROGRESS = 1;
    private static final int VALIDATION_DONE = 2;
    private static final int VALIDATION_GET_QV_FAIL = 3;

    private static final int EVENT_QCRIL_HOOK_READY = 1;
    private static final int EVENT_START_VALIDATE = 2;

    private MBNMetaInfo mMBNMetaInfo;

    private String mRefMBNPath;
    private String mRefMBNConfig;
    private Context mContext;

    private int[] mValidationStatus = null;
    private ValiExpaListAdapter[] mListExpListAdapter = null;
    private ExpandableListView mExpListView = null;
    private List<String>[] mListGroup = null;
    private HashMap<String, List<String>>[] mListChild = null;
    private Button mStartValidateButon[] = null;
    private int mCurValidateSub = 0;
    private int mPhoneCount = 0;

    private BroadcastReceiver sValidationReceiver = new BroadcastReceiver() {

        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            log("Received:" + action);
            if (MBNTestUtils.PDC_DATA_ACTION.equals(action)) {
                byte[] result = intent.getByteArrayExtra(MBNTestUtils.PDC_ACTIVATED);
                int sub = intent.getIntExtra(MBNTestUtils.SUB_ID, MBNTestUtils.DEFAULT_SUB);
                ByteBuffer payload = ByteBuffer.wrap(result);
                payload.order(ByteOrder.nativeOrder());
                int error = payload.get();
                log("Sub:" + sub + " activated:" + new String(result) + " error code:" + error);
                if (error == MBNTestUtils.LOAD_MBN_SUCCESS) {
                    Message msg = mHandler.obtainMessage(EVENT_START_VALIDATE);
                    msg.arg1 = mCurValidateSub;
                    msg.sendToTarget();
                } else {
                    //TODO need add a status for mValidationStatus
                    setOtherSubsClickable(mCurValidateSub);
                    mValidationStatus[mCurValidateSub] = VALIDATION_DONE;
                }
            } else if (MBNTestUtils.PDC_CONFIGS_VALIDATION_ACTION.equals(action)) {
                Bundle data = intent.getExtras();
                int sub = data.getInt("sub");
                int index = data.getInt("index");
                String nvItemInfo = data.getString("nv_item");
                String refValue= data.getString("ref_value");
                String curValue= data.getString("cur_value");
                log("----  index:" + index);
                if (index == -1) {
                    mValidationStatus[mCurValidateSub] = VALIDATION_DONE;
                    MBNPrompt.showToast(mContext, "Sub " + mCurValidateSub + " Validation Done");
                    setOtherSubsClickable(mCurValidateSub);
                } else {
                    addNVData(mCurValidateSub, nvItemInfo, refValue, curValue);
                }
            }
        }
    };

    // update UI
    private Handler mHandler = new Handler() {
        public void handleMessage(Message msg) {
            log("sub: "+msg.arg1);
            switch (msg.what) {
            case EVENT_QCRIL_HOOK_READY:
                MBNAppGlobals.getInstance().unregisterQcRilHookReady(mHandler);
                setActivityView();
                break;

            case EVENT_START_VALIDATE:
                log("EVENT_START_VALIDATE");
                MBNAppGlobals.getInstance().validateConfig(mRefMBNConfig, mCurValidateSub);
                break;

            default:
                log("wrong event");
                break;
            }
        }
    };

    private void setActivityView() {

        if (!handleMBNIdMetaInfo()) {
            // TODO need show alert.
            finish();
            return;
        }

        mPhoneCount = TelephonyManager.getDefault().getPhoneCount();

        mStartValidateButon = new Button[mPhoneCount];
        mValidationStatus = new int[mPhoneCount];
        for (int i = 0; i < mPhoneCount; i++) {
            mValidationStatus[i] = VALIDATION_INIT;
            int viewId = R.id.validate_sub1;
            if (i > 0) {
                viewId = R.id.validate_sub2;
            }
            mStartValidateButon[i] = (Button) findViewById(viewId);
            mStartValidateButon[i].setOnClickListener(
                    new Button.OnClickListener() {
                        public void onClick(View v) {
                            switch (v.getId()) {
                            case R.id.validate_sub1:
                                //mStartValidateButon[1].setClickable(false);
                                //mStartValidateButon[1].setBackgroundColor(0);
                                startMbnValidate(0);
                                break;
                            case R.id.validate_sub2:
                                //mStartValidateButon[0].setClickable(false);
                                //mStartValidateButon[0].setBackgroundColor(1);
                                startMbnValidate(1);
                                break;
                            }
                        }
                    });
            mStartValidateButon[i].setVisibility(View.VISIBLE);
        }
        setExpandableListView();
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.mbn_validate);
        mContext = this;
        MBNAppGlobals.getInstance().registerQcRilHookReady(mHandler,
                EVENT_QCRIL_HOOK_READY, null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        IntentFilter filter = new IntentFilter();
        filter.addAction(MBNTestUtils.PDC_DATA_ACTION);
        filter.addAction(MBNTestUtils.PDC_CONFIGS_VALIDATION_ACTION);
        registerReceiver(sValidationReceiver, filter);
    }

    @Override
    protected void onPause() {
        super.onPause();
        unregisterReceiver(sValidationReceiver);
    }

    private void setExpandableListView() {
        mExpListView = (ExpandableListView) findViewById(R.id.nv_diff_data);
        mListExpListAdapter = new ValiExpaListAdapter[mPhoneCount];
        mListGroup = new ArrayList[mPhoneCount];
        mListChild = new HashMap[mPhoneCount];
        for (int subId = 0; subId < mPhoneCount; subId++) {
            mListGroup[subId] = new ArrayList<String>();
            mListChild[subId] = new HashMap<String, List<String>>();
            mListExpListAdapter[subId] = new ValiExpaListAdapter(this, this, subId);
        }
    }

    public void onDestroy() {
        super.onDestroy();
    }

    private boolean otherSubValiateCheck(int sub, int event) {
        for (int i = 0; i < mPhoneCount; i++) {
            if (i != sub && mValidationStatus[i] == event) {
                log("mStatus[" + i + "]=" + mValidationStatus[i]);
                return true;
            }
        }
        return false;
    }

    private void setOtherSubsClickable(int sub) {
        // TODO need?
      /*for (int i = 0; i < mPhoneCount; i++) {
            if (i != sub) {
                mStartValidateButon[i].setClickable(true);
            }
        }*/
        return;
    }

    private void startMbnValidate(int sub) {
        log("startMbnValidate sub " + sub);
        if (otherSubValiateCheck(sub, VALIDATION_IN_PROGRESS)) {
            MBNPrompt.showToast(this, "Other sub is validating");
        } else if (mValidationStatus[sub] == VALIDATION_INIT) {
            MBNPrompt.showToast(this, "Begin to validate sub " + sub);
            mValidationStatus[sub] = VALIDATION_IN_PROGRESS;
            mExpListView.setAdapter(mListExpListAdapter[sub]);

            if (otherSubValiateCheck(sub, VALIDATION_GET_QV_FAIL)) {
                mValidationStatus[sub] = VALIDATION_GET_QV_FAIL;
                setOtherSubsClickable(sub);
                return;
            }

            if (otherSubValiateCheck(sub, VALIDATION_DONE)) {
                Message msg = mHandler.obtainMessage(EVENT_START_VALIDATE);
                msg.arg1 = sub;
                mCurValidateSub = sub;
                mHandler.sendMessage(msg);
            } else {
                if (loadRefMBN(sub)) {
                    return;
                } else {
                    mValidationStatus[sub] = VALIDATION_GET_QV_FAIL;
                }
            }
        } else if(mValidationStatus[sub] == VALIDATION_IN_PROGRESS) {
            MBNPrompt.showToast(this, "sub " + sub + " is validating");
        } else if(mValidationStatus[sub] == VALIDATION_DONE) {
            mExpListView.setAdapter(mListExpListAdapter[sub]);
        } else if (mValidationStatus[sub] == VALIDATION_GET_QV_FAIL) {
            MBNPrompt.showToast(this, "Get Qc Version Error");
        } else {
            log("Wrong status");
        }
        setOtherSubsClickable(sub);
    }

    private boolean handleMBNIdMetaInfo() {
        String mbnId = MBNAppGlobals.getInstance().getMBNConfig(0);
        String metaInfo = MBNAppGlobals.getInstance().getMetaInfoForConfig(mbnId);
        mMBNMetaInfo = new MBNMetaInfo(mbnId, metaInfo);
        log("Cur MBN: " + mMBNMetaInfo);
        //TODO need for all activated mbn validation?
        if (mbnId == null || metaInfo == null) {
            return false;
        }
        return true;
    }

    private void addNVData(int sub, String group, String ref, String cur) {
        mListGroup[sub].add(group);
        List<String> tmp = new ArrayList<String>();
        tmp.add("Ref:"+ref);
        tmp.add("Cur:"+cur);
        mListChild[sub].put(mListGroup[sub].get(mListGroup[sub].size()-1), tmp);
        mListExpListAdapter[sub].notifyDataSetChanged();
        //log("" + listGroup[sub] + "  " + listChild[sub] + " " + listGroup[sub]);
    }

    // TODO Don't like this method
    private void reverseDirToLoadRefMBN(File dir) {
        if (mRefMBNPath != null) {
            return;
        }
        try {
            File[] files = dir.listFiles();
            for (File file: files) {
                if (file.isDirectory()) {
                    reverseDirToLoadRefMBN(file);
                } else {
                    if (file.canRead() && file.getName().endsWith(".mbn")) {
                        String destName = file.getName();
                        // TODO need copy to somewhere
                        if (MBNTestUtils.mbnNeedToGo()) {
                            destName = MBNTestUtils.MBN_TO_GO_DIR+destName;
                            File dest = new File(destName);
                            try {
                                dest.delete();
                                dest.createNewFile();
                                Runtime.getRuntime().exec("chmod 644 " + destName);
                                Runtime.getRuntime().exec("chown radio.radio " + destName);
                            } catch (IOException e) {
                                log("can't create file:" + e);
                                continue;
                            }
                            log("copy " + file.getAbsolutePath() + " to " + dest.getAbsolutePath());
                            if (!MBNFileManager.copyWithChannels(file, dest, true)) {
                                MBNPrompt.showToast(mContext, mContext.getString(
                                        R.string.fail_to_copy_file));
                                continue;
                            }
                        } else {
                            destName = file.getCanonicalPath();
                        }

                        byte[] refQV = MBNAppGlobals.getInstance().getQcVersionOfFile(destName);
                        if (refQV == null || refQV.length != mMBNMetaInfo.getQcVersion().length) {
                            log("Cur QV length:" + mMBNMetaInfo.getQcVersion().length + "Ref QV:" +
                                    refQV + " path:" + destName);
                            continue;
                        }

                        int j = 0;
                        for (j = 0; j < mMBNMetaInfo.getQcVersion().length; j++) {
                            log("index:" + j + " Cur:" + mMBNMetaInfo.getQcVersion()[j] +
                                    " Ref:" + refQV[j]);
                            if (mMBNMetaInfo.getQcVersion()[j] != refQV[j]) {
                                break;
                            }
                        }

                        if (j == refQV.length) {
                            mRefMBNPath = destName;
                            log("Fild Golden MBN:" + mRefMBNPath);
                            return;
                        }
                    }
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    // TODO
    private boolean loadRefMBN(int sub) {
        // 1. get current mbn qc version
        byte[] qcVersion = MBNAppGlobals.getInstance().getQcVersionOfID(mMBNMetaInfo.getMBNId());
        if (qcVersion == null) {
            log("Fail to get QcVersion for " + mMBNMetaInfo.getMBNId());
            MBNPrompt.showToast(this, "Can't get activated mbn Qc Version");
            return false;
        }
        mMBNMetaInfo.setQcVersion(qcVersion);

        // 2. begin to get ref qc version and load, reverse a Golden Dir
        File file = new File(MBNTestUtils.getGoldenMBNPath());
        if (file.exists() && file.canRead()) {
            mRefMBNPath = null;
            reverseDirToLoadRefMBN(file);
            if (mRefMBNPath != null) {
                // 3. success, need load to modem.
                mRefMBNConfig = MBNTestUtils.GOLDEN_MBN_ID_PREFIX + mRefMBNPath;
                MBNAppGlobals.getInstance().setupMBNConfig(mRefMBNPath, mRefMBNConfig, 0);
                mCurValidateSub = sub;
                return true;
            }
        } else {
            MBNPrompt.showToast(mContext, "Fail to handle dir " + MBNTestUtils.getGoldenMBNPath());
            return false;
        }
        MBNPrompt.showToast(mContext, "Fail to find Golden MBN");
        log("Fail to find Golden MBN");
        return false;
    }

    private class ValiExpaListAdapter extends BaseExpandableListAdapter {
        private Activity mActivity;
        private int mSubId;

        public ValiExpaListAdapter(Context context, Activity activity, int sub) {
            mActivity = activity;
            mSubId = sub;
        }

        public Object getChild(int groupPosition, int childPosition) {
            String tmp = mListGroup[mSubId].get(groupPosition);
            return mListChild[mSubId].get(tmp).get(childPosition);
        }

        public long getChildId(int groupPosition, int childPosition) {
            return childPosition;
        }

        public int getChildrenCount(int groupPosition) {
            String tmp = mListGroup[mSubId].get(groupPosition);
            return mListChild[mSubId].get(tmp).size();
        }

        public View getChildView(int groupPosition, int childPosition,
                boolean isLastChild, View convertView, ViewGroup parent) {
            String childText = (String)getChild(groupPosition, childPosition);
            if (convertView == null) {
                convertView = mActivity.getLayoutInflater().inflate(
                        R.layout.list_child, null);
            }
            TextView tv = (TextView)convertView.findViewById(R.id.list_child);
            tv.setText(childText);
            return convertView;
        }

        public boolean isChildSelectable(int groupPosition, int childPosition) {
            return true;
        }

        public Object getGroup(int groupPosition) {
            return mListGroup[mSubId].get(groupPosition);
        }

        public int getGroupCount() {
            return mListGroup[mSubId].size();
        }

        public long getGroupId(int groupPosition) {
            return groupPosition;
        }

        public View getGroupView(int groupPosition, boolean isExpanded,
                View convertView, ViewGroup parent) {
            String groupTitle = (String) getGroup(groupPosition);
            if (convertView == null) {
                convertView = mActivity.getLayoutInflater().inflate(
                        R.layout.list_group, null);
            }
            TextView tv = (TextView) convertView.findViewById(R.id.list_group);
            tv.setText(groupTitle);
            return convertView;
        }

        public boolean hasStableIds() {
            return false;
        }
    }

    private void log(String msg) {
        Log.d(TAG, "MBNTest_ " + msg);
    }
}
