/* Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

package org.codeaurora.ims;

import android.os.AsyncResult;
import android.os.Handler;
import android.os.Message;
import android.telephony.TelephonyManager;
import android.util.Log;

import com.android.ims.ImsConfig;
import com.android.ims.ImsConfigListener;
import com.android.ims.internal.IImsConfig;

public class ImsConfigImpl extends IImsConfig.Stub {

    private static final String TAG = "ImsConfigImpl";
    private static final int EVENT_SET_VT_CALL_QUALITY = 1;
    private static final int EVENT_QUERY_VT_CALL_QUALITY = 2;
    private static final int EVENT_SET_FEATURE_VALUE = 3;

    private ImsSenderRxr mCi;
    private Handler mHandler = new ImsConfigImplHandler();

    /**
     * Creates the Ims Config interface object for a sub.
     * @param senderRxr
     */
    public ImsConfigImpl(ImsSenderRxr senderRxr) {
        mCi = senderRxr;
    }

    /* Wrapper class to encapsulate the arguments and listener to the setFeatureValue and
     * getFeatureValue APIs
     */
    private static final class FeatureAccessWrapper {
        public int feature;
        public int network;
        public int value;
        public ImsConfigListener listener;
        public FeatureAccessWrapper(int feature, int network, int value,
                ImsConfigListener listener) {
            this.feature = feature;
            this.network = network;
            this.listener = listener;
            this.value = value;
        }
    }

    //Handler for tracking requests sent to ImsSenderRxr.
    private class ImsConfigImplHandler extends Handler {

        @Override
        public void handleMessage(Message msg) {
            Log.d(TAG, "Message received: what = " + msg.what);
            AsyncResult ar = (AsyncResult) msg.obj;

            switch (msg.what) {
                case EVENT_QUERY_VT_CALL_QUALITY:
                    onGetVideoCallQualityDone(getImsConfigListener(ar), ar);
                    break;
                case EVENT_SET_VT_CALL_QUALITY:
                    onSetVideoCallQualityDone(getImsConfigListener(ar), ar);
                    break;
                case EVENT_SET_FEATURE_VALUE:
                    onSetFeatureResponseDone(getImsConfigListener(ar), ar);
                    break;
                default:
                    Log.e(TAG, "handleMessage: unhandled message");
            }

        }

    }

    private void onGetVideoCallQualityDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        if (imsConfigListener != null) {
            try {
                int status = getOperationStatus(ar.exception == null);
                int result = ar.result == null ?
                        ImsConfig.OperationValuesConstants.VIDEO_QUALITY_UNKNOWN :
                        (int)ar.result;
                imsConfigListener.onGetVideoQuality(status, result);
            } catch (Throwable t) {
                Log.e(TAG, "onGetVideoCallQualityDone " + t);
            }
        } else {
            Log.e(TAG, "onGetVideoCallQualityDone listener is not valid !!!");
        }
    }

    private void onSetVideoCallQualityDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        if (imsConfigListener != null) {
            try {
                int status = getOperationStatus(ar.exception == null);
                imsConfigListener.onSetVideoQuality(status);
            } catch (Throwable t) {
                Log.e(TAG, "onSetVideoCallQualityDone " + t);
            }
        } else {
            Log.e(TAG, "onSetVideoCallQualityDone listener is not valid !!!");
        }
    }

    private void onGetPacketCountDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        //Dummy implementation
    }

    private void onGetPacketErrorCountDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        //Dummy implementation
    }

    private void onGetWifiCallingStatusDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        //Dummy implementation
    }

    private void onSetWifiCallingStatusDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        //Dummy implementation
    }

    private void onSetFeatureResponseDone(ImsConfigListener imsConfigListener, AsyncResult ar) {
        if (imsConfigListener != null) {
            try {
                int status = getOperationStatus(ar.exception == null);
                FeatureAccessWrapper f = (FeatureAccessWrapper)ar.userObj;
                imsConfigListener.onSetFeatureResponse(f.feature, f.network, f.value, status);
            } catch (Throwable t) {
                Log.e(TAG, "onSetFeatureResponseDone " + t);
            }
        } else {
            Log.e(TAG, "onSetFeatureResponseDone listener is not valid !!!");
        }
    }

    private int getOperationStatus(boolean status) {
        return status ? ImsConfig.OperationStatusConstants.SUCCESS :
                ImsConfig.OperationStatusConstants.FAILED;
    }

    private ImsConfigListener getImsConfigListener(AsyncResult ar) {
        if (ar == null ) {
            Log.e(TAG, "AsyncResult is null.");
        } else if (ar.userObj instanceof ImsConfigListener) {
            return (ImsConfigListener)ar.userObj;
        } else if (ar.userObj instanceof FeatureAccessWrapper &&
                ((FeatureAccessWrapper)(ar.userObj)).listener instanceof ImsConfigListener) {
            return (ImsConfigListener)((FeatureAccessWrapper)(ar.userObj)).listener;
        }

        Log.e(TAG, "getImsConfigListener returns null");
        return null;
    }

    /**
     * Query for current video call quality.
     */
    @Override
    public void getVideoQuality(ImsConfigListener imsConfigListener) {
        Log.d(TAG, "getVideoQuality");
        mCi.queryVideoQuality(mHandler.obtainMessage(EVENT_QUERY_VT_CALL_QUALITY,
                imsConfigListener));
    }

    /**
     * Set for current video call quality.
     */
    @Override
    public void setVideoQuality(int quality, ImsConfigListener imsConfigListener) {
        Log.d(TAG, "setVideoQuality qualiy = " + quality);
        mCi.setVideoQuality(quality, mHandler.obtainMessage(EVENT_SET_VT_CALL_QUALITY,
                imsConfigListener));
    }

    @Override
    public void getPacketCount(ImsConfigListener listener) {
        //Dummy implementation
    }

    @Override
    public void getPacketErrorCount(ImsConfigListener listener) {
        //Dummy implementation
    }

    /**
     * Query for current wifi calling info.
     */
    //TODO - Compilation stub @Override
    public void getWifiCallingPreference(ImsConfigListener imsConfigListener) {
        //Dummy implementation
    }

    /**
     * Set current wifi calling info.
     */
    //TODO - Compilation stub @Override
    public void setWifiCallingPreference(int wifiCallingStatus, int wifiCallingPreference,
            ImsConfigListener imsConfigListener) {
        //Dummy implementation
    }

    // All below functions need to be implemented on demand basis
    //TODO: Remove Comment added for bringup @Override
    public int getMasterValue(int item) {
        //Dummy implementation
        return 0;
    }

    //TODO: Remove Comment added for bringup @Override
    public String getMasterStringValue(int item) {
        //Dummy implementation
        return null;
    }

    public int getProvisionedValue(int item) {
        int retval = 0;
        return retval;
    }

    public String getProvisionedStringValue(int item) {
        String retval = "Unkown";
        return retval;
    }

    @Override
    public int setProvisionedValue(int item, int value) {
        //Dummy implementation
        return 0;
    }

    @Override
    public int setProvisionedStringValue(int item, String value) {
        //Dummy implementation
        return 0;
    }

    @Override
    public void getFeatureValue(int feature, int network, ImsConfigListener listener) {
        //Dummy implementation
    }

    @Override
    public boolean getVolteProvisioned() {
        //TODO: Dummy implementation added for bringup
        return true;
    }

    @Override
    public void setFeatureValue(int feature, int network, int value, ImsConfigListener listener) {
        int srvType = ImsQmiIF.CALL_TYPE_VOICE;
        if (feature == ImsConfig.FeatureConstants.FEATURE_TYPE_VIDEO_OVER_LTE) {
            srvType = ImsQmiIF.CALL_TYPE_VT;
        }
        int enabled = ImsQmiIF.STATUS_DISABLED;
        if (value == ImsConfig.FeatureValueConstants.ON) {
            enabled = ImsQmiIF.STATUS_ENABLED;
        }
        int act = ImsQmiIF.RADIO_TECH_LTE;
        if (network == TelephonyManager.NETWORK_TYPE_IWLAN) {
            act = ImsQmiIF.RADIO_TECH_IWLAN;
        }
        if (network == TelephonyManager.NETWORK_TYPE_LTE ||
                network == TelephonyManager.NETWORK_TYPE_IWLAN) {
            Log.d(TAG, "SetServiceStatus = " + srvType + " " + network + " " + enabled);
            mCi.setServiceStatus(mHandler.obtainMessage(EVENT_SET_FEATURE_VALUE,
                new FeatureAccessWrapper(feature, network, value, listener)),
                srvType, act, enabled, 0);
        }
    }
}
