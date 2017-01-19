/*-------------------------------------------------------------------
Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential
--------------------------------------------------------------------*/

#include "vtest_Crypto.h"
#include "vtest_Debug.h"
#include <dlfcn.h>

#define SymOEMCryptoLib "libcontentcopy.so"
#define SymOEMCryptoInit "Content_Protection_Copy_Init"
#define SymOEMCryptoTerminate "Content_Protection_Copy_Terminate"
#define SymOEMCryptoCopy "Content_Protection_Copy"

#undef LOG_TAG
#define LOG_TAG "VTEST_CRYPTO"

namespace vtest {

#ifdef SECURE_COPY_ENABLED

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
Crypto::Crypto()
    : m_pLibHandle(NULL),
      m_pWVHandle(NULL),
      m_fOEMCryptoInit(NULL),
      m_fOEMCryptoTerminate(NULL),
      m_fOEMCryptoCopy(NULL) {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
Crypto::~Crypto() {
    UnloadCryptoLib();
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::Init() {

    VTEST_MSG_HIGH("Crypto init");
    OMX_ERRORTYPE result = LoadCryptoLib();
    if (result == OMX_ErrorNone) {
        if (m_fOEMCryptoInit) {
            result = (OMX_ERRORTYPE)m_fOEMCryptoInit(&m_pWVHandle);
        } else {
            VTEST_MSG_ERROR("Invalid method handle to OEMCryptoInit");
            result = OMX_ErrorBadParameter;
        }
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::Terminate() {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_fOEMCryptoTerminate) {
        result = (OMX_ERRORTYPE)m_fOEMCryptoTerminate(&m_pWVHandle);
    } else {
        VTEST_MSG_ERROR("Invalid method handle to OEMCryptoTerminate");
        result = OMX_ErrorBadParameter;
    }
    UnloadCryptoLib();
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::Copy(SampleClientCopyDir eCopyDir,
        OMX_U8 *pBuffer, unsigned long nBufferFd, OMX_U32 nBufferSize) {

    SampleClientResult result = SAMPLE_CLIENT_SUCCESS;
    OMX_U32 nBytesCopied = 0;

    if (m_fOEMCryptoCopy == NULL) {
        VTEST_MSG_ERROR("Invalid method handle to OEMCryptoCopy");
        return OMX_ErrorNone;
    }

    VTEST_MSG_LOW(
        "CryptoCopy, fd: %u, buf: %p, size: %u, byte_ct: %u, copy_dir: %d",
        (unsigned int)nBufferFd, pBuffer, (unsigned int)nBufferSize, (unsigned int)nBytesCopied, eCopyDir);
    result = m_fOEMCryptoCopy(m_pWVHandle, pBuffer, nBufferSize,
            nBufferFd, 0, (unsigned long *)&nBytesCopied, eCopyDir);

    if ((result != SAMPLE_CLIENT_SUCCESS) || (nBytesCopied != nBufferSize)) {
        VTEST_MSG_ERROR(
            "Error in CryptoCopy, fd: %u, buf: %p, size: %u, byte_ct: %u, copy_dir: %d",
            (unsigned int)nBufferFd, pBuffer, (unsigned int)nBufferSize, (unsigned int)nBytesCopied, eCopyDir);
        return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::LoadCryptoLib() {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    VTEST_MSG_HIGH("Loading crypto lib");

    m_pLibHandle = dlopen(SymOEMCryptoLib, RTLD_NOW);
    if (m_pLibHandle == NULL) {
        VTEST_MSG_ERROR("Failed to open libwvm.so : %s", dlerror());
        return OMX_ErrorUndefined;
    }

    m_fOEMCryptoInit = (Content_Protection_Copy_Init)dlsym(m_pLibHandle, SymOEMCryptoInit);
    if (m_fOEMCryptoInit == NULL) {
        VTEST_MSG_ERROR("Failed to find symbol for OEMCryptoInit: %s", dlerror());
        result = OMX_ErrorUndefined;
    }
    if (result == OMX_ErrorNone) {
        m_fOEMCryptoTerminate = (Content_Protection_Copy_Terminate)dlsym(m_pLibHandle, SymOEMCryptoTerminate);
        if (m_fOEMCryptoTerminate == NULL) {
            VTEST_MSG_ERROR("Failed to find symbol for OEMCryptoTerminate: %s", dlerror());
            result = OMX_ErrorUndefined;
        }
    }
    if (result == OMX_ErrorNone) {
        m_fOEMCryptoCopy = (Content_Protection_Copy)dlsym(m_pLibHandle, SymOEMCryptoCopy);
        if (m_fOEMCryptoCopy == NULL) {
            VTEST_MSG_ERROR("Failed to find symbol for OEMCryptoCopy: %s", dlerror());
            result = OMX_ErrorUndefined;
        }
    }
    if (result != OMX_ErrorNone) {
        UnloadCryptoLib();
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
void Crypto::UnloadCryptoLib() {

    if (m_pLibHandle) {
        dlclose(m_pLibHandle);
        m_pLibHandle = NULL;
        m_pWVHandle = NULL;
    }
    m_fOEMCryptoInit = NULL;
    m_fOEMCryptoTerminate = NULL;
    m_fOEMCryptoCopy = NULL;
}

#else //SECURE_COPY_DISABLED

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
Crypto::Crypto() {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
Crypto::~Crypto() {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::Init() {

    VTEST_MSG_HIGH("Crypto init disabled");
    return OMX_ErrorNotImplemented;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::Terminate() {

    VTEST_MSG_HIGH("Crypto terminate disabled");
    return OMX_ErrorNotImplemented;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE Crypto::Copy(SampleClientCopyDir eCopyDir,
        OMX_U8 *pBuffer, unsigned long nBufferFd, OMX_U32 nBufferSize) {

    (void)eCopyDir, (void)pBuffer, (void)nBufferFd, (void)nBufferSize;
    VTEST_MSG_HIGH("Crypto copy disabled");
    return OMX_ErrorNotImplemented;
}

#endif //SECURE_COPY_ENABLED

}
