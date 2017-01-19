/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
--------------------------------------------------------------------*/

#include <stdlib.h>
#include <media/hardware/HardwareAPI.h>
#ifdef _ANDROID_
#include <cutils/properties.h>
#endif

#include "OMX_Component.h"
#include "OMX_IndexExt.h"
#include "OMX_VideoExt.h"
#include "OMX_QCOMExtns.h"
#include "QComOMXMetadata.h"
#include "venc/inc/omx_video_common.h"
#include "vtest_Debug.h"
#include "vtest_Signal.h"
#include "vtest_SignalQueue.h"
#include "vtest_Thread.h"
#include "vtest_Sleeper.h"
#include "vtest_Encoder.h"

#undef LOG_TAG
#define LOG_TAG "VTEST_ENCODER"

#define Log2(number, power)  { OMX_U32 temp = number; power = 0; while( (0 == (temp & 0x1)) &&  power < 16) { temp >>=0x1; power++; } }
#define FractionToQ16(q,num,den) { OMX_U32 power; Log2(den,power); q = num << (16 - power); }

using android::encoder_media_buffer_type;
using android::StoreMetaDataInBuffersParams;

namespace vtest {

struct CmdType {
    OMX_EVENTTYPE   eEvent;
    OMX_COMMANDTYPE eCmd;
    OMX_U32         sCmdData;
    OMX_ERRORTYPE   eResult;
};

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
Encoder::Encoder(CodecConfigType *pConfig)
    : ISource(),
      m_bInputEOSReached(OMX_FALSE),
      m_pSignalQueue(new SignalQueue(32, sizeof(CmdType))),
      m_bSecureSession(OMX_FALSE),
      m_eCodec(pConfig->eCodec),
      m_hEncoder(NULL),
      m_eState(OMX_StateLoaded),
      m_eStatePending(OMX_StateLoaded),
      m_nInputBufferCount(0),
      m_nOutputBufferCount(0),
      m_nInputBufferSize(0),
      m_nOutputBufferSize(0),
      m_LTRId(0),
      m_nDynamicIndexCount(0),
      m_bPortReconfig(OMX_FALSE),
      m_pFreeBufferQueue(new SignalQueue(32, sizeof(BufferInfo*))) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_STRING role = (OMX_STRING)"";
    static OMX_CALLBACKTYPE callbacks =
    { EventCallback, EmptyDoneCallback, FillDoneCallback };

    sprintf(m_pName, "Encoder");
    VTEST_MSG_HIGH("%s created...", m_pName);

    GetComponentRole(pConfig->bSecureSession, pConfig->eCodec, &role);
    result = OMX_GetHandle(&m_hEncoder, role, this, &callbacks);
    if (result != OMX_ErrorNone || m_hEncoder == NULL) {
        VTEST_MSG_ERROR("Error getting component handle, rv: %d", result);
    }
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
Encoder::~Encoder() {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    VTEST_MSG_LOW("start");

    //Move this a to an intermediate function between stop and destructor
    result = SetState(OMX_StateLoaded, OMX_TRUE);
    FAILED(result, "Could not move to OMX_StateLoaded!");

    if (m_hEncoder != NULL) {
        OMX_FreeHandle(m_hEncoder);
        m_hEncoder = NULL;
    }
    if (m_pSignalQueue != NULL) {
        delete m_pSignalQueue;
        m_pSignalQueue = NULL;
    }
    if (m_pFreeBufferQueue != NULL) {
        delete m_pFreeBufferQueue;
        m_pFreeBufferQueue = NULL;
    }
    VTEST_MSG_LOW("done");
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
PortBufferCapability Encoder::GetBufferRequirements(OMX_U32 ePortIndex) {

    PortBufferCapability sBufCap;

    Mutex::Autolock autoLock(m_pLock);
    memset(&sBufCap, 0, sizeof(PortBufferCapability));

    // make sure we have been configured here
    if (m_nInputBufferCount <= 0 || m_nOutputBufferSize <= 0) {
        VTEST_MSG_ERROR("Error: must call configure to get valid buf reqs");
        return sBufCap;
    }

    if (ePortIndex == PORT_INDEX_IN) {
        sBufCap.bAllocateBuffer = OMX_TRUE;
        sBufCap.bUseBuffer = OMX_TRUE;
        sBufCap.pSource = this;
        sBufCap.ePortIndex = ePortIndex;
        sBufCap.nWidth = m_sConfig.nFrameWidth;
        sBufCap.nHeight = m_sConfig.nFrameHeight;
        sBufCap.nMinBufferSize = m_nInputBufferSize;
        sBufCap.nMinBufferCount = m_nInputBufferCount;
        sBufCap.nBufferUsage = 0;
        sBufCap.nExtraBufferCount = 0;
    } else if (ePortIndex == PORT_INDEX_OUT) {
        sBufCap.bAllocateBuffer = OMX_TRUE;
        sBufCap.bUseBuffer = OMX_FALSE;
        sBufCap.pSource = this;
        sBufCap.ePortIndex = ePortIndex;
        sBufCap.nWidth = m_sConfig.nOutputFrameWidth;
        sBufCap.nHeight = m_sConfig.nOutputFrameHeight;
        sBufCap.nMinBufferSize = m_nOutputBufferSize;
        sBufCap.nMinBufferCount = m_nOutputBufferCount;
        sBufCap.nBufferUsage = 0;
        sBufCap.nExtraBufferCount = 0;
    } else {
        VTEST_MSG_ERROR("Error: invalid port selection");
    }
    return sBufCap;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::Start() {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_eState != OMX_StateExecuting) {
        result = SetState(OMX_StateExecuting, OMX_TRUE);
        FAILED1(result, "Could not move to executing state!");
    }
    return ISource::Start();
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::Stop() {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    {
        Mutex::Autolock autoLock(m_pLock);
        m_bThreadStop = OMX_TRUE;
        if ((m_eState == OMX_StateIdle)
                || (m_eStatePending == OMX_StateIdle)) {
            return result;
        }
    }

    result = SetState(OMX_StateIdle, OMX_FALSE);
    FAILED(result, "Could not move to OMX_StateIdle!");

    /* We should not acquire a lock across sendcommand because of a possible
     * deadlock scenario */
    Flush();
    OMX_SendCommand(m_hEncoder, OMX_CommandPortDisable, PORT_INDEX_OUT, 0);
    OMX_SendCommand(m_hEncoder, OMX_CommandPortDisable, PORT_INDEX_IN, 0);
    return ISource::Stop();
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::Configure(CodecConfigType *pConfig,
        BufferManager *pBufManager, ISource *pSource, ISource *pSink) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    char value[PROPERTY_VALUE_MAX] = { 0 };

    if (!pConfig) {
       VTEST_MSG_ERROR("%s - invalid input", __FUNCTION__);
       return OMX_ErrorBadParameter;
    }

    result = ISource::Configure(pConfig, pBufManager, pSource, pSink);
    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("Encoder configure failed");
        return result;
    }

    Mutex::Autolock autoLock(m_pLock);

    VTEST_MSG_HIGH("");

    m_sConfig = *pConfig;

    //////////////////////////////////////////
    // codec specific config
    //////////////////////////////////////////
    if (m_eCodec == OMX_VIDEO_CodingMPEG4) {

        OMX_VIDEO_PARAM_MPEG4TYPE mp4;
        mp4.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                OMX_IndexParamVideoMpeg4, (OMX_PTR)& mp4);
        if (result != OMX_ErrorNone) {
            return result;
        }

        mp4.nTimeIncRes = pConfig->nTimeIncRes;
        mp4.nPFrames = pConfig->nIntraPeriod - 1;
        if ((pConfig->eCodecProfile == MPEG4ProfileSimple) ||
                (pConfig->eCodecProfile == MPEG4ProfileAdvancedSimple)) {
            switch (pConfig->eCodecProfile) {
                case MPEG4ProfileSimple:
                    mp4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
                    mp4.nBFrames = 0;
                    break;
                case MPEG4ProfileAdvancedSimple:
                    mp4.eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                    mp4.nBFrames = 1;
                    mp4.nPFrames = pConfig->nIntraPeriod / 2;
                    break;
                default:
                    mp4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
                    mp4.nBFrames = 0;
                    break;
            }
        } else {
            VTEST_MSG_MEDIUM("Invalid MPEG4 Profile set defaulting to Simple Profile");
            mp4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
        }
        mp4.eLevel = OMX_VIDEO_MPEG4Level0;
        mp4.nSliceHeaderSpacing = 0;
        mp4.bReversibleVLC = OMX_FALSE;
        mp4.bACPred = OMX_TRUE;
        mp4.bGov = OMX_FALSE;
        mp4.bSVH = pConfig->bEnableShortHeader;
        mp4.nHeaderExtension = 0;

        result = OMX_SetParameter(m_hEncoder,
                OMX_IndexParamVideoMpeg4, (OMX_PTR)&mp4);

    } else if (m_eCodec == OMX_VIDEO_CodingH263) {

        OMX_VIDEO_PARAM_H263TYPE h263;
        h263.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                OMX_IndexParamVideoH263, (OMX_PTR)&h263);
        if (result != OMX_ErrorNone) {
            return result;
        }

        h263.nPFrames = pConfig->nIntraPeriod - 1;
        h263.nBFrames = 0;
        if (pConfig->eCodecProfile == H263ProfileBaseline) {
            h263.eProfile = OMX_VIDEO_H263ProfileBaseline;
        } else {
            VTEST_MSG_MEDIUM("Invalid H263 Profile set defaulting to Baseline Profile");
            h263.eProfile = (OMX_VIDEO_H263PROFILETYPE)OMX_VIDEO_H263ProfileBaseline;
        }
        h263.eLevel = OMX_VIDEO_H263Level10;
        h263.bPLUSPTYPEAllowed = OMX_FALSE;
        h263.nAllowedPictureTypes = 2;
        h263.bForceRoundingTypeToZero = OMX_TRUE;
        h263.nPictureHeaderRepetition = 0;
        h263.nGOBHeaderInterval = 1;

        result = OMX_SetParameter(m_hEncoder,
                OMX_IndexParamVideoH263, (OMX_PTR)&h263);

    } else if (m_eCodec == OMX_VIDEO_CodingAVC) {

        OMX_VIDEO_PARAM_AVCTYPE avc;
        OMX_VIDEO_CONFIG_AVCINTRAPERIOD idr_period;
        avc.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                OMX_IndexParamVideoAvc, (OMX_PTR)&avc);
        if (result != OMX_ErrorNone) {
            return result;
        }

