/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

import java.util.ArrayList;
import java.util.List;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.TextView;

public class MetaInfoListAdapter extends ArrayAdapter <MBNMetaInfo> {
    private ArrayList<MBNMetaInfo> mMBNInfo;

    public MetaInfoListAdapter(Context context, int resource, List<MBNMetaInfo> objects) {
        super(context, resource, objects);
        this.mMBNInfo = (ArrayList<MBNMetaInfo>) objects;
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        View v = convertView;
        if (v == null) {
            LayoutInflater inflater = (LayoutInflater) getContext().getSystemService(
                    Context.LAYOUT_INFLATER_SERVICE);
            v = inflater.inflate(R.layout.meta_info_list, null);
        }

        if (v == null) {
            return null;
        }

        MBNMetaInfo tmp = mMBNInfo.get(position);
        if (tmp != null) {
            TextView metaTv = (TextView) v.findViewById(R.id.meta_info);
            metaTv.setText("Meta: " + tmp.getMetaInfo());
            TextView sourceTv = (TextView) v.findViewById(R.id.mbn_source);
            sourceTv.setText("Source: " + tmp.getSourceString());
        }
        return v;
    }

}