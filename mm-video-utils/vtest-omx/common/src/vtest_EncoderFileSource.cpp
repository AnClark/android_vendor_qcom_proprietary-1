/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential

Copyright (c) 2010 The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#include "venc/inc/omx_video_common.h"
#include "vtest_ComDef.h"
#include "vtest_Debug.h"
#include "vtest_Thread.h"
#include "vtest_Sleeper.h"
#include "vtest_File.h"
#include "vtest_Time.h"
#include "vtest_SignalQueue.h"
#include "vtest_BufferManager.h"
#include "vtest_EncoderFileSource.h"

#undef LOG_TAG
#define LOG_TAG "VTEST_ENCODER_FILE_SOURCE"

namespace vtest {

static const OMX_S32 MAX_BUFFER_ASSUME = 16;

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
EncoderFileSource::EncoderFileSource(Crypto *pCrypto)
    : ISource(),
      m_nFrames(0),
      m_nFramerate(0),
      m_nFrameWidth(0),
      m_nFrameHeight(0),
      m_nBuffers(0),
      m_nDVSXOffset(0),
      m_nDVSYOffset(0),
      m_pFile(NULL),
      m_bIsProfileMode(OMX_FALSE),
      m_bSecureSession(OMX_FALSE),
      m_bIsVTPath(OMX_FALSE),
      m_pInCopyBuf(NULL),
      m_nInCopyBufSize(0),
      m_pCrypto(pCrypto) {

    sprintf(m_pName, "EncoderFileSource");
    VTEST_MSG_LOW("%s: created...", Name());
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
EncoderFileSource::~EncoderFileSource() {

    VTEST_MSG_LOW("start");
    if (m_pFile != NULL) {
        (void)m_pFile->Close();
        delete m_pFile;
    }
    if (m_pInCopyBuf != NULL) {
        delete m_pInCopyBuf;
    }
    VTEST_MSG_LOW("done");
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
PortBufferCapability EncoderFileSource::GetBufferRequirements(OMX_U32 ePortIndex) {

    PortBufferCapability sBufCap;

    Mutex::Autolock autoLock(m_pLock);
    memset(&sBufCap, 0, sizeof(PortBufferCapability));

    if (ePortIndex == PORT_INDEX_OUT) {
        sBufCap.bAllocateBuffer = OMX_FALSE;
        sBufCap.bUseBuffer = OMX_TRUE;
        sBufCap.pSource = this;
        sBufCap.ePortIndex = ePortIndex;
        sBufCap.nMinBufferSize = 0x1000;
        sBufCap.nMinBufferCount = 1;
        sBufCap.nExtraBufferCount = 0;
        sBufCap.nBufferUsage = 0;
    } else {
        VTEST_MSG_ERROR("Error: invalid port selection");
    }
    return sBufCap;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EncoderFileSource::Configure(CodecConfigType *pConfig,
        BufferManager *pBufManager, ISource *pSource, ISource *pSink) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    result = ISource::Configure(pConfig, pBufManager, pSource, pSink);
    if (result != OMX_ErrorNone) {
        VTEST_MSG_ERROR("EncoderFileSource configure failed");
        return result;
    }

    Mutex::Autolock autoLock(m_pLock);

    OMX_STRING pFileName = pConfig->cInFileName;
    m_bSecureSession = pConfig->bSecureSession;

    if (pConfig->nFrames > 0 &&
        pConfig->nFramerate > 0 &&
        pConfig->nFrameWidth > 0 &&
        pConfig->nFrameHeight > 0 &&
        pConfig->nDVSXOffset >= 0 &&
        pConfig->nDVSYOffset >= 0 &&
        pConfig->nInBufferCount > 0 &&
        pConfig->nInBufferCount <= MAX_BUFFER_ASSUME) {
        m_nFrames = pConfig->nFrames;
        m_nFramerate = pConfig->nFramerate;
        m_nFrameWidth = pConfig->nFrameWidth;
        m_nFrameHeight = pConfig->nFrameHeight;
        m_nBuffers = pConfig->nInBufferCount;
        m_nDVSXOffset = pConfig->nDVSXOffset;
        m_nDVSYOffset = pConfig->nDVSYOffset;
        m_bIsProfileMode = pConfig->bProfileMode;

        if (pFileName != NULL) {
            m_pFile = new File();
            if (m_pFile != NULL) {
                result = m_pFile->Open(pFileName, OMX_TRUE);
                if (result != OMX_ErrorNone) {
                    VTEST_MSG_ERROR("Failed to open file");
                }
            } else {
                VTEST_MSG_ERROR("Failed to allocate file");
                result = OMX_ErrorInsufficientResources;
            }
        }
    } else {
        VTEST_MSG_ERROR("bad params");
        result = OMX_ErrorBadParameter;
    }

    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EncoderFileSource::SetBuffer(
        BufferInfo *pBuffer, ISource *pSource) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    result = ISource::SetBuffer(pBuffer, pSource);
    if (result != OMX_ErrorNone) {
        return result;
    }

    VTEST_MSG_LOW("queue push (%p %p)", pBuffer->pHeaderIn, pBuffer->pHeaderOut);
    result = m_pBufferQueue->Push(&pBuffer, sizeof(BufferInfo **));
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EncoderFileSource::ChangeFrameRate(OMX_S32 nFramerate) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    Mutex::Autolock autoLock(m_pLock);

    if (nFramerate > 0) {
        m_nFramerate = nFramerate;
    } else {
        VTEST_MSG_ERROR("bad frame rate");
        result = OMX_ErrorBadParameter;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EncoderFileSource::ThreadRun(OMX_PTR pThreadData) {

    (void)pThreadData;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *pHeader = NULL;
    BufferInfo *pBuffer = NULL;
    OMX_S32 nFrameBytes = m_nFrameWidth * m_nFrameHeight * 3 / 2;
    OMX_TICKS nTimeStamp = 0;

    for (OMX_S32 i = 1; i <= m_nFrames && !m_bThreadStop; i++) {
        // Since frame rate can change at any time, let's make sure that we use
        // the same frame rate for the duration of this loop iteration
        OMX_S32 nFramerate = m_nFramerate;

        // If in live mode we deliver frames in a real-time fashion
        if (m_bIsProfileMode) {
            VTEST_MSG_LOW("delaying frame %u ms", (unsigned int)(1000 / nFramerate));
            Sleeper::Sleep(1000 / nFramerate);
        }

        result = m_pBufferQueue->Pop(&pBuffer, sizeof(BufferInfo**), 2000);
        VTEST_MSG_LOW("queue pop %u of %u...", (unsigned int)i, (unsigned int)m_nFrames);

        if ((pBuffer == NULL) || (result != OMX_ErrorNone)) {
            VTEST_MSG_ERROR("Error queue pop (%p 0x%x), stopping thread",
                            pBuffer, result);
            m_bThreadStop = OMX_TRUE;
            continue;
        }
        pHeader = pBuffer->pHeaderOut;
        pHeader->nInputPortIndex = 0;
        pHeader->nOffset = 0;
        pHeader->nTimeStamp = 0;
        pHeader->nFlags = 0;

        result = FillBuffer(pHeader, nFrameBytes);
        if (result != OMX_ErrorNone) {
            m_bThreadStop = OMX_TRUE;
            continue;
        }

        // set the EOS flag if this is the last frame
        if (i >= m_nFrames) {
            pHeader->nFlags = OMX_BUFFERFLAG_EOS;
            VTEST_MSG_HIGH("enable OMX_BUFFERFLAG_EOS on frame %u", (unsigned int)i);
            m_bThreadStop = OMX_TRUE;
        }

        pHeader->nFilledLen = nFrameBytes;
        if (m_bIsProfileMode) {
            nTimeStamp = (OMX_TICKS)Time::GetTimeMicrosec();
        } else {
            nTimeStamp = nTimeStamp + (OMX_TICKS)(1000000 / nFramerate);
        }
        pHeader->nTimeStamp = nTimeStamp;

        VTEST_MSG_MEDIUM("delivering frame %u", (unsigned int)i);
        pHeader->nOffset = ((m_nFrameWidth * m_nDVSYOffset) + m_nDVSXOffset) * 3 / 2;
        m_pSink->SetBuffer(pBuffer, this);
    } // end for-loop

    //clean up
    while(m_pBufferQueue->GetSize() > 0) {
        VTEST_MSG_LOW("cleanup: q-wait (qsize %u)", (unsigned int)m_pBufferQueue->GetSize());
        result = m_pBufferQueue->Pop(&pBuffer, sizeof(BufferInfo **), 0);
        m_pSink->SetBuffer(pBuffer, this);
    }

    VTEST_MSG_HIGH("thread exiting...");
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EncoderFileSource::FillBuffer(
        OMX_BUFFERHEADERTYPE *pBuffer, OMX_S32 nFrameBytes) {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_S32 nBytesRead;
    OMX_U8 *buffer;

    if (m_pFile == NULL) {
        VTEST_MSG_ERROR("EncoderFileSource file is null");
        return OMX_ErrorBadParameter;
    }
    if (nFrameBytes > (OMX_S32)pBuffer->nAllocLen) {
        VTEST_MSG_ERROR("EncoderFileSource buffer to small");
        return OMX_ErrorBadParameter;
    }

    // allocate scratch buffer for secure sessions
    if (m_bSecureSession && m_pInCopyBuf == NULL) {
        m_nInCopyBufSize = pBuffer->nAllocLen;
        m_pInCopyBuf = new OMX_U8[m_nInCopyBufSize];
        VTEST_MSG_HIGH("EncoderFileSource secure buf alloc %p (%u bytes)",
            m_pInCopyBuf, (unsigned int)m_nInCopyBufSize);
    }

    if (m_bSecureSession) {
        VTEST_MSG_LOW("select sec buf %p", m_pInCopyBuf);
        buffer = m_pInCopyBuf;
    } else {
        buffer = pBuffer->pBuffer;
    }

    result = m_pFile->Read(buffer, m_nFrameWidth,
            m_nFrameHeight, &nBytesRead, 0);
    if (result != OMX_ErrorNone || nFrameBytes != nBytesRead) {
        VTEST_MSG_HIGH("YUV file too small. Seeking to start.");
        result = m_pFile->SeekStart(0);
        result = m_pFile->Read(buffer, m_nFrameWidth,
                m_nFrameHeight, &nBytesRead, 0);
    }
    VTEST_MSG_LOW("EncoderFileSource: read %u bytes", (unsigned int)nBytesRead);

    if (m_bSecureSession) {
        struct pmem *pPmem = (struct pmem *)pBuffer->pInputPortPrivate;
        if (m_pCrypto != NULL && pPmem != NULL && pPmem->fd > 0) {
            // copy the entire buffer as pixel spacing will make
            // the frame larger then the number of bytes read.
            m_pCrypto->Copy(SAMPLECLIENT_COPY_NONSECURE_TO_SECURE,
                    m_pInCopyBuf, pPmem->fd, m_nInCopyBufSize);
        } else {
            VTEST_MSG_ERROR("bad secure input handle.");
        }
    }
    return OMX_ErrorNone;
}

} // namespace vtest
