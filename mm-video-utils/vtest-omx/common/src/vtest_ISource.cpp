/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
--------------------------------------------------------------------*/

#include "vtest_Debug.h"
#include "vtest_BufferManager.h"
#include "vtest_ISource.h"

#undef LOG_TAG
#define LOG_TAG "VTEST_ISOURCE"

namespace vtest {

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
ISource::ISource()
    : m_nInBufferCount(0),
      m_nInBytes(0),
      m_pThread(new Thread()),
      m_bThreadStop(OMX_FALSE),
      m_pSink(NULL),
      m_pSource(NULL),
      m_pBufferManager(NULL),
      m_pLock(new Mutex()),
      m_pBufferQueue(new SignalQueue(100, sizeof(OMX_BUFFERHEADERTYPE*))) {

    memset(m_pName, 0, sizeof(m_pName));
    sprintf(m_pName, "NAME NOT SET");
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
ISource::~ISource() {

    if (m_pThread != NULL) {
        delete m_pThread;
        m_pThread = NULL;
    }
    if (m_pLock != NULL) {
        delete m_pLock;
        m_pLock = NULL;
    }
    if (m_pBufferQueue != NULL) {
        delete m_pBufferQueue;
        m_pBufferQueue = NULL;
    }
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::Start() {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    {
        Mutex::Autolock autoLock(m_pLock);

        // duplicate starts ok, as any source can start
        if (m_pThread->Started()) {
            return result;
        }

        VTEST_MSG_MEDIUM("%s, thread start...", Name());
        result = m_pThread->Start(
            this,   // threaded object
            this,   // thread data
            0);     // thread priority
        FAILED1(result, "Failed to start thread");
    }

    if (m_pSource != NULL) {
        result = m_pSource->Start();
        FAILED1(result, "Error: cannot start source %s!", m_pSource->Name());
    }

    if (m_pSink != NULL) {
        result = m_pSink->Start();
        FAILED1(result, "Error: cannot start sink %s!", m_pSource->Name());
    }

    // Queue buffers to start chain
    // by convention, have each block enqueue to its output port to avoid duplicate enqueuing
    if (m_pSink != NULL) {
        OMX_U32 nBufferCount;
        BufferInfo *pBuffers = NULL;
        result = m_pBufferManager->GetBuffers(
            this, PORT_INDEX_OUT, &pBuffers, &nBufferCount);
        FAILED1(result, "%s: Error setting port buffers", Name());

        for (OMX_U32 i = 0; i < nBufferCount; i++) {
            VTEST_MSG_HIGH("%s: queuing buffer %p", Name(), &pBuffers[i]);
            SetBuffer(&pBuffers[i], m_pSink);
            FAILED1(result, "%s: Error setting port buffer %p", Name(), &pBuffers[i]);
        }
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::Finish() {

    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_ERRORTYPE threadResult;

    if (!m_pThread->Started()) {
        VTEST_MSG_HIGH("%s: thread already done", Name());
        return OMX_ErrorNone;
    }

    VTEST_MSG_MEDIUM("%s: thread join...", Name());
    result = m_pThread->Join(&threadResult);
    if (threadResult != OMX_ErrorNone) {
        VTEST_MSG_ERROR("Error: %s thread execution error", Name());
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::Stop() {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    VTEST_MSG_HIGH("%s: Stop", Name());
    {
        Mutex::Autolock autoLock(m_pLock);
        m_bThreadStop = OMX_TRUE;
        m_pBufferQueue->WakeAll();
    }

    VTEST_MSG_MEDIUM("%s: finishing...", Name());
    result = Finish();
    FAILED(result, "cannot finish source thread");

    if (m_pSource != NULL) {
        result = m_pSource->Stop();
        FAILED(result, "Cannot stop source!");
    }

    if (m_pSink != NULL) {
        result = m_pSink->Stop();
        FAILED(result, "Cannot stop sink!");
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::Configure(CodecConfigType *pConfig,
        BufferManager *pBufManager, ISource *pSource, ISource *pSink) {

    (void) pConfig;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    Mutex::Autolock autoLock(m_pLock);

    if (pBufManager == NULL ||
        (pSource == NULL && pSink == NULL)) {
        VTEST_MSG_ERROR("Error: BufferManager or source/sink not set");
        return OMX_ErrorBadParameter;
    }

    m_pBufferManager = pBufManager;
    m_pSource = pSource;
    m_pSink = pSink;
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::SetBuffer(BufferInfo *pBuffer, ISource *pSource) {

    Mutex::Autolock autoLock(m_pLock);

    if (m_bThreadStop) {
        return OMX_ErrorNoMore;
    }

    if (pBuffer != NULL) {
        //non-shared headers have some fields copied...
        if (pBuffer->pHeaderIn != pBuffer->pHeaderOut) {
            if (pSource == m_pSource) {
                pBuffer->pHeaderIn->nFilledLen = pBuffer->pHeaderOut->nFilledLen;
                pBuffer->pHeaderIn->nOffset = pBuffer->pHeaderOut->nOffset;
                pBuffer->pHeaderIn->nTimeStamp = pBuffer->pHeaderOut->nTimeStamp;
                pBuffer->pHeaderIn->nFlags = pBuffer->pHeaderOut->nFlags;
            } else {
                pBuffer->pHeaderOut->nFilledLen = pBuffer->pHeaderIn->nFilledLen;
                pBuffer->pHeaderOut->nOffset = pBuffer->pHeaderIn->nOffset;
                pBuffer->pHeaderOut->nTimeStamp = pBuffer->pHeaderIn->nTimeStamp;
                pBuffer->pHeaderOut->nFlags = pBuffer->pHeaderIn->nFlags;
            }
        }

        // track input bytes
        if(pSource == m_pSource || m_pSource == NULL) {
            m_nInBufferCount++;
            m_nInBytes += pBuffer->pHeaderIn->nFilledLen;
            VTEST_MSG_LOW("%s received: buf_ct %u byte_ct %u",
                          Name(), (unsigned int)m_nInBufferCount, (unsigned int)m_nInBytes);
        }
    }
    return OMX_ErrorNone;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::AllocateBuffers(BufferInfo **pBuffers,
        OMX_U32 nWidth, OMX_U32 nHeight, OMX_U32 nBufferCount,
        OMX_U32 nBufferSize, OMX_U32 ePortIndex, OMX_U32 nBufferUsage) {

    (void)pBuffers, (void)nWidth, (void)nHeight, (void)nBufferCount;
    (void)nBufferSize, (void)ePortIndex, (void)nBufferUsage;
    Mutex::Autolock autoLock(m_pLock);
    VTEST_MSG_ERROR("%s: Error, unimplemented allocate", Name());
    return OMX_ErrorUndefined;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::UseBuffers(BufferInfo **pBuffers,
        OMX_U32 nWidth, OMX_U32 nHeight, OMX_U32 nBufferCount,
        OMX_U32 nBufferSize, OMX_U32 ePortIndex) {

    (void)nWidth, (void)nHeight, (void)nBufferSize;
    Mutex::Autolock autoLock(m_pLock);
    VTEST_MSG_LOW("%s: sharing buffer-headers with %s on %s port",
                  Name(),
                  (ePortIndex == PORT_INDEX_IN ? m_pSource->Name() : m_pSink->Name()),
                  OMX_PORT_NAME(ePortIndex));

    // By default use the allocating nodes buffer-headers
    for (OMX_U32 i = 0; i < nBufferCount; i++) {
        BufferInfo *pBuffer = &((*pBuffers)[i]);
        if (ePortIndex == PORT_INDEX_OUT) {
            pBuffer->pHeaderOut = pBuffer->pHeaderIn;
        } else {
            pBuffer->pHeaderIn = pBuffer->pHeaderOut;
        }
    }
    return OMX_ErrorNone;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::FreeAllocatedBuffers(
        BufferInfo **pBuffers, OMX_U32 nBufferCount, OMX_U32 ePortIndex) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pBuffers == NULL) {
        // might get dumplicate frees in pass-throught mode (NativeWindow)
        return result;
    }

    VTEST_MSG_MEDIUM("%s, freeing allocated buffers on %s port",
                     Name(), OMX_PORT_NAME(ePortIndex));
    for (OMX_U32 i = 0; i < nBufferCount; i++) {
        BufferInfo *pBuffer = &((*pBuffers)[i]);
        VTEST_MSG_LOW("%s, free-alloc 0x%lx (%p %p)",Name(),
                         pBuffer->pHandle, pBuffer->pHeaderIn, pBuffer->pHeaderOut);
        result = FreeBuffer(pBuffer, ePortIndex);
        pBuffer->pHandle = 0;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::FreeUsedBuffers(
        BufferInfo **pBuffers, OMX_U32 nBufferCount, OMX_U32 ePortIndex) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    VTEST_MSG_MEDIUM("%s, freeing used buffer on %s port",
                     Name(), OMX_PORT_NAME(ePortIndex));
    for (OMX_U32 i = 0; i < nBufferCount; i++) {
        BufferInfo *pBuffer = &((*pBuffers)[i]);
        VTEST_MSG_LOW("%s, free-use 0x%lx (%p %p)",Name(),
                      pBuffer->pHandle, pBuffer->pHeaderIn, pBuffer->pHeaderOut);
        result = FreeBuffer(pBuffer, ePortIndex);
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::PortReconfig(OMX_U32 ePortIndex,
        OMX_U32 nWidth, OMX_U32 nHeight, const OMX_CONFIG_RECTTYPE& sRect) {

    (void)nWidth, (void)nHeight, (void)sRect;
    VTEST_MSG_MEDIUM("%s: PortReconfig %s",
            Name(), OMX_PORT_NAME(ePortIndex));
    return OMX_ErrorNone;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

OMX_STRING ISource::Name() {
    return m_pName;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_S32 ISource::OutputBufferCount() {
    return (m_pSink != NULL ? m_pSink->m_nInBufferCount : m_nInBufferCount);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_S32 ISource::OutputByteCount() {
    return (m_pSink != NULL ? m_pSink->m_nInBytes : m_nInBytes);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
ISource* ISource::Source() {
    return m_pSource;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
ISource* ISource::Sink() {
    return m_pSink;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::ThreadRun(OMX_PTR pThreadData) {
    (void)pThreadData;
    return OMX_ErrorUndefined;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ISource::FreeBuffer(
        BufferInfo *pBuffer, OMX_U32 ePortIndex) {

    (void)pBuffer, (void)ePortIndex;
    VTEST_MSG_MEDIUM("%s: unimplemented FreeBuffer", Name());
    return OMX_ErrorNone;
}

} // namespace vtest