        avc.nPFrames = pConfig->nIntraPeriod - 1;
        avc.nBFrames = 0;
        switch (pConfig->eCodecProfile) {
            case AVCProfileBaseline:
                avc.eProfile = OMX_VIDEO_AVCProfileBaseline;
                avc.nBFrames = 0;
                break;
            case AVCProfileHigh:
                avc.eProfile = OMX_VIDEO_AVCProfileHigh;
                avc.nBFrames = 1;
                avc.nPFrames = pConfig->nIntraPeriod / 2;
                break;
            case AVCProfileMain:
                avc.eProfile = OMX_VIDEO_AVCProfileMain;
                avc.nBFrames = 1;
                avc.nPFrames = pConfig->nIntraPeriod / 2;
                break;
            default:
                VTEST_MSG_ERROR("Invalid H264 Profile set defaulting to Baseline Profile");
                avc.eProfile = OMX_VIDEO_AVCProfileBaseline;
                avc.nBFrames = 0;
                break;
        }

        OMX_U32 slice_delivery_mode = 0;
        property_get("vidc.venc.slicedeliverymode", value, "0");
        slice_delivery_mode = atoi(value);
        if (slice_delivery_mode) {
            avc.nBFrames = 0;
        }

        avc.eLevel = OMX_VIDEO_AVCLevel1;
        avc.bUseHadamard = OMX_FALSE;
        avc.nRefFrames = 1;
        avc.nRefIdx10ActiveMinus1 = 1;
        avc.nRefIdx11ActiveMinus1 = 0;
        avc.bEnableUEP = OMX_FALSE;
        avc.bEnableFMO = OMX_FALSE;
        avc.bEnableASO = OMX_FALSE;
        avc.bEnableRS = OMX_FALSE;
        avc.nAllowedPictureTypes = 2;
        avc.bFrameMBsOnly = OMX_FALSE;
        avc.bMBAFF = OMX_FALSE;
        avc.bWeightedPPrediction = OMX_FALSE;
        avc.nWeightedBipredicitonMode = 0;
        avc.bconstIpred = OMX_FALSE;
        avc.bDirect8x8Inference = OMX_FALSE;
        avc.bDirectSpatialTemporal = OMX_FALSE;

        if (avc.eProfile == OMX_VIDEO_AVCProfileBaseline) {
            avc.bEntropyCodingCABAC = OMX_FALSE;
            avc.nCabacInitIdc = 0;
        } else {
            if (pConfig->bCABAC) {
                avc.bEntropyCodingCABAC = OMX_TRUE;
                avc.nCabacInitIdc = 0;
            } else {
                avc.bEntropyCodingCABAC = OMX_FALSE;
                avc.nCabacInitIdc = 0;
            }
        }

        switch (pConfig->nDeblocker) {
            case 0:
                avc.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterDisable;
                break;
            case 1:
                avc.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;
                break;
            case 2:
                avc.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterDisableSliceBoundary;
                break;
            default:
                avc.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;
                break;
        }
        result = OMX_SetParameter(m_hEncoder,
                OMX_IndexParamVideoAvc, (OMX_PTR)&avc);

        if (result != OMX_ErrorNone) {
            return result;
        }
        ///////////////////////////////////////
        // set SPS/PPS insertion for IDR frames
        ///////////////////////////////////////
        android::PrependSPSPPSToIDRFramesParams param;
        memset(&param, 0, sizeof(android::PrependSPSPPSToIDRFramesParams));
        param.nSize = sizeof(android::PrependSPSPPSToIDRFramesParams);
        param.bEnable = pConfig->bInsertInbandVideoHeaders;
        VTEST_MSG_MEDIUM("Set SPS/PPS headers: %d", param.bEnable);
        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexParamSequenceHeaderWithIDR,
                (OMX_PTR)&param);

        if (result != OMX_ErrorNone) {
            return result;
        }
        ///////////////////////////////////////
        // set AU delimiters for video stream
        ///////////////////////////////////////
        OMX_QCOM_VIDEO_CONFIG_H264_AUD param_aud;
        memset(&param_aud, 0, sizeof(OMX_QCOM_VIDEO_CONFIG_H264_AUD));
        param_aud.nSize = sizeof(OMX_QCOM_VIDEO_CONFIG_H264_AUD);
        param_aud.bEnable = pConfig->bInsertAUDelimiters;
        VTEST_MSG_MEDIUM("Set AU Delimiters: %d", param_aud.bEnable);
        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexParamH264AUDelimiter,
                (OMX_PTR)&param_aud);

#ifdef MSM8974
        if (result == OMX_ErrorNone && pConfig->nIDRPeriod) {
            result = SetIDRPeriod(pConfig->nIntraPeriod - 1, pConfig->nIDRPeriod);
        }
#endif
#ifndef MSM8974
        ////////////////////////////////////////
        // SEI FramePack data
        ////////////////////////////////////////
        if (result != OMX_ErrorNone) {
            return result;
        }

        OMX_QCOM_FRAME_PACK_ARRANGEMENT framePackingArrangement;
        memset(&framePackingArrangement, 0, sizeof(framePackingArrangement));
        framePackingArrangement.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        framePackingArrangement.id = pConfig->id;
        framePackingArrangement.cancel_flag = pConfig->cancel_flag;
        framePackingArrangement.type = pConfig->type;
        framePackingArrangement.quincunx_sampling_flag = pConfig->quincunx_sampling_flag;
        framePackingArrangement.content_interpretation_type = pConfig->content_interpretation_type;
        framePackingArrangement.spatial_flipping_flag = pConfig->spatial_flipping_flag;
        framePackingArrangement.frame0_flipped_flag = pConfig->frame0_flipped_flag;
        framePackingArrangement.field_views_flag = pConfig->field_views_flag;
        framePackingArrangement.current_frame_is_frame0_flag = pConfig->current_frame_is_frame0_flag;
        framePackingArrangement.frame0_self_contained_flag = pConfig->frame0_self_contained_flag;
        framePackingArrangement.frame1_self_contained_flag = pConfig->frame1_self_contained_flag;
        framePackingArrangement.frame0_grid_position_x = pConfig->frame0_grid_position_x;
        framePackingArrangement.frame0_grid_position_y = pConfig->frame0_grid_position_y;
        framePackingArrangement.frame1_grid_position_x = pConfig->frame1_grid_position_x;
        framePackingArrangement.frame1_grid_position_y = pConfig->frame1_grid_position_y;
        framePackingArrangement.reserved_byte = pConfig->reserved_byte;
        framePackingArrangement.repetition_period = pConfig->repetition_period;
        framePackingArrangement.extension_flag = pConfig->extension_flag;

        result = OMX_SetConfig(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexConfigVideoFramePackingArrangement,
                (OMX_PTR)&framePackingArrangement);
