/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
--------------------------------------------------------------------*/

#include "venc/inc/omx_video_common.h"
#include "vtest_ComDef.h"
#include "vtest_Debug.h"
#include "vtest_Thread.h"
#include "vtest_SignalQueue.h"
#include "vtest_Sleeper.h"
#include "vtest_File.h"
#include "vtest_Time.h"
#include "vtest_Parser.h"
#include "vtest_DecoderFileSink.h"

#undef LOG_TAG
#define LOG_TAG "VTEST_DECODE_FILE_SINK"

namespace vtest {

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
DecoderFileSink::DecoderFileSink(Crypto *pCrypto)
    : ISource(),
      m_nFrames(0),
      m_nFrameWidth(0),
      m_nFrameHeight(0),
      m_pFile(NULL),
      m_nFileFd(-1),
      m_bSecureSession(OMX_FALSE),
      m_pCrypto(pCrypto) {

    sprintf(m_pName, "DecoderFileSink");
    VTEST_MSG_LOW("%s: created...", Name());
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
DecoderFileSink::~DecoderFileSink() {

    if (m_pFile != NULL) {
        m_pFile->Close();
        delete m_pFile;
        m_pFile = NULL;
    }
    if (m_nFileFd != -1) {
        close(m_nFileFd);
        m_nFileFd = -1;
    }
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
PortBufferCapability DecoderFileSink::GetBufferRequirements(OMX_U32 ePortIndex) {

    PortBufferCapability sBufCap;

    Mutex::Autolock autoLock(m_pLock);
    memset(&sBufCap, 0, sizeof(PortBufferCapability));

    if (ePortIndex == PORT_INDEX_IN) {
        sBufCap.bAllocateBuffer = OMX_FALSE;
        sBufCap.bUseBuffer = OMX_TRUE;
        sBufCap.pSource = this;
        sBufCap.ePortIndex = ePortIndex;
        sBufCap.nMinBufferSize = 0x1000;
        sBufCap.nMinBufferCount = 1;
        sBufCap.nBufferUsage = 0;
        sBufCap.nExtraBufferCount = 0;
    } else {
        VTEST_MSG_ERROR("Error: invalid port selection");
    }
    return sBufCap;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE DecoderFileSink::Configure(CodecConfigType *pConfig,
        BufferManager *pBufManager, ISource *pSource, ISource *pSink) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    result = ISource::Configure(pConfig, pBufManager, pSource, pSink);
    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("DecoderFileSink configure failed");
        return result;
    }

    Mutex::Autolock autoLock(m_pLock);

    char *pFileName = pConfig->cOutFileName;
    m_bSecureSession = pConfig->bSecureSession;

    if (pConfig->nFramerate > 0 &&
        pConfig->nFrameWidth > 0 &&
        pConfig->nFrameHeight > 0) {

        m_nFramerate = pConfig->nFramerate;
        m_nFrameWidth = pConfig->nFrameWidth;
        m_nFrameHeight = pConfig->nFrameHeight;
        m_nFrames = pConfig->nFrames;

        if ((pFileName != NULL) &&
            (Parser::StringICompare((OMX_STRING)"", pFileName) != 0)) {

            m_pFile = new File();
            if (m_pFile != NULL) {
                VTEST_MSG_MEDIUM("Opening output file...");
                result = m_pFile->Open(pFileName, OMX_FALSE);
                if (result != OMX_ErrorNone) {
                    VTEST_MSG_ERROR("Failed to open file");
                }
            } else {
                VTEST_MSG_ERROR("Failed to allocate file");
                result = OMX_ErrorInsufficientResources;
            }
        } else {
            VTEST_MSG_MEDIUM("No output file");
        }
    } else {
        VTEST_MSG_ERROR("bad params in config");
        result = OMX_ErrorBadParameter;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE DecoderFileSink::SetBuffer(
        BufferInfo *pBuffer, ISource *pSource) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    result = ISource::SetBuffer(pBuffer, pSource);
    if (result != OMX_ErrorNone) {
        return result;
    }

    VTEST_MSG_LOW("queue push (%p %p)", pBuffer->pHeaderIn, pBuffer->pHeaderOut);
    result = m_pBufferQueue->Push(&pBuffer, sizeof(BufferInfo**));
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE DecoderFileSink::ThreadRun(OMX_PTR pThreadData) {

    (void)pThreadData;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *pHeader = NULL;
    BufferInfo *pBuffer = NULL;

    for (OMX_U32 i = 1; !m_bThreadStop; i++) {

        result = m_pBufferQueue->Pop(&pBuffer, sizeof(pBuffer), 0);
        VTEST_MSG_LOW("queue pop %u of %u (qsize %u)",
                (unsigned int)i, (unsigned int)m_nFrames, (unsigned int)m_pBufferQueue->GetSize());

        if ((pBuffer == NULL) || (result != OMX_ErrorNone)) {
            VTEST_MSG_ERROR("Error in queue pop, stopping thread");
            m_bThreadStop = OMX_TRUE;
            continue;
        }

        pHeader = pBuffer->pHeaderIn;
        if (pHeader->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) {
            VTEST_MSG_HIGH("frames written %u", (unsigned int)i);
        }

        if (pHeader->nFlags & OMX_BUFFERFLAG_EOS) {
            VTEST_MSG_HIGH("got EOS for frame : %u", (unsigned int)i);
            m_bThreadStop = OMX_TRUE;
        }

        if ((m_pFile != NULL) && (!m_bSecureSession)) {

            if (pHeader->nFilledLen > 0) {
                VTEST_MSG_MEDIUM("writing frame %u with %u bytes...",
                        (unsigned int)i, (unsigned int)pHeader->nFilledLen);
                /*
                   unsigned int stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, m_nFrameWidth);
                   unsigned int scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, m_nFrameHeight);
                   char *temp = (char *) pBuffer->pBuffer;
                   int i = 0;

                   temp += (stride * (int)crop_rect.nTop) +  (int)crop_rect.nLeft;
                   for (i = 0; i < crop_rect.nHeight; i++) {
                   bytes_written = fwrite(temp, crop_rect.nWidth, 1, outputBufferFile);
                   temp += stride;
                   }

                   temp = (char *)pBuffer->pBuffer + stride * scanlines;
                   temp += (stride * (int)crop_rect.nTop) +  (int)crop_rect.nLeft;
                   for (i = 0; i < crop_rect.nHeight/2; i++) {
                   bytes_written += fwrite(temp, crop_rect.nWidth, 1, outputBufferFile);
                   temp += stride;
                   }
                   */
                OMX_S32 nBytes;
                result = m_pFile->Write(pHeader->pBuffer,
                        pHeader->nFilledLen, &nBytes);

                if (result != OMX_ErrorNone) {
                    VTEST_MSG_ERROR("Error writing to file...");
                    m_bThreadStop = OMX_TRUE;
                } else if ((OMX_U32)nBytes != pHeader->nFilledLen) {
                    VTEST_MSG_ERROR("Error mismatched number of bytes in file write");
                    m_bThreadStop = OMX_TRUE;
                    result = OMX_ErrorUndefined;
                }
            } else {
                VTEST_MSG_HIGH("skipping frame %u, 0 length...", (unsigned int)i);
            }
        } else {
            VTEST_MSG_MEDIUM("received frame %u...", (unsigned int)i);
        }
        m_pSource->SetBuffer(pBuffer, this);
    }

    //clean up
    while(m_pBufferQueue->GetSize() > 0) {
        VTEST_MSG_LOW("cleanup: q-wait (qsize %u)", (unsigned int)m_pBufferQueue->GetSize());
        result = m_pBufferQueue->Pop(&pBuffer, sizeof(BufferInfo **), 0);
        m_pSource->SetBuffer(pBuffer, this);
    }

    VTEST_MSG_HIGH("thread exiting...");
    return result;
}

} // namespace vtest
