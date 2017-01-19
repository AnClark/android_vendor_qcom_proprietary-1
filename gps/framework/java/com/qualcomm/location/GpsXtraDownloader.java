/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*
  Copyright (c) 2013 Qualcomm Atheros, Inc.
  All Rights Reserved.
  Qualcomm Atheros Confidential and Proprietary.

  Not a Contribution, Apache license notifications and
  license are retained for attribution purposes only.
=============================================================================*/

/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.qualcomm.location;
import android.content.Context;
import android.net.Proxy;
import android.util.Log;
import android.os.Build;
import android.telephony.TelephonyManager;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.util.Properties;
import java.util.Random;
import java.net.InetSocketAddress;

/**
 * A class for downloading GPS XTRA data.
 *
 * {@hide}
 */
public class GpsXtraDownloader {

    private static final String TAG = "GpsXtraDownloader";
    static final boolean DEBUG = Log.isLoggable(TAG, Log.VERBOSE);

    private Context mContext;
    private String[] mXtraServers;
    // to load balance our server requests
    private int mNextServerIndex;

    GpsXtraDownloader(Context context, Properties properties) {
        mContext = context;

        // read XTRA servers from the Properties object
        int count = 0;
        String server1 = properties.getProperty("XTRA_SERVER_1");
        String server2 = properties.getProperty("XTRA_SERVER_2");
        String server3 = properties.getProperty("XTRA_SERVER_3");
        if (server1 != null) count++;
        if (server2 != null) count++;
        if (server3 != null) count++;

        if (count == 0) {
            Log.e(TAG, "No XTRA servers were specified in the GPS configuration");
            return;
        } else {
            mXtraServers = new String[count];
            count = 0;
            if (server1 != null) mXtraServers[count++] = server1;
            if (server2 != null) mXtraServers[count++] = server2;
            if (server3 != null) mXtraServers[count++] = server3;

            // randomize first server
            Random random = new Random();
            mNextServerIndex = random.nextInt(count);
        }
    }

    byte[] downloadXtraData() {
        String proxyHost = Proxy.getHost(mContext);
        int proxyPort = Proxy.getPort(mContext);
        boolean useProxy = (proxyHost != null && proxyPort != -1);
        byte[] result = null;
        int startIndex = mNextServerIndex;

        if (mXtraServers == null) {
            return null;
        }

        // load balance our requests among the available servers
        while (result == null) {
            result = doDownload(mXtraServers[mNextServerIndex], useProxy, proxyHost, proxyPort);

            // increment mNextServerIndex and wrap around if necessary
            mNextServerIndex++;
            if (mNextServerIndex == mXtraServers.length) {
                mNextServerIndex = 0;
            }
            // break if we have tried all the servers
            if (mNextServerIndex == startIndex) break;
        }

        return result;
    }

    protected byte[] doDownload(String url, boolean isProxySet,
                                String proxyHost, int proxyPort) {
        if (DEBUG) Log.d(TAG, "Downloading XTRA data from " + url);

        TelephonyManager phone = (TelephonyManager)mContext.getSystemService(
                                                   mContext.TELEPHONY_SERVICE);
        //AndroidHttpClient client = null;
        URL Url = null;
        HttpURLConnection connection = null;
	/*if (isProxySet) {
	InetSocketAddress proxyInet = new InetSocketAddress(proxyHost,proxyPort);
	Proxy proxy = new Proxy(Proxy.Type.HTTP, proxyInet);
	}*/

        try {
	    Url = new URL(url);
            /*if (isProxySet)
            connection = (HttpURLConnection) Url.openConnection(proxy);
            else */
            connection = (HttpURLConnection) Url.openConnection();
            connection.setRequestMethod("GET");

	    connection.setRequestProperty("Accept", "*/*, application/vnd.wap.mms-message, application/vnd.wap.sic");
            connection.setRequestProperty("x-wap-profile", "http://www.openmobilealliance.org/tech/profiles/UAPROF/ccppschema-20021212#");

            String networkOperatorName = phone.getNetworkOperatorName();
            if (networkOperatorName == null) {
                networkOperatorName = "-";
            }
            String ua = "A/" + Build.VERSION.RELEASE +
                         "/" + Build.MANUFACTURER +
                         "/" + Build.MODEL +
                         "/" + Build.BOARD +
                         "/" + networkOperatorName;

            ua = ua.replace(' ', '#');

            connection.setRequestProperty("User-Agent", ua);

            if (DEBUG) Log.d(TAG, "Downloading XTRA data from " +
                             url + ": User-Agent: " + ua);

	    int rp = connection.getResponseCode() ;
            if (rp != 200) { // HTTP 200 is success.
                if (DEBUG) Log.d(TAG, "HTTP error: " + rp);
                return null;
            }


	    BufferedReader in = new BufferedReader(
		        new InputStreamReader(connection.getInputStream()));

	    StringBuffer response = new StringBuffer();
	    String inputLine;

		while ((inputLine = in.readLine()) != null) {
			response.append(inputLine);
		}
		in.close();

            return response.toString().getBytes();
        } catch (Exception e) {
            if (DEBUG) Log.d(TAG, "error " + e);
        } finally {

        }
        return null;
    }

}
