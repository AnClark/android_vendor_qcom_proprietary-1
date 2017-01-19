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

#include "vtest_ITestCase.h"
#include "vtest_Thread.h"
#include "vtest_Debug.h"
#include "vtest_Config.h"
#include "vtest_Parser.h"

namespace vtest {

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
ITestCase::ITestCase()
    : m_pThread(new Thread()),
      m_eTestResult(OMX_ErrorNone),
      m_nTestNum(0) {}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
ITestCase::~ITestCase() {

    if (m_pThread) {
        delete m_pThread;
    }
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ITestCase::Start(
        OMX_STRING pConfigFileName, OMX_S32 nTestNum) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    Config config;
    config.GetCodecConfig(&m_sConfig);
    config.GetDynamicConfig(&m_sDynamicConfig);

    m_nTestNum = nTestNum;
    result = config.Parse(pConfigFileName, &m_sConfig, &m_sDynamicConfig);
    if (result == OMX_ErrorNone) {
        result = ValidateAssumptions(&m_sConfig, &m_sDynamicConfig);

        if (result == OMX_ErrorNone) {
            VTEST_MSG_MEDIUM("Starting test thread...");
            if (m_pThread) {
                result = m_pThread->Start(ThreadEntry, this, 0);
            } else {
                VTEST_MSG_ERROR("Start test thread failed...");
                result = OMX_ErrorUndefined;
            }
        } else {
            VTEST_MSG_ERROR("Invalid config. Assumptions not validated");
        }
    } else {
        VTEST_MSG_ERROR("Error parsing config file");
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ITestCase::Finish() {

    OMX_ERRORTYPE result = OMX_ErrorUndefined;

    if (m_pThread) {
        OMX_ERRORTYPE threadResult;

        VTEST_MSG_MEDIUM("waiting for testcase thread to finish...");

        // wait for thread to exit
        result = m_pThread->Join(&threadResult);

        if (result == OMX_ErrorNone) {
            result = threadResult;
        }

        if (threadResult != OMX_ErrorNone) {
            VTEST_MSG_ERROR("test case thread execution error");
        }
    }

    // let's not over-ride the original error with a different result
    if (m_eTestResult == OMX_ErrorNone) {
        m_eTestResult = result;
    }
    return m_eTestResult;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ITestCase::CheckError(OMX_ERRORTYPE eTestResult) {

    // The first error we encounter (if one actually occurs) is the final test result.
    // Do not over-ride the original error!
    if (m_eTestResult == OMX_ErrorNone) {
        m_eTestResult = eTestResult;
    }

    // Simply return the result that was passed in
    return eTestResult;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ITestCase::ThreadEntry(OMX_PTR pThreadData) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    ITestCase *pTest = (ITestCase *)pThreadData;
    result = pTest->Execute(&pTest->m_sConfig, &pTest->m_sDynamicConfig,
                            pTest->m_nTestNum);

    if (pTest->m_eTestResult == OMX_ErrorNone) {
        result = pTest->m_eTestResult;
    }

    return pTest->m_eTestResult;
}

} // namespace vtest