#endif
    } else if (m_eCodec == OMX_VIDEO_CodingVPX) {

        OMX_VIDEO_PARAM_VP8TYPE vp8;
        vp8.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_IndexParamVideoVp8, (OMX_PTR)&vp8);

        if (result != OMX_ErrorNone) {
            VTEST_MSG_ERROR("Failed to OMX_GetParameter(OMX_IndexParamVideoVp8), error = 0x%x",
                    result);
            return result;
        }

        if (pConfig->eCodecProfile == VP8ProfileMain) {
            vp8.eProfile = OMX_VIDEO_VP8ProfileMain;
        } else {
            VTEST_MSG_ERROR("Invalid VP8 Profile set defaulting to OMX_VIDEO_VP8ProfileMain");
            vp8.eProfile = OMX_VIDEO_VP8ProfileMain;
        }

        if (pConfig->eCodecLevel == VP8Version0) {
            vp8.eLevel = OMX_VIDEO_VP8Level_Version0;
        } else if (pConfig->eCodecLevel == VP8Version1) {
            vp8.eLevel = OMX_VIDEO_VP8Level_Version1;
        } else if (!pConfig->eCodecLevel == DefaultLevel) {
            VTEST_MSG_ERROR("Invalid VP8 Level using default");
        }

        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_IndexParamVideoVp8, (OMX_PTR)&vp8);
    }

    //////////////////////////////////////////
    // error resilience
    //////////////////////////////////////////
    if (result != OMX_ErrorNone) {
        return result;
    }

    OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE errorCorrection;
    errorCorrection.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetParameter(m_hEncoder,
            (OMX_INDEXTYPE)OMX_IndexParamVideoErrorCorrection,
            (OMX_PTR)&errorCorrection);

    errorCorrection.bEnableRVLC = OMX_FALSE;
    errorCorrection.bEnableDataPartitioning = OMX_FALSE;

    if (result != OMX_ErrorNone) {
        return result;
    }

    if (pConfig->eResyncMarkerType != RESYNC_MARKER_NONE) {
        if (pConfig->eResyncMarkerType != RESYNC_MARKER_MB) {
            if ((pConfig->eResyncMarkerType == RESYNC_MARKER_BITS) &&
                    (m_eCodec == OMX_VIDEO_CodingMPEG4)) {
                errorCorrection.bEnableResync = OMX_TRUE;
                errorCorrection.nResynchMarkerSpacing = pConfig->nResyncMarkerSpacing;
                errorCorrection.bEnableHEC = pConfig->nHECInterval == 0 ? OMX_FALSE : OMX_TRUE;
            } else if ((pConfig->eResyncMarkerType == RESYNC_MARKER_BITS) &&
                    (m_eCodec == OMX_VIDEO_CodingAVC)) {
                errorCorrection.bEnableResync = OMX_TRUE;
                errorCorrection.nResynchMarkerSpacing = pConfig->nResyncMarkerSpacing;
            } else if ((pConfig->eResyncMarkerType == RESYNC_MARKER_GOB) &&
                    (m_eCodec == OMX_VIDEO_CodingH263)) {
                errorCorrection.bEnableResync = OMX_FALSE;
                errorCorrection.nResynchMarkerSpacing = pConfig->nResyncMarkerSpacing;
                errorCorrection.bEnableDataPartitioning = OMX_TRUE;
            }

            result = OMX_SetParameter(m_hEncoder,
                    (OMX_INDEXTYPE)OMX_IndexParamVideoErrorCorrection,
                    (OMX_PTR)&errorCorrection);
        } else { //pConfig->eResyncMarkerType == RESYNC_MARKER_MB
            if (m_eCodec == OMX_VIDEO_CodingAVC) {
                OMX_VIDEO_PARAM_AVCTYPE avcdata;
                avcdata.nPortIndex = (OMX_U32)PORT_INDEX_OUT; // output
                result = OMX_GetParameter(m_hEncoder,
                        OMX_IndexParamVideoAvc, (OMX_PTR)&avcdata);
                if (result == OMX_ErrorNone) {
                    VTEST_MSG_HIGH("slice mode enabled with slice size = %u",
                            (unsigned int)pConfig->nResyncMarkerSpacing);
                    avcdata.nSliceHeaderSpacing = pConfig->nResyncMarkerSpacing;
                    result = OMX_SetParameter(m_hEncoder,
                            OMX_IndexParamVideoAvc, (OMX_PTR) & avcdata);
                }
                OMX_U32 slice_delivery_mode = 0;
                property_get("vidc.venc.slicedeliverymode", value, "0");
                slice_delivery_mode = atoi(value);
                if ((result == OMX_ErrorNone) && (slice_delivery_mode)) {
                    VTEST_MSG_HIGH("Slice delivery mode enabled via setprop command");
                    QOMX_EXTNINDEX_PARAMTYPE extnIndex;
                    extnIndex.nPortIndex = PORT_INDEX_OUT;
                    extnIndex.bEnable = OMX_TRUE;
                    result = OMX_SetParameter(m_hEncoder,
                            (OMX_INDEXTYPE)OMX_QcomIndexEnableSliceDeliveryMode,
                            (OMX_PTR) & extnIndex);
                    if (result != OMX_ErrorNone) {
                        VTEST_MSG_ERROR("Failed to set slice delivery mode");
                    }
                }
            } else if (m_eCodec == OMX_VIDEO_CodingMPEG4) {
                OMX_VIDEO_PARAM_MPEG4TYPE mp4;
                mp4.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
                result = OMX_GetParameter(m_hEncoder,
                        OMX_IndexParamVideoMpeg4, (OMX_PTR)&mp4);
                if (result == OMX_ErrorNone) {
                    mp4.nSliceHeaderSpacing = pConfig->nResyncMarkerSpacing;
                    result = OMX_SetParameter(m_hEncoder,
                            OMX_IndexParamVideoMpeg4, (OMX_PTR)&mp4);
                }
            }
        } //pConfig->eResyncMarkerType == RESYNC_MARKER_MB
    } // pConfig->eResyncMarkerType != RESYNC_MARKER_NONE

#ifndef MAX_RES_720P

    //////////////////////////////////////////
    // Note: ExtraData should be set before OMX_IndexParamPortDefinition
    //       else driver will through error allocating input/output buffers
    //////////////////////////////////////////
    if (pConfig->bExtradata && result == OMX_ErrorNone) {
        const OMX_U32 extradata[] = {OMX_ExtraDataVideoEncoderSliceInfo,
            OMX_ExtraDataVideoEncoderMBInfo};

        for (OMX_U32 c = 0; c < sizeof(extradata) / sizeof(*extradata); ++c) {
            QOMX_INDEXEXTRADATATYPE e; // OMX_QcomIndexParamIndexExtraDataType

            e.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
            e.nIndex = (OMX_INDEXTYPE)extradata[c];
            VTEST_MSG_HIGH("GetParameter OMX_QcomIndexParamIndexExtraDataType");
            result = OMX_GetParameter(m_hEncoder,
                    (OMX_INDEXTYPE)OMX_QcomIndexParamIndexExtraDataType,
                    (OMX_PTR)&e);
            if (result == OMX_ErrorNone) {
                VTEST_MSG_HIGH("GetParameter OMX_QcomIndexParamIndexExtraDataType (%x) = %d",
                        e.nIndex, e.bEnabled);
                if (e.bEnabled == OMX_FALSE) {
                    e.bEnabled = OMX_TRUE;
                    VTEST_MSG_HIGH("SetParameter OMX_QcomIndexParamIndexExtraDataType (%x) = %d",
                            e.nIndex, e.bEnabled);
                    result = OMX_SetParameter(m_hEncoder,
                            (OMX_INDEXTYPE)OMX_QcomIndexParamIndexExtraDataType,
                            (OMX_PTR)&e);
                    if (result != OMX_ErrorNone)
                        VTEST_MSG_ERROR("Setting extradata (%x) failed", e.nIndex);
                }
            }
        }
    }

#ifndef MSM8974
    /* LTR encoding settings */
    if ((result == OMX_ErrorNone) && (pConfig->nLTRMode > 0)) {
        QOMX_INDEXEXTRADATATYPE extraData;
        extraData.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        extraData.nIndex = (OMX_INDEXTYPE)OMX_ExtraDataVideoLTRInfo;
        VTEST_MSG_HIGH("GetParameter OMX_QcomIndexParamIndexExtraDataType");
        result = OMX_GetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexParamIndexExtraDataType,
                (OMX_PTR)&extraData);
        if (result != OMX_ErrorNone) {
            return result;
        }
        VTEST_MSG_HIGH("GetParameter ExtraDataVideoLTRInfo = %d",
                extraData.bEnabled);
        if (extraData.bEnabled == OMX_FALSE) {
            extraData.bEnabled = OMX_TRUE;
            VTEST_MSG_HIGH("SetParameter ExtraDataVideoLTRInfo = %d",
                    extraData.bEnabled);
            result = OMX_SetParameter(m_hEncoder,
                    (OMX_INDEXTYPE)OMX_QcomIndexParamIndexExtraDataType,
                    (OMX_PTR)&extraData);
            if (result != OMX_ErrorNone) {
                VTEST_MSG_ERROR("Setting LTR information extradata failed ...");
            }
        }
        /* get capability */
        QOMX_EXTNINDEX_RANGETYPE  ltrCountRange;
        OMX_INIT_STRUCT(&ltrCountRange, QOMX_EXTNINDEX_RANGETYPE);
        ltrCountRange.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                (OMX_INDEXTYPE)QOMX_IndexParamVideoLTRCountRangeSupported,
                (OMX_PTR) & ltrCountRange);
        if (result != OMX_ErrorNone) {
            VTEST_MSG_ERROR("Get capability on LTR Count failed 0x%x ...",
                    result);
            return result;
        }
        if ((OMX_U32)pConfig->nLTRCount > (OMX_U32)ltrCountRange.nMax) {
            VTEST_MSG_ERROR("Test app configuring LTR count %d larger than "
                    "supported %d...", (int)pConfig->nLTRCount, (int)ltrCountRange.nMax);
            result = OMX_ErrorInsufficientResources;
        } else {
            VTEST_MSG_HIGH("Encoder LTR coding capability: Max %d, Min %d, "
                    "Stepsize %d", (int)ltrCountRange.nMax, (int)ltrCountRange.nMin,
                    (int)ltrCountRange.nStepSize);
        }

        if (result != OMX_ErrorNone) {
            return result;
        }
        /* Configure LTR mode */
        QOMX_VIDEO_PARAM_LTRMODE_TYPE  ltrMode;
        OMX_INIT_STRUCT(&ltrMode, QOMX_VIDEO_PARAM_LTRMODE_TYPE);
        ltrMode.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        if (pConfig->nLTRMode == 0) {
            ltrMode.eLTRMode = QOMX_VIDEO_LTRMode_Disable;
        } else if  (pConfig->nLTRMode == 1) {
            ltrMode.eLTRMode = QOMX_VIDEO_LTRMode_Manual;
        } else if (pConfig->nLTRMode == 2) {
            ltrMode.eLTRMode = QOMX_VIDEO_LTRMode_Auto;
        }

        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)QOMX_IndexParamVideoLTRMode,
                (OMX_PTR)&ltrMode);
        if (OMX_ErrorNone != result) {
            VTEST_MSG_ERROR("Set LTR Mode failed with status %d ...", result);
        }

        if (result != OMX_ErrorNone) {
            return result;
        }

        /* Configure LTR count */
        QOMX_VIDEO_PARAM_LTRCOUNT_TYPE  ltrCount;
        OMX_INIT_STRUCT(&ltrCount, QOMX_VIDEO_PARAM_LTRCOUNT_TYPE);
        ltrCount.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        ltrCount.nCount = pConfig->nLTRCount;

        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)QOMX_IndexParamVideoLTRCount,
                (OMX_PTR) & ltrCount);
        if (result != OMX_ErrorNone) {
            VTEST_MSG_ERROR("Set LTR Count failed with status %d ...", result);
            return result;
        }


        /* Configure LTR period */
        QOMX_VIDEO_CONFIG_LTRPERIOD_TYPE  ltrPeriod;
        OMX_INIT_STRUCT(&ltrPeriod, QOMX_VIDEO_CONFIG_LTRPERIOD_TYPE);
        ltrPeriod.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        ltrPeriod.nFrames = pConfig->nLTRPeriod - 1;

        result = OMX_SetConfig(m_hEncoder,
                (OMX_INDEXTYPE)QOMX_IndexConfigVideoLTRPeriod,
                (OMX_PTR) & ltrPeriod);
        if (result != OMX_ErrorNone) {
            VTEST_MSG_ERROR("Set LTR Period failed with status %d ...", result);
            return result;
        }
    }
