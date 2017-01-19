/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
--------------------------------------------------------------------*/

#include "vtest_ComDef.h"
#include "vtest_Debug.h"
#include "vtest_TestDecode.h"
#include "vtest_Time.h"
#include "vtest_Decoder.h"
#include "vtest_DecoderFileSource.h"
#include "vtest_DecoderFileSink.h"
#include "vtest_NativeWindow.h"
#include "vtest_MdpOverlaySink.h"
#include "vtest_BufferManager.h"

#undef LOG_TAG
#define LOG_TAG "VTEST_TEST_DECODE"

namespace vtest {

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
TestDecode::TestDecode()
    : ITestCase(),    // invoke the base class constructor
      m_pSource(NULL),
      m_pSink(NULL),
      m_pDecoder(NULL),
      m_pBufferManager(NULL),
      m_pCrypto(NULL) {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
TestDecode::~TestDecode() {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE TestDecode::Execute(CodecConfigType *pConfig,
        DynamicConfigType *pDynamicConfig, OMX_S32 nTestNum) {

    (void)pDynamicConfig; (void)nTestNum;
    OMX_ERRORTYPE result = OMX_ErrorNone;

    OMX_TICKS nStartTime = 0;
    OMX_TICKS nEndTime;
    OMX_TICKS nRunTimeSec;
    OMX_TICKS nRunTimeMillis;

    if (result == OMX_ErrorNone && pConfig->bSecureSession) {
        VTEST_MSG_HIGH("Creating Cryto Manager...");
        m_pCrypto = new Crypto();

        result = CheckError(m_pCrypto->Init());
        FAILED(result, "Crypto Init failed");
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("Creating Buffer Manager...");
        m_pBufferManager = new BufferManager();

        VTEST_MSG_HIGH("Creating file source...");
        m_pSource = new DecoderFileSource(m_pCrypto);

        VTEST_MSG_HIGH("Creating decoder...");
        m_pDecoder = new Decoder(pConfig);

        VTEST_MSG_HIGH("Creating sink...");
        switch (pConfig->eSinkType) {
            case NativeWindow_Sink:
                m_pSink = new NativeWindow(pConfig->bRotateDisplay);
                break;
            case MDPOverlay_Sink:
                m_pSink = new MdpOverlaySink(pConfig->bRotateDisplay);
                break;
            case File_Sink:
                m_pSink = new DecoderFileSink(m_pCrypto);
                break;
            default:
                m_pSink = NULL;
                result = OMX_ErrorBadParameter;
        }
   }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("Configuring Source");
        result = CheckError(m_pSource->Configure(pConfig, m_pBufferManager, NULL, m_pDecoder));
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("Configuring Decoder");
        result = CheckError(m_pDecoder->Configure(pConfig, m_pBufferManager, m_pSource, m_pSink));
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("Configuring Sink");
        result = CheckError(m_pSink->Configure(pConfig, m_pBufferManager, m_pDecoder, NULL));
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("Configuring buffer pools");
        result = CheckError(m_pBufferManager->SetupBufferPool(m_pSource, m_pDecoder));
    }

    if (result == OMX_ErrorNone) {
        result = CheckError(m_pBufferManager->SetupBufferPool(m_pDecoder, m_pSink));
    }

    //==========================================
    // Start the decoder
    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("starting the decoder");
        nStartTime = Time::GetTimeMicrosec();
        result = CheckError(m_pDecoder->Start());
    }

    //==========================================
    // Wait for the decoder to finish decoding all frames
    VTEST_MSG_HIGH("waiting for decoder to finish...");
    result = CheckError(m_pDecoder->Finish());
    VTEST_MSG_HIGH("decoder is finished %d", result);

    //==========================================
    // Stop the decoder
    VTEST_MSG_HIGH("Stopping the decoder");
    result = CheckError(m_pDecoder->Stop());
    VTEST_MSG_HIGH("decoder is stopped %d", result);

    //==========================================
    // Compute stats
    nEndTime = Time::GetTimeMicrosec();
    nRunTimeMillis = (nEndTime - nStartTime) / 1000;   // convert to millis
    nRunTimeSec = nRunTimeMillis / 1000;               // convert to seconds

    VTEST_MSG_PROFILE("Time = %d millis", (int)nRunTimeMillis);

    //==========================================
    // Free buffers and classes
    VTEST_MSG_HIGH("freeing all buffers");
    if (m_pBufferManager) {
        m_pBufferManager->FreeBuffers(m_pDecoder, PORT_INDEX_IN);
        m_pBufferManager->FreeBuffers(m_pDecoder, PORT_INDEX_OUT);
    }

    VTEST_MSG_HIGH("deleting all objects");
    if (m_pSink) delete m_pSink;
    if (m_pDecoder) delete m_pDecoder;
    if (m_pSource) delete m_pSource;
    if (m_pBufferManager) delete m_pBufferManager;
    if (m_pCrypto != NULL) {
        m_pCrypto->Terminate();
        delete m_pCrypto;
    }

    return result;
}

OMX_ERRORTYPE TestDecode::ValidateAssumptions(CodecConfigType *pConfig,
        DynamicConfigType *pDynamicConfig) {

    (void)pConfig; (void)pDynamicConfig;
    return OMX_ErrorNone;
}

} // namespace vtest
