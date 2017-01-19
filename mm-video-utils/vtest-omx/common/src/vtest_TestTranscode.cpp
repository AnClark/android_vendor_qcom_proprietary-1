/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
--------------------------------------------------------------------*/

#include "vtest_ComDef.h"
#include "vtest_Debug.h"
#include "vtest_Time.h"
#include "vtest_BufferManager.h"
#include "vtest_TestTranscode.h"
#include "vtest_DecoderFileSource.h"
#include "vtest_Decoder.h"
#include "vtest_Encoder.h"
#include "vtest_EncoderFileSink.h"
#include "vtest_NativeWindow.h"

#undef LOG_TAG
#define LOG_TAG "VTEST_TEST_TRANSCODE"

namespace vtest {

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
TestTranscode::TestTranscode()
    : ITestCase(),
      m_pCrypto(NULL) {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
TestTranscode::~TestTranscode() {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE TestTranscode::Execute(CodecConfigType *pConfig,
        DynamicConfigType *pDynamicConfig, OMX_S32 nTestNum) {

    (void)pDynamicConfig; (void)nTestNum;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    vector<ISource*>::const_iterator pSrc;
    OMX_U32 i;

#if 1
    // transcode only works with smoothstreaming due to OMX encoder bugs:
    // 1) encoder: cannot disable only InputPort
    // 2) encoder: cannot disable port on without total reallocation
    // 3) encoder: cannont set buffer-count in enabled state
    pConfig->ePlaybackMode = QCSmoothStreaming;
    //pConfig->nFrameWidth = 1920;
    //pConfig->nFrameHeight = 1080;
#endif

    if (pConfig->bSecureSession) {
        VTEST_MSG_HIGH("Creating Cryto Manager...");
        m_pCrypto = new Crypto();

        result = CheckError(m_pCrypto->Init());
        FAILED(result, "Crypto Init failed");
    }

    VTEST_MSG_HIGH("Creating Buffer Manager...");
    m_pBufferManager = new BufferManager();

    VTEST_MSG_HIGH("Creating Source Objects...");
    m_pSources.push_back(new DecoderFileSource(m_pCrypto));
    m_pSources.push_back(new Decoder(pConfig));
#if 1 //Transocde and display mode, uses shared buffer betweem dec-nw-end
    m_pSources.push_back(new NativeWindow(pConfig->bRotateDisplay));
#endif
    m_pSources.push_back(new Encoder(pConfig));
    m_pSources.push_back(new EncoderFileSink(m_pCrypto));

    VTEST_MSG_HIGH("Configuring Source Objects...");
    for (i = 0; i < m_pSources.size(); i++) {
        ISource *prev = (i == 0 ? NULL : m_pSources[i-1]);
        ISource *next = (i == m_pSources.size()-1 ? NULL : m_pSources[i+1]);
        VTEST_MSG_HIGH("Linking %s: %s ==> %s ==> %s",
                m_pSources[i]->Name(), (prev != NULL ? prev->Name() : "NONE"),
                m_pSources[i]->Name(), (next != NULL ? next->Name() : "NONE"));
        result = CheckError(m_pSources[i]->Configure(pConfig, m_pBufferManager, prev, next));
        if (result != OMX_ErrorNone) {
            VTEST_MSG_ERROR("Config %s", m_pSources[i]->Name());
            break;
        }
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("Configuring Buffer Pools...");
        for (i = 0; i < (m_pSources.size() - 1); i++) {
            result = CheckError(m_pBufferManager->SetupBufferPool(m_pSources[i], m_pSources[i+1]));
            if (result != OMX_ErrorNone) {
                VTEST_MSG_ERROR("buffer alloc %s %s",
                    m_pSources[i]->Name(), m_pSources[i+1]->Name());
                break;
            }
        }
    }

    if (result == OMX_ErrorNone) {
        VTEST_MSG_HIGH("TestChain starting the video chain...");
        /*start/stop the last node so that we can transcode all frames*/
        result = CheckError(m_pSources[m_pSources.size() - 1]->Start());
        FAILED(result, "TestChain: Error during start");
    }

    VTEST_MSG_HIGH("TestChain waiting for finish...");
    result = CheckError(m_pSources[m_pSources.size() - 1]->Finish());
    FAILED(result, "TestChain: Error during run");

    VTEST_MSG_HIGH("TestChain stopping chain...");
    result = CheckError(m_pSources[m_pSources.size() - 1]->Stop());
    FAILED(result, "TestChain: Error during stop");

    //==========================================
    // Clean up
    VTEST_MSG_HIGH("Deleting Buffer Pools...");
    for (i = 0; i < (m_pSources.size() - 1); i++) {
        if (m_pBufferManager) {
            m_pBufferManager->FreeBuffers(m_pSources[i], PORT_INDEX_OUT);
        }
        delete m_pSources[i];
    }
    // free the last source
    delete m_pSources[i];

    return result;
}

OMX_ERRORTYPE TestTranscode::ValidateAssumptions(CodecConfigType *pConfig,
        DynamicConfigType *pDynamicConfig) {

    (void)pConfig; (void)pDynamicConfig;
    return OMX_ErrorNone;
}

} // namespace vtest