#endif
#endif

    //////////////////////////////////////////
    // input buffer requirements
    //////////////////////////////////////////
    if (result != OMX_ErrorNone) {
        return result;
    }

    result = SetPortParams(PORT_INDEX_IN,
            pConfig->nFrameWidth, pConfig->nFrameHeight,
            pConfig->nInBufferCount, 0, //FRAME-RATE not used for input port
            &m_nInputBufferSize, &m_nInputBufferCount);

    //////////////////////////////////////////
    // output buffer requirements
    //////////////////////////////////////////
    if (result != OMX_ErrorNone) {
        return result;
    }

    result = SetPortParams(PORT_INDEX_OUT,
            pConfig->nOutputFrameWidth, pConfig->nOutputFrameHeight,
            pConfig->nOutBufferCount, pConfig->nFramerate,
            &m_nOutputBufferSize, &m_nOutputBufferCount);

    //////////////////////////////////////////
    // bitrate
    //////////////////////////////////////////

    if (result != OMX_ErrorNone) {
        return result;
    }

    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    bitrate.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoBitrate, (OMX_PTR)&bitrate);
    if (result != OMX_ErrorNone) {
        return result;
    }

    bitrate.eControlRate = pConfig->eControlRate;
    bitrate.nTargetBitrate = pConfig->nBitrate;
    result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamVideoBitrate, (OMX_PTR)&bitrate);

#ifndef MSM8974
    //////////////////////////////////////////
    // maximum allowed bitrate check
    //////////////////////////////////////////
    OMX_U32 bitratecheck = 0;
    property_get("vidc.venc.debug.bitratecheck", value, "0");
    bitratecheck = atoi(value);
    if ((result == OMX_ErrorNone) && (bitratecheck)) {
        VTEST_MSG_HIGH("bitratecheck is enabled via setprop command");
        QOMX_EXTNINDEX_PARAMTYPE bitrate_check;
        bitrate_check.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        bitrate_check.bEnable = OMX_TRUE;
        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexParamVideoMaxAllowedBitrateCheck,
                (OMX_PTR)&bitrate_check);
    }
#endif

    if (result != OMX_ErrorNone) {
        return result;
    }
    //////////////////////////////////////////
    // quantization
    //////////////////////////////////////////
    if (pConfig->eControlRate == OMX_Video_ControlRateDisable) {
        ///////////////////////////////////////////////////////////
        // QP Config
        ///////////////////////////////////////////////////////////
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE qp;
        qp.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                OMX_IndexParamVideoQuantization, (OMX_PTR)&qp);
        if (result != OMX_ErrorNone) {
            return result;
        }

        if (m_eCodec == OMX_VIDEO_CodingAVC) {
            qp.nQpI = 30;
            qp.nQpP = 30;
        } else {
            qp.nQpI = 10;
            qp.nQpP = 10;
        }
        result = OMX_SetParameter(m_hEncoder,
                OMX_IndexParamVideoQuantization, (OMX_PTR)&qp);
    }

    if (result != OMX_ErrorNone) {
        return result;
    }

    //////////////////////////////////////////
    // quantization parameter range
    //////////////////////////////////////////
    if (pConfig->eControlRate == OMX_Video_ControlRateDisable) {
        ///////////////////////////////////////////////////////////
        // QP range setting
        ///////////////////////////////////////////////////////////
        OMX_QCOM_VIDEO_PARAM_QPRANGETYPE qprange;
        qprange.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_GetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexParamVideoQPRange,
                (OMX_PTR)&qprange);
        if (result != OMX_ErrorNone) {
            return result;
        }

        qprange.minQP = pConfig->nMinQp;
        qprange.maxQP = pConfig->nMaxQp;
        VTEST_MSG_MEDIUM(
                "Original Config values with minQP = %u, maxQP = %u",
                (unsigned int)qprange.minQP, (unsigned int)qprange.maxQP);
        if (m_eCodec == OMX_VIDEO_CodingAVC) {
            if (qprange.minQP < 2) qprange.minQP = 2;
            if (qprange.maxQP > 51) qprange.maxQP = 51;
        } else {
            if (qprange.minQP < 2) qprange.minQP = 2;
            if (qprange.maxQP > 31) qprange.maxQP = 31;
        }
        VTEST_MSG_MEDIUM(
                "Calling OMX_QcomIndexParamVideoQPRange with minQP = %u, maxQP = %u",
                (unsigned int)qprange.minQP, (unsigned int)qprange.maxQP);
        qprange.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
        result = OMX_SetParameter(m_hEncoder,
                (OMX_INDEXTYPE)OMX_QcomIndexParamVideoQPRange,
                (OMX_PTR)&qprange);
    }

    if (result != OMX_ErrorNone) {
        return result;
    }

    //////////////////////////////////////////
    // frame rate
    //////////////////////////////////////////
    OMX_CONFIG_FRAMERATETYPE framerate;
    framerate.nPortIndex = (OMX_U32)PORT_INDEX_IN;
    result = OMX_GetConfig(m_hEncoder,
            OMX_IndexConfigVideoFramerate, (OMX_PTR)&framerate);
    if (result != OMX_ErrorNone) {
        return result;
    }

    FractionToQ16(framerate.xEncodeFramerate, (int)(pConfig->nFramerate * 2), 2);
    result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoFramerate, (OMX_PTR)&framerate);
    if (result != OMX_ErrorNone) {
        return result;
    }

    //////////////////////////////////////////
    // intra refresh
    //////////////////////////////////////////
    OMX_VIDEO_PARAM_INTRAREFRESHTYPE ir;
    ir.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoIntraRefresh, (OMX_PTR)&ir);
    if (result != OMX_ErrorNone) {
        return result;
    }

    if ((pConfig->nIntraRefreshMBCount < ((pConfig->nFrameWidth * pConfig->nFrameHeight) >> 8))) {
        ir.eRefreshMode = OMX_VIDEO_IntraRefreshCyclic;
        ir.nCirMBs = pConfig->nIntraRefreshMBCount;
        result = OMX_SetParameter(m_hEncoder,
                OMX_IndexParamVideoIntraRefresh, (OMX_PTR)&ir);
    } else {
        VTEST_MSG_ERROR(
                "IntraRefresh not set for %u MBs because total MBs is %u",
                (unsigned int)ir.nCirMBs, (unsigned int)(pConfig->nFrameWidth * pConfig->nFrameHeight >> 8));
    }

    if (result != OMX_ErrorNone) {
        return result;
    }

#ifndef MSM8974
    //////////////////////////////////////////
    // rotation
    //////////////////////////////////////////
    OMX_CONFIG_ROTATIONTYPE framerotate;
    framerotate.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetConfig(m_hEncoder,
            OMX_IndexConfigCommonRotate, (OMX_PTR)&framerotate);
    if (result != OMX_ErrorNone) {
        return result;
    }

    framerotate.nRotation = pConfig->nRotation;
    result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigCommonRotate, (OMX_PTR)&framerotate);

#ifdef _ANDROID_
    //////////////////////////////////////////
    // SetSyntaxHeader
    //////////////////////////////////////////
    char prop_val[PROPERTY_VALUE_MAX] = { 0 };
    OMX_U32 getcodechdr = 0;
    property_get("vidc.venc.debug.codechdr", prop_val, "0");
    getcodechdr = atoi(prop_val);

    if ((result == OMX_ErrorNone) && (getcodechdr)) {
#define VIDEO_SYNTAXHDRSIZE 100
        VTEST_MSG_HIGH("getcodechdr is enabled via setprop command");
        QOMX_EXTNINDEX_PARAMTYPE codechdr;
        memset(&codechdr, 0, sizeof(QOMX_EXTNINDEX_PARAMTYPE));
        codechdr.nSize = sizeof(QOMX_EXTNINDEX_PARAMTYPE) +
            VIDEO_SYNTAXHDRSIZE;
        codechdr.nPortIndex = (OMX_U32)PORT_INDEX_OUT; // output
        codechdr.pData = (OMX_PTR)malloc(VIDEO_SYNTAXHDRSIZE);
        if (codechdr.pData == NULL) {
            VTEST_MSG_ERROR("failed to allocate memory for syntax header");
            result = OMX_ErrorInsufficientResources;
        }
        if (result == OMX_ErrorNone) {
            result = OMX_GetParameter(m_hEncoder,
                    (OMX_INDEXTYPE)QOMX_IndexParamVideoSyntaxHdr,
                    (OMX_PTR)&codechdr);
            if (result == OMX_ErrorNone) {
                for (OMX_U32 i = 0; i < codechdr.nDataSize; i++) {
                    VTEST_MSG_HIGH("header[%u] = %x", (unsigned int)i,
                            *((OMX_U8 *)codechdr.pData + i));
                }
            } else {
                VTEST_MSG_ERROR("failed to get syntax header");
            }
            if (codechdr.pData) {
                free(codechdr.pData);
            }
        }
    }
#endif
#endif

    // go to Idle, so we can allocate buffers
    result = SetState(OMX_StateIdle, OMX_FALSE);
    FAILED1(result, "Could not move to OMX_StateIdle!");
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::SetBuffer(BufferInfo *pBuffer, ISource *pSource) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    {
        Mutex::Autolock autoLock(m_pLock);
        /* TODO : give all buffers back to Buffer Manager */
        if (m_bThreadStop) {
            result = m_pFreeBufferQueue->Push(&pBuffer, sizeof(BufferInfo **));
            return result;
        }
        if (m_bPortReconfig && (pSource == m_pSink)) {
            result = m_pFreeBufferQueue->Push(&pBuffer, sizeof(BufferInfo **));
            return result;
        }

    }

    result = ISource::SetBuffer(pBuffer, pSource);
    if (result != OMX_ErrorNone) {
        return result;
    }

    {
        Mutex::Autolock autoLock(m_pLock);
        if ((pBuffer->pHeaderIn->nFlags & OMX_BUFFERFLAG_EOS)
                && (pSource == m_pSource)) {
            VTEST_MSG_HIGH("Got input EOS");
            m_bInputEOSReached = OMX_TRUE;
        }
    }

    if (pSource == m_pSource) { //input port

        VTEST_MSG_LOW("%s ==> %s: ETB size=%u", pSource->Name(), Name(),
                (unsigned int)pBuffer->pHeaderIn->nFilledLen);

        // This allows transcode via buffer copy...
        // TODO: this will not work for secure buffers
        // TODO: fix this ugly hack, required as we lack RTTI
        if (strstr(m_pSource->Name(), "Decoder") != NULL) {
            VTEST_MSG_LOW("In pBuffer copy mode %p %p",
                    pBuffer->pHeaderIn->pBuffer, pBuffer->pHeaderOut->pBuffer);
            memcpy(pBuffer->pHeaderIn->pBuffer, pBuffer->pHeaderOut->pBuffer,
                   pBuffer->pHeaderIn->nFilledLen);
        }
        result = DeliverInput(pBuffer->pHeaderIn);
        FAILED1(result, "ETB error, 0x%lx (%p %p)",
                pBuffer->pHandle, pBuffer->pHeaderIn, pBuffer->pHeaderOut);
    } else if (pSource == m_pSink) { //output port

        VTEST_MSG_LOW("%s ==> %s: FTB size=%u", pSource->Name(), Name(),
                (unsigned int)pBuffer->pHeaderOut->nFilledLen);
        result = DeliverOutput(pBuffer->pHeaderOut);
        FAILED1(result, "FTB error, 0x%lx (%p %p)",
                pBuffer->pHandle, pBuffer->pHeaderIn, pBuffer->pHeaderOut);
    } else {
        VTEST_MSG_ERROR("unknown source %p", pSource);
        result = OMX_ErrorBadParameter;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::AllocateBuffers(BufferInfo **pBuffers,
        OMX_U32 nWidth, OMX_U32 nHeight, OMX_U32 nBufferCount,
        OMX_U32 nBufferSize, OMX_U32 ePortIndex, OMX_U32 nBufferUsage) {

    (void)nWidth, (void)nHeight, (void)nBufferUsage;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_U32 nOriginalBufferCount = -1;
    OMX_U32 nOriginalBufferSize = -1;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    Mutex::Autolock autoLock(m_pLock);
    VTEST_MSG_HIGH("alloc %s size %u count %u",
            OMX_PORT_NAME(ePortIndex), (unsigned int)nBufferSize, (unsigned int)nBufferCount);

    if (ePortIndex == PORT_INDEX_IN) {
        nOriginalBufferCount = m_nInputBufferCount;
        nOriginalBufferSize = m_nInputBufferSize;
    } else if (ePortIndex == PORT_INDEX_OUT) {
        nOriginalBufferCount = m_nOutputBufferCount;
        nOriginalBufferSize = m_nOutputBufferSize;
    }

    if ((nOriginalBufferCount > nBufferCount) ||
        (nOriginalBufferSize > nBufferSize)) {
        VTEST_MSG_ERROR(
            "Allocate Buffers failure : original count : %u, new count : %u, "
            "original size : %u, new size : %u",
            (unsigned int)nOriginalBufferCount, (unsigned int)nBufferCount, (unsigned int)nOriginalBufferSize, (unsigned int)nBufferSize);
        return OMX_ErrorBadParameter;
    }

    //TODO: this is a workaround, OMX_Comp should allow this during a PortReconfig but does not.
    if (m_eState != OMX_StateExecuting) {
        sPortDef.nPortIndex = ePortIndex;
        OMX_GetParameter(m_hEncoder, OMX_IndexParamPortDefinition, &sPortDef);
        FAILED1(result, "error get OMX_IndexParamPortDefinition");

        sPortDef.nBufferCountActual = nBufferCount;
        result = OMX_SetParameter(m_hEncoder, OMX_IndexParamPortDefinition, (OMX_PTR) & sPortDef);
        FAILED1(result, "error set OMX_IndexParamPortDefinition");
    }

    *pBuffers = new BufferInfo[nBufferCount];
    memset(*pBuffers, 0, sizeof(BufferInfo) * nBufferCount);

    for (OMX_U32 i = 0; i < nBufferCount; i++) {

        BufferInfo *pBuffer = &((*pBuffers)[i]);
        OMX_BUFFERHEADERTYPE **pHeaderPtr = pBuffer->GetHeaderPtr(ePortIndex);

        result = OMX_AllocateBuffer(m_hEncoder, pHeaderPtr,
                ePortIndex, this, nBufferSize);
        FAILED1(result, "error allocating buffer");

        pBuffer->pHeaderIn = pBuffer->pHeaderOut = *pHeaderPtr;

        // get buffer fd, this allows "use-buffer" and MDP-access
        struct pmem *pMem =
            (ePortIndex == PORT_INDEX_IN ?
             ((struct pmem *)pBuffer->pHeaderIn->pInputPortPrivate) :
             ((struct pmem *)pBuffer->pHeaderOut->pOutputPortPrivate));
        pBuffer->pHandle = (OMX_U32)pMem->fd;

        VTEST_MSG_HIGH("%s alloc_ct=%u sz=%u fd=0x%lx hdr=(%p %p)",
                OMX_PORT_NAME(ePortIndex), (unsigned int)i+1, (unsigned int)nBufferSize,
                pBuffer->pHandle, pBuffer->pHeaderIn, pBuffer->pHeaderOut);
    }

    if (ePortIndex == PORT_INDEX_IN) {
        m_nInputBufferCount = nBufferCount;
        m_nInputBufferSize = nBufferSize;
    }
    else if (ePortIndex == PORT_INDEX_OUT) {
        m_nOutputBufferCount = nBufferCount;
        m_nOutputBufferSize = nBufferSize;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::UseBuffers(BufferInfo **pBuffers,
        OMX_U32 nWidth, OMX_U32 nHeight, OMX_U32 nBufferCount,
        OMX_U32 nBufferSize, OMX_U32 ePortIndex) {

    (void)nWidth, (void)nHeight;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BOOL bMetaMode = OMX_FALSE;

    Mutex::Autolock autoLock(m_pLock);

    if (ePortIndex == PORT_INDEX_IN) {
        //TODO: fix this ugly hack, but we do not have any RTTI info...
        //if(dynamic_cast<NativeWindowSink*>(m_pSource) != NULL) // will not work no RTTI
        if (strstr(m_pSource->Name(), "Native") != NULL) {
            VTEST_MSG_LOW("MetaMode for pass-throught transcode");
            bMetaMode = OMX_TRUE;
        } else {
            VTEST_MSG_LOW("dual-alloc mode for copy transcode");
            bMetaMode = OMX_FALSE;
        }
    }

    // MetaMode - NativeWindow allocs buffers, Encoder allocs "meta-headers" that get
    // set to point to the NativeWindow buffers
    if (bMetaMode) {
        StoreMetaDataInBuffersParams sMetadataMode;
        OMX_INIT_STRUCT(&sMetadataMode, StoreMetaDataInBuffersParams);
        sMetadataMode.nPortIndex = ePortIndex;
        sMetadataMode.bStoreMetaData = OMX_TRUE;
        result = OMX_SetParameter(m_hEncoder,
            (OMX_INDEXTYPE)OMX_QcomIndexParamVideoMetaBufferMode,
            (OMX_PTR)&sMetadataMode);
        FAILED1(result, "Error OMX_QcomIndexParamVideoEncodeMetaBufferMode");
    }

    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    sPortDef.nPortIndex = ePortIndex;
    OMX_GetParameter(m_hEncoder, OMX_IndexParamPortDefinition, &sPortDef);
    FAILED1(result, "error get OMX_IndexParamPortDefinition");

    if (sPortDef.nBufferCountActual > nBufferCount) {
        VTEST_MSG_ERROR("alloc buffer count to small (%u vs %u)",
                        (unsigned int)sPortDef.nBufferCountActual, (unsigned int)nBufferCount);
        return OMX_ErrorBadParameter;
    }

    sPortDef.nBufferCountActual = nBufferCount;
    result = OMX_SetParameter(m_hEncoder, OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef);
    FAILED1(result, "error set OMX_IndexParamPortDefinition");

    // In meta-mode the OMX_comp allocates a small structure, that we fill in
    for (OMX_U32 i = 0; i < nBufferCount; i++) {

        BufferInfo *pBuffer = &((*pBuffers)[i]);
        OMX_BUFFERHEADERTYPE **pHeaderPtr = pBuffer->GetHeaderPtr(ePortIndex);
        // in Decoder==>Encoder mode both allocate and buffers are copied
        // in NativeWindow==>Encoder mode encoder allocates "meta-buffers" that
        // point to NativeWindow buffers
        if (bMetaMode) {
            result = OMX_AllocateBuffer(m_hEncoder, pHeaderPtr,
                    ePortIndex, this, sizeof(encoder_media_buffer_type));

            android::encoder_media_buffer_type *pMeta =
                (encoder_media_buffer_type *)((*pHeaderPtr)->pBuffer);
            pMeta->buffer_type = (android::MetadataBufferType)1; // kMetadataBufferTypeGrallocSource = 1,
            pMeta->meta_handle = (native_handle_t *)pBuffer->pHandle;
            //pMeta->buffer_type = (android::MetadataBufferType)0; // kMetadataBufferTypeCameraSource = 0,
            //pMeta->meta_handle = native_handle_create(1,2);
            //native_handle_t * hNWin = (native_handle_t *)pMeta->meta_handle;
            //hNWin->data[0] = pBuffer->pHandle;
            //hNWin->data[1] = 0;    //data offset...
            //hNWin->data[2] = nBufferSize;
        } else {
            result = OMX_AllocateBuffer(m_hEncoder, pHeaderPtr,
                    ePortIndex, this, nBufferSize);
        }

        VTEST_MSG_HIGH("%s use_ct=%u sz=%u fd=0x%lx hdr=(%p %p)",
                OMX_PORT_NAME(ePortIndex), (unsigned int)i+1, (unsigned int)nBufferSize,
                pBuffer->pHandle, pBuffer->pHeaderIn, pBuffer->pHeaderOut);
    }

    if (ePortIndex == PORT_INDEX_IN) {
        m_nInputBufferCount = nBufferCount;
        m_nInputBufferSize = nBufferSize;
    } else if (ePortIndex == PORT_INDEX_OUT) {
        m_nOutputBufferCount = nBufferCount;
        m_nOutputBufferSize = nBufferSize;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::PortReconfig(OMX_U32 ePortIndex,
        OMX_U32 nWidth, OMX_U32 nHeight, const OMX_CONFIG_RECTTYPE& sRect) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    CmdType cmd;

    VTEST_MSG_LOW("PortReconfig %s", OMX_PORT_NAME(ePortIndex));

    if (ePortIndex != PORT_INDEX_IN) {
        VTEST_MSG_ERROR("Error output port reconfig not currently supported");
        return OMX_ErrorUndefined;
    }

    // disable ports, Encoder work-around requires we enable/disable both ports
    m_bPortReconfig = OMX_TRUE;
    VTEST_MSG_MEDIUM("PortReconfig OMX_CommandPortDisable");
    result = OMX_SendCommand(m_hEncoder, OMX_CommandPortDisable, PORT_INDEX_IN, 0);
    FAILED1(result, "Disable Input Port failed");
    result = OMX_SendCommand(m_hEncoder, OMX_CommandPortDisable, PORT_INDEX_OUT, 0);
    FAILED1(result, "Disable Output Port failed");

    // wait for all output buffers
    // wait for OMX_comp/sink to return all buffers
    BufferInfo *pBuffer = NULL;
    OMX_U32 nFreeBufferCount = 0;
    while (nFreeBufferCount < m_nOutputBufferCount) {
        VTEST_MSG_LOW("FreeQueue wait...");
        result = m_pFreeBufferQueue->Pop(&pBuffer, sizeof(pBuffer), 0);

        if ((pBuffer == NULL) || (result != OMX_ErrorNone)) {
            VTEST_MSG_ERROR("Error in getting buffer, it might be null");
        } else {
            VTEST_MSG_MEDIUM("PortReconfig free buffer count %u",
                    (unsigned int)nFreeBufferCount);
        }
        nFreeBufferCount++;
    }

    // delete output buffers
    VTEST_MSG_MEDIUM("PortReconfig free OUTPUT buffers");
    m_pBufferManager->FreeBuffers(this, PORT_INDEX_OUT);

    VTEST_MSG_MEDIUM("PortReconfig getting response");
    if (m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0) != OMX_ErrorNone) {
        FAILED1(result, "Error popping signal");
    }
    if (cmd.eResult != OMX_ErrorNone || cmd.eEvent != OMX_EventCmdComplete ||
            cmd.eCmd != OMX_CommandPortDisable) {
        result = OMX_ErrorUndefined;
        FAILED1(result, "expecting complete for command : %d", OMX_CommandPortDisable);
    }
    if (m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0) != OMX_ErrorNone) {
        FAILED1(result, "Error popping signal");
    }
    if (cmd.eResult != OMX_ErrorNone || cmd.eEvent != OMX_EventCmdComplete ||
            cmd.eCmd != OMX_CommandPortDisable) {
        result = OMX_ErrorUndefined;
        FAILED1(result, "expecting complete for command : %d", OMX_CommandPortDisable);
    }

    VTEST_MSG_MEDIUM("PortReconfig got new settings");
    PortBufferCapability req = GetBufferRequirements(PORT_INDEX_OUT);
    OMX_U32 nBufferCount = m_nInputBufferCount;

    // get updated port parameters
    result = SetPortParams(PORT_INDEX_IN,
            nWidth, nHeight, nBufferCount, 0, //FRAME-RATE not used for input port
            &m_nInputBufferSize, &m_nInputBufferCount);
    VTEST_MSG_MEDIUM("PortReconfig Min Buffer Count = %u", (unsigned int)m_nInputBufferCount);
    VTEST_MSG_MEDIUM("PortReconfig Buffer Size = %u", (unsigned int)m_nInputBufferSize);

    VTEST_MSG_MEDIUM("PortReconfig restarting sink");
    result = m_pSink->PortReconfig(PORT_INDEX_IN,
            m_sConfig.nOutputFrameWidth, m_sConfig.nOutputFrameHeight, sRect);
    FAILED1(result, "PortReconfig done failed on sink");

    // enable ports, Encoder work-around requires we enable/disable both ports
    VTEST_MSG_MEDIUM("PortReconfig re-enabling port");
    OMX_SendCommand(m_hEncoder, OMX_CommandPortEnable, PORT_INDEX_IN, 0);
    FAILED1(result, "Enable Input Port failed");
    OMX_SendCommand(m_hEncoder, OMX_CommandPortEnable, PORT_INDEX_OUT, 0);
    FAILED1(result, "Enable Output Port failed");
    m_bPortReconfig = OMX_FALSE;

    // re-allocate output buffers
    VTEST_MSG_MEDIUM("PortReconfig re-allocate OUTPUT buffers");
    result = m_pBufferManager->SetupBufferPool(this, m_pSink);
    FAILED1(result, "Error in realloacting buffer pool");

    /* no need to wait for enable port complete, as input port will only
     * be enabled once BufferPool is setup by the source */

    m_bPortReconfig = OMX_FALSE;
    BufferInfo *pBuffers;
    result = m_pBufferManager->GetBuffers(
            this, PORT_INDEX_OUT, &pBuffers, &nBufferCount);
    FAILED1(result, "Error in Getting Buffers");

    for (OMX_U32 i = 0; i < nBufferCount; i++) {
        VTEST_MSG_MEDIUM("port-reconfig requeue buffer %lu (%p %p)",
                pBuffers[i].pHandle,
                pBuffers[i].pHeaderIn, pBuffers[i].pHeaderOut);
        result = SetBuffer(&pBuffers[i], m_pSink);
        FAILED1(result, "Error setting port buffer %p", pBuffers[i].pHeaderOut);
    }

    VTEST_MSG_MEDIUM("port-reconfig done");
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::ThreadRun(OMX_PTR pThreadData) {

    (void)pThreadData;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_TICKS nTimeStamp = 0;
    OMX_U32 i;

    while (!m_bThreadStop) {
        Sleeper::Sleep(100);
    }

    VTEST_MSG_HIGH("thread exiting...");
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::FreeBuffer(
        BufferInfo *pBuffer, OMX_U32 ePortIndex) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    OMX_BUFFERHEADERTYPE **pHeaderPtr = pBuffer->GetHeaderPtr(ePortIndex);
    if (*pHeaderPtr == NULL) {
        return result;
    }

    VTEST_MSG_MEDIUM("Freeing buffer %p 0x%lu",
            *pHeaderPtr, pBuffer->pHandle);

    result = OMX_FreeBuffer(m_hEncoder, ePortIndex, *pHeaderPtr);
    FAILED1(result, "Error freeing %s", OMX_PORT_NAME(ePortIndex));

    *pHeaderPtr = NULL;
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::SetState(
        OMX_STATETYPE eState, OMX_BOOL bSynchronous) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    // check for pending state transition
    if (m_eState != m_eStatePending) {
        result = WaitState(m_eStatePending);
        FAILED1(result, "wait for %s failed, waiting for alloc/dealloc?!?!",
            OMX_STATE_NAME(m_eStatePending));
    }

    // check for invalid transition
    if ((eState == OMX_StateLoaded && m_eState != OMX_StateIdle) ||
        (eState == OMX_StateExecuting && m_eState != OMX_StateIdle)) {
        VTEST_MSG_ERROR(
            "Error: invalid state tranisition: state %s to %s",
            OMX_STATE_NAME(m_eState), OMX_STATE_NAME(OMX_StateExecuting));
        result = OMX_ErrorIncorrectStateTransition;
    }

    VTEST_MSG_MEDIUM("goto state %s...", OMX_STATE_NAME(eState));
    result = OMX_SendCommand(m_hEncoder,
            OMX_CommandStateSet, (OMX_U32)eState, NULL);
    m_eStatePending = eState;

    FAILED1(result, "failed to set state");
    if (bSynchronous == OMX_TRUE) {
        result = WaitState(eState);
        FAILED1(result, "failed to wait in set state in synchronous mode");
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::WaitState(OMX_STATETYPE eState) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    CmdType cmd;

    VTEST_MSG_MEDIUM("waiting for state %s...", OMX_STATE_NAME(eState));
    (void)m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0); // infinite wait
    result = cmd.eResult;

    if (cmd.eEvent == OMX_EventCmdComplete) {
        if (cmd.eCmd == OMX_CommandStateSet) {
            if ((OMX_STATETYPE)cmd.sCmdData == eState) {
                m_eState = (OMX_STATETYPE)cmd.sCmdData;
            } else {
                VTEST_MSG_ERROR("wrong state (%d)", (int)cmd.sCmdData);
                result = OMX_ErrorUndefined;
            }
        } else {
            VTEST_MSG_ERROR("expecting state change");
            result = OMX_ErrorUndefined;
        }
    } else {
        VTEST_MSG_ERROR("expecting cmd complete");
        result = OMX_ErrorUndefined;
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_MEDIUM("reached state %s...", OMX_STATE_NAME(eState));
    }
    return result;
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::SetIDRPeriod(OMX_S32 nIntraPeriod, OMX_S32 nIDRPeriod) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_VIDEO_CONFIG_AVCINTRAPERIOD idr_period;

    idr_period.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetConfig(m_hEncoder,
                           (OMX_INDEXTYPE)OMX_IndexConfigVideoAVCIntraPeriod,
                           (OMX_PTR) & idr_period);
    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("failed to get state");
        return result;
    }

    idr_period.nPFrames = (OMX_U32)nIntraPeriod - 1;
    idr_period.nIDRPeriod = (OMX_U32)nIDRPeriod;
    result = OMX_SetConfig(m_hEncoder,
            (OMX_INDEXTYPE)OMX_IndexConfigVideoAVCIntraPeriod,
            (OMX_PTR)&idr_period);
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::DeliverInput(OMX_BUFFERHEADERTYPE *pBuffer) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    while (m_sConfig.pDynamicProperties != NULL &&
           (OMX_U32)m_nInBufferCount >= m_sConfig.pDynamicProperties[m_nDynamicIndexCount].frame_num &&
           m_sConfig.pDynamicProperties[m_nDynamicIndexCount].frame_num != 0) {
        ProcessDynamicConfiguration(m_sConfig.pDynamicProperties, m_nDynamicIndexCount);
        m_nDynamicIndexCount++;
    }
#ifndef MSM8974
    if (m_sConfig.nLTRMode == 2) {
        OMX_ERRORTYPE result = OMX_ErrorNone;
        /* Send LTR Use cmd after every (IDR + LTR Period + LTR Period/2) frames */
        if ((m_nInBufferCount % (m_sConfig.nIntraPeriod + m_sConfig.nLTRPeriod +
                             m_sConfig.nLTRPeriod / 2)) == 0) {
            /* Configure LTR use*/
            QOMX_VIDEO_CONFIG_LTRUSE_TYPE  ltrUse;
            OMX_INIT_STRUCT(&ltrUse, QOMX_VIDEO_CONFIG_LTRUSE_TYPE);
            ltrUse.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
            ltrUse.nID = m_LTRId;
            ltrUse.nFrames = 0x7FFFFFFF;
            VTEST_MSG_ERROR("Set LTR use at frame no. %d with LTRId %u "
                            "for following %u frames",
                            (int)m_nInBufferCount, (unsigned int)ltrUse.nID, (unsigned int)ltrUse.nFrames);

            result = OMX_SetConfig(m_hEncoder,
                    (OMX_INDEXTYPE)QOMX_IndexConfigVideoLTRUse,
                    (OMX_PTR)&ltrUse);
            if (result != OMX_ErrorNone) {
                VTEST_MSG_ERROR("Set LTR use failed with status %d ...", result);
            }
        }
    }
#endif
    return OMX_EmptyThisBuffer(m_hEncoder, pBuffer);
}


OMX_ERRORTYPE Encoder::ProcessDynamicConfiguration(
        DynamicConfig *dynamic_config, int index) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    VTEST_MSG_MEDIUM(
        "Processing Dynamic Config. Frame count: %d. Config Index: 0x%x",
        dynamic_config[index].frame_num, dynamic_config[index].config_param);

    if (dynamic_config[index].config_param == OMX_IndexConfigVideoFramerate) {
        float framerate = dynamic_config[index].config_data.f_framerate;
        FractionToQ16(dynamic_config[index].config_data.framerate.xEncodeFramerate,
                      (int)(framerate * 2), 2);
#ifdef MSM8974
        // TODO: figure out how to handle this...
        //result = ChangeFrameRate(framerate);
        result = OMX_ErrorNotImplemented;
        if (result) {
            VTEST_MSG_ERROR("Could not update the framerate");
        }
#endif
    }
    if (dynamic_config[index].config_param ==
            (OMX_INDEXTYPE)QOMX_IndexConfigVideoIntraperiod) {
        QOMX_VIDEO_INTRAPERIODTYPE intra_period;
        intra_period.nPortIndex = PORT_INDEX_OUT;
        result = OMX_GetConfig(m_hEncoder,
                dynamic_config[index].config_param,
                (OMX_PTR)&intra_period);
        if (intra_period.nBFrames) {
            dynamic_config[index].config_data.intraperiod.nPFrames /=
                (intra_period.nBFrames + 1);
            dynamic_config[index].config_data.intraperiod.nBFrames =
                intra_period.nBFrames;
        }
        VTEST_MSG_HIGH("Old: P/B frames = %u/%u",
                (unsigned int)intra_period.nPFrames, (unsigned int)intra_period.nBFrames);
        VTEST_MSG_HIGH("New: P/B frames = %u/%u",
                (unsigned int)dynamic_config[index].config_data.intraperiod.nPFrames,
                (unsigned int)dynamic_config[index].config_data.intraperiod.nBFrames);
    }

    result = OMX_SetConfig(m_hEncoder,
            dynamic_config[index].config_param,
            (OMX_PTR)&dynamic_config[index].config_data);

    if (result == OMX_ErrorNone) {
        VTEST_MSG_MEDIUM(
            "Set Dynamic Config Successfully. Frame count: %d. Config Index: %x",
            dynamic_config[index].frame_num,
            dynamic_config[index].config_param);
    } else {
        VTEST_MSG_ERROR(
            "Failed to set Dynamic Config. Frame count: %d. Config Index: %x",
            dynamic_config[index].frame_num,
            dynamic_config[index].config_param);
    }
    return  result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::DeliverOutput(OMX_BUFFERHEADERTYPE *pBuffer) {

    pBuffer->nFlags = 0;
    return OMX_FillThisBuffer(m_hEncoder, pBuffer);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::RequestIntraVOP() {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_CONFIG_INTRAREFRESHVOPTYPE vop;

    vop.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetConfig(m_hEncoder,
            OMX_IndexConfigVideoIntraVOPRefresh, (OMX_PTR)&vop);

    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("failed to get state");
        return result;
    }
    vop.IntraRefreshVOP = OMX_TRUE;
    result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoIntraVOPRefresh, (OMX_PTR)&vop);
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::SetIntraRefresh(OMX_S32 nIntraRefresh) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_VIDEO_PARAM_INTRAREFRESHTYPE intrarefresh;

    intrarefresh.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetParameter(m_hEncoder,
            (OMX_INDEXTYPE)OMX_IndexParamVideoIntraRefresh,
            (OMX_PTR)&intrarefresh);

    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("failed to get IntraRefresh");
        return result;
    }
    intrarefresh.eRefreshMode = OMX_VIDEO_IntraRefreshCyclic;
    intrarefresh.nCirMBs = nIntraRefresh;
    result = OMX_SetParameter(m_hEncoder,
            (OMX_INDEXTYPE)OMX_IndexParamVideoIntraRefresh,
            (OMX_PTR)&intrarefresh);
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::SetIntraPeriod(OMX_S32 nIntraPeriod) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    QOMX_VIDEO_INTRAPERIODTYPE intra;

    intra.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetConfig(m_hEncoder,
            (OMX_INDEXTYPE)QOMX_IndexConfigVideoIntraperiod,
            (OMX_PTR)&intra);

    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("failed to get state");
        return result;
    }
    intra.nPFrames = (OMX_U32)nIntraPeriod - 1;
    result = OMX_SetConfig(m_hEncoder,
            (OMX_INDEXTYPE)QOMX_IndexConfigVideoIntraperiod,
            (OMX_PTR)&intra);
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::SetPortParams(OMX_U32 ePortIndex,
        OMX_U32 nWidth, OMX_U32 nHeight, OMX_U32 nBufferCountMin,
        OMX_U32 nFrameRate, OMX_U32 *nBufferSize, OMX_U32 *nBufferCount) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    sPortDef.nPortIndex = ePortIndex;
    result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef);
    FAILED1(result, "Error: GET OMX_IndexParamPortDefinition");

    // set frame rate for output port only
    if (ePortIndex == PORT_INDEX_OUT) {
        FractionToQ16(sPortDef.format.video.xFramerate,(int)(nFrameRate*2),2);
    }

    // setup frame width/height
    sPortDef.format.video.nFrameWidth = nWidth;
    sPortDef.format.video.nFrameHeight = nHeight;

    result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef);
    FAILED1(result, "Error: SET OMX_IndexParamPortDefinition");

    result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef);
    FAILED1(result, "Error: GET OMX_IndexParamPortDefinition");

    if ((sPortDef.format.video.nFrameWidth != nWidth) ||
        (sPortDef.format.video.nFrameHeight != nHeight)) {
        VTEST_MSG_ERROR("width %u != %u or height %u != %u\n",
                (unsigned int)sPortDef.format.video.nFrameWidth, (unsigned int)nWidth,
                (unsigned int)sPortDef.format.video.nFrameHeight, (unsigned int)nHeight);
    }

    // setup buffer count
    if (nBufferCountMin < sPortDef.nBufferCountMin) {
        VTEST_MSG_HIGH("nBufferCount %u too small overriding to %u",
            (unsigned int)nBufferCountMin, (unsigned int)sPortDef.nBufferCountActual);
        nBufferCountMin = sPortDef.nBufferCountMin;
    }
    sPortDef.nBufferCountActual = sPortDef.nBufferCountMin = nBufferCountMin;

    result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef);
    FAILED1(result, "Error: GET OMX_IndexParamPortDefinition");

    result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition, (OMX_PTR)&sPortDef);
    FAILED1(result, "Error: SET OMX_IndexParamPortDefinition");

    if (nBufferCountMin != sPortDef.nBufferCountActual) {
        VTEST_MSG_ERROR("Buffer reqs dont match...%u != %u\n",
                (unsigned int)nBufferCountMin, (unsigned int)sPortDef.nBufferCountActual);
    }

    VTEST_MSG_HIGH("%s cfg, width %u, height %u, bufs %u, size %u",
            OMX_PORT_NAME(ePortIndex),
            (unsigned int)sPortDef.format.video.nFrameWidth, (unsigned int)sPortDef.format.video.nFrameHeight,
            (unsigned int)sPortDef.nBufferCountActual, (unsigned int)sPortDef.nBufferSize);

    *nBufferCount = sPortDef.nBufferCountActual;
    *nBufferSize = sPortDef.nBufferSize;
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::ChangeQuality(
        OMX_S32 nFramerate, OMX_S32 nBitrate) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    //////////////////////////////////////////
    // frame rate
    //////////////////////////////////////////

    OMX_CONFIG_FRAMERATETYPE framerate;
    framerate.nPortIndex = (OMX_U32)PORT_INDEX_IN;
    result = OMX_GetConfig(m_hEncoder,
            OMX_IndexConfigVideoFramerate, (OMX_PTR)&framerate);
    if (result != OMX_ErrorNone) {
        return result;
    }

    FractionToQ16(framerate.xEncodeFramerate, (int)(nFramerate * 2), 2);
    result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoFramerate, (OMX_PTR)&framerate);
    if (result != OMX_ErrorNone) {
        return result;
    }

    //////////////////////////////////////////
    // bitrate
    //////////////////////////////////////////
    OMX_VIDEO_CONFIG_BITRATETYPE bitrate;
    bitrate.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    result = OMX_GetConfig(m_hEncoder,
            OMX_IndexConfigVideoBitrate, (OMX_PTR)&bitrate);
    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("error changing quality");
        return result;
    }
    bitrate.nEncodeBitrate = (OMX_U32)nBitrate;
    result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoBitrate, (OMX_PTR)&bitrate);
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::EventCallback(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData, OMX_IN OMX_EVENTTYPE eEvent,
        OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2, OMX_IN OMX_PTR pEventData) {

    (void)hComponent; (void)pEventData;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    Encoder *pThis = (Encoder *)pAppData;

    if (eEvent == OMX_EventCmdComplete) {
        if ((OMX_COMMANDTYPE)nData1 == OMX_CommandStateSet) {
            VTEST_MSG_HIGH("Event callback: state is %s",
                           OMX_STATE_NAME((OMX_STATETYPE)nData2));

            CmdType cmd;
            cmd.eEvent = OMX_EventCmdComplete;
            cmd.eCmd = OMX_CommandStateSet;
            cmd.sCmdData = nData2;
            cmd.eResult = result;
            result = pThis->m_pSignalQueue->Push(&cmd, sizeof(cmd));
            FAILED1(result, "push to signal queue failed");

        } else if ((OMX_COMMANDTYPE)nData1 == OMX_CommandFlush) {
            VTEST_MSG_MEDIUM("Event callback: flush complete on port : %s",
                    OMX_PORT_NAME(nData2));

            CmdType cmd;
            cmd.eEvent = OMX_EventCmdComplete;
            cmd.eCmd = OMX_CommandFlush;
            cmd.sCmdData = 0;
            cmd.eResult = result;
            result = pThis->m_pSignalQueue->Push(&cmd, sizeof(cmd));
            FAILED1(result, "push to signal queue failed");

        } else if ((OMX_COMMANDTYPE)nData1 == OMX_CommandPortDisable) {
            VTEST_MSG_MEDIUM("Event callback: %s port disable",
                    OMX_PORT_NAME(nData2));

            /* Only queue port disable during port reconfig,
             * during stop, it clashes with the event for moving
             * to loaded state */
            if (pThis->m_bPortReconfig) {
                CmdType cmd;
                cmd.eEvent = OMX_EventCmdComplete;
                cmd.eCmd = OMX_CommandPortDisable;
                cmd.sCmdData = 0;
                cmd.eResult = result;
                result = pThis->m_pSignalQueue->Push(&cmd, sizeof(cmd));
                FAILED1(result, "push to signal queue failed");
            }

        } else if ((OMX_COMMANDTYPE)nData1 == OMX_CommandPortEnable) {
            VTEST_MSG_MEDIUM("Event callback: %s port enable",
                    OMX_PORT_NAME(nData2));
        } else {
            result = OMX_ErrorUndefined;
            FAILED2(result, pThis->m_bThreadStop, "Unimplemented command");
        }
    } else if (eEvent == OMX_EventError) {
        VTEST_MSG_ERROR("Event callback: async error nData1 %u, nData2 %u",
                        (unsigned int)nData1, (unsigned int)nData2);
        if (QOMX_ErrorLTRUseFailed == (OMX_S32)nData1) {
            VTEST_MSG_ERROR("LTR use failed error with LTRID %u", (unsigned int)nData2);
        }
        CmdType cmd;
        cmd.eEvent = OMX_EventError;
        cmd.eCmd = OMX_CommandMax;
        cmd.sCmdData = 0;
        cmd.eResult = (OMX_ERRORTYPE)nData1;
        result = pThis->m_pSignalQueue->Push(&cmd, sizeof(cmd));
        FAILED1(result, "push to signal queue failed");

    } else if (eEvent == OMX_EventBufferFlag) {
        VTEST_MSG_MEDIUM("Event callback: got buffer flag event");
    } else {
        result = OMX_ErrorUndefined;
        FAILED2(result, pThis->m_bThreadStop, "Unimplemented event");
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::EmptyDoneCallback(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData, OMX_IN OMX_BUFFERHEADERTYPE *pHeader) {

    (void)hComponent;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    Encoder *pThis = (Encoder *)pAppData;
    BufferInfo *pBuffer = NULL;

    VTEST_MSG_MEDIUM("EBD size=%u", (unsigned int)pHeader->nFilledLen);
    result = pThis->m_pBufferManager->GetBuffer(
            pThis, PORT_INDEX_IN, pHeader, &pBuffer);
    FAILED2(result, pThis->m_bThreadStop,
            "Error in finding buffer %p", pBuffer);

    Mutex::Autolock autoLock(pThis->m_pLock);

    /* TODO : give all buffers back to Buffer Manager */
    if (pThis->m_bThreadStop || pThis->m_bInputEOSReached) {
        result = pThis->m_pFreeBufferQueue->Push(&pBuffer, sizeof(BufferInfo **));
        return result;
    }
    result = pThis->m_pSource->SetBuffer(pBuffer, pThis);
    if (result != OMX_ErrorNone) {
        /* Don't treat it as fatal, because there is possibility where the
         * eos hasn't reached us and source is not expecting any more buffers */
        VTEST_MSG_HIGH("SetBuffer on source failed pBuffer: %p", pBuffer);
        pThis->m_pFreeBufferQueue->Push(&pBuffer, sizeof(BufferInfo **));
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::FillDoneCallback(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData, OMX_IN OMX_BUFFERHEADERTYPE *pHeader) {

    (void)hComponent;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    Encoder *pThis = (Encoder *)pAppData;
    BufferInfo *pBuffer = NULL;

    VTEST_MSG_MEDIUM("FBD size=%u", (unsigned int)pHeader->nFilledLen);
    result = pThis->m_pBufferManager->GetBuffer(
        pThis, PORT_INDEX_OUT, pHeader, &pBuffer);
    FAILED2(result, pThis->m_bThreadStop,
            "Error in finding buffer %p", pBuffer);

    Mutex::Autolock autoLock(pThis->m_pLock);

    /* TODO : give all buffers back to Buffer Manager */
    if (pThis->m_bPortReconfig || pThis->m_bThreadStop) {
        result = pThis->m_pFreeBufferQueue->Push(&pBuffer, sizeof(BufferInfo**));
        return result;
    }

    result = pThis->m_pSink->SetBuffer(pBuffer, pThis);
    if (pHeader->nFlags & OMX_BUFFERFLAG_EOS) {
        VTEST_MSG_MEDIUM("got output EOS\n");
        pThis->m_bThreadStop = OMX_TRUE;
    }
    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("SetBuffer on sink failed pBuffer: %p", pBuffer);
        pThis->m_bThreadStop = OMX_TRUE;
        pThis->m_pFreeBufferQueue->Push(&pBuffer, sizeof(BufferInfo **));
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::GetComponentRole(OMX_BOOL bSecureSession,
        OMX_VIDEO_CODINGTYPE eCodec, OMX_STRING *role) {

    if (bSecureSession) {
        switch (eCodec) {
            case OMX_VIDEO_CodingAVC:
                break;
            default:
                VTEST_MSG_ERROR("Secure session not supported for codec format : %d",
                                eCodec);
                return OMX_ErrorBadParameter;
        }
    }

    switch (eCodec) {
        case OMX_VIDEO_CodingAVC:
            if (bSecureSession) {
                *role = (OMX_STRING)"OMX.qcom.video.encoder.avc.secure";
            } else {
                *role = (OMX_STRING)"OMX.qcom.video.encoder.avc";
            }
            break;
        case OMX_VIDEO_CodingMPEG4:
            *role = (OMX_STRING)"OMX.qcom.video.encoder.mpeg4";
            break;
        case OMX_VIDEO_CodingH263:
            *role = (OMX_STRING)"OMX.qcom.video.encoder.h263";
            break;
        case OMX_VIDEO_CodingVPX:
            *role = (OMX_STRING)"OMX.qcom.video.encoder.vp8";
            break;
        default:
            VTEST_MSG_ERROR("Error: Unsupported codec %d", eCodec);
            return OMX_ErrorBadParameter;
    }
    VTEST_MSG_MEDIUM("Role : %s, Codec Format : %d\n", *role, eCodec);
    return OMX_ErrorNone;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Encoder::Flush()
{
    OMX_ERRORTYPE result = OMX_ErrorNone;
    CmdType cmd;

    result = OMX_SendCommand(m_hEncoder, OMX_CommandFlush, OMX_ALL, 0);
    FAILED1(result, "Flush failed");

    if (m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0) != OMX_ErrorNone) {
        FAILED1(result, "Error popping signal");
    }
    if (cmd.eResult != OMX_ErrorNone || cmd.eEvent != OMX_EventCmdComplete ||
        cmd.eCmd != OMX_CommandFlush) {
        result = OMX_ErrorUndefined;
        VTEST_MSG_ERROR("Was expecting complete for flush command");
    }

    if (m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0) != OMX_ErrorNone) {
        FAILED1(result, "Error popping signal");
    }
    if (cmd.eResult != OMX_ErrorNone || cmd.eEvent != OMX_EventCmdComplete ||
        cmd.eCmd != OMX_CommandFlush) {
        result = OMX_ErrorUndefined;
        VTEST_MSG_ERROR("Was expecting complete for flush command");
    }
    return result;
}

} // namespace vtest
