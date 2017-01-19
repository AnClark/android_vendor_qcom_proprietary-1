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

#include <stdlib.h>
#include "vtest_Script.h"
#include "vtest_Debug.h"
#include "vtest_ComDef.h"
#include "vtest_ITestCase.h"
#include "vtest_TestCaseFactory.h"


void RunScript(OMX_STRING pScriptFile) {

    OMX_S32 nPass = 0;
    OMX_S32 nFail = 0;
    OMX_S32 nTestCase = 0;
    OMX_ERRORTYPE result = OMX_ErrorNone;


    vtest::Script script;
    script.Configure(pScriptFile);

    if (result == OMX_ErrorNone) {
        vtest::TestDescriptionType testDescription;
        do {
            result = script.NextTest(&testDescription);

            if (result == OMX_ErrorNone) {
                for (OMX_S32 i = 0; i < testDescription.nSession; i++) {
                    vtest::ITestCase *pTest =
                        vtest::TestCaseFactory::AllocTest((OMX_STRING)testDescription.cTestName);

                    if (pTest != NULL) {
                        VTEST_MSG_CONSOLE("Running test %u", (unsigned int)(nPass + nFail));
                        result = pTest->Start(testDescription.cConfigFile, i);

                        if (result == OMX_ErrorNone) {
                            result = pTest->Finish();
                            if (result == OMX_ErrorNone) {
                                ++nPass;
                            } else {
                                VTEST_MSG_ERROR("test %u failed", (unsigned int)(nPass + nFail));
                                ++nFail;
                            }
                        } else {
                            VTEST_MSG_ERROR("error starting test");
                            ++nFail;
                        }
                        vtest::TestCaseFactory::DestroyTest(pTest);
                    } else {
                        VTEST_MSG_ERROR("unable to alloc test");
                    }

                }
            } else if (result != OMX_ErrorNoMore) {
                VTEST_MSG_ERROR("error parsing script");
            }

        }
        while (result != OMX_ErrorNoMore);
    }

    VTEST_MSG_CONSOLE("passed %d out of %d tests", (int)nPass, (int)(nPass + nFail));
    VTEST_MSG_CONSOLE("/*******************TestApp Exiting*******************/");
}

void RunTest(OMX_STRING pTestName, OMX_STRING pConfigFile, OMX_S32 nSession) {

    OMX_S32 nPass = 0;
    OMX_S32 nFail = 0;
    OMX_ERRORTYPE result = OMX_ErrorNone;

    for (OMX_S32 i = 0; i < nSession; i++) {
        vtest::ITestCase *pTest =
            vtest::TestCaseFactory::AllocTest((OMX_STRING)pTestName);

        if (pTest != NULL) {
            VTEST_MSG_CONSOLE("Running test %u", (unsigned int)(nPass + nFail));
            result = pTest->Start(pConfigFile, i);

            if (result == OMX_ErrorNone) {
                result = pTest->Finish();
                if (result == OMX_ErrorNone) {
                    ++nPass;
                } else {
                    ++nFail;
                }
            } else {
                VTEST_MSG_ERROR("error starting test");
            }
            vtest::TestCaseFactory::DestroyTest(pTest);
        } else {
            VTEST_MSG_ERROR("unable to alloc test");
        }
    }

    VTEST_MSG_CONSOLE("passed %d out of %d tests", (int)nPass, (int)(nPass + nFail));
    VTEST_MSG_CONSOLE("/*******************TestApp Exiting*******************/");
}

int main(int argc, char *argv[]) {

    OMX_Init();

    if (argc == 2) {
        OMX_STRING pScriptFile = (OMX_STRING)argv[1];

        RunScript(pScriptFile);
    } else if (argc == 4) {
        OMX_STRING pTestName = (OMX_STRING)argv[1];
        OMX_STRING pConfigFile = (OMX_STRING)argv[2];
        OMX_STRING pNumSession = (OMX_STRING)argv[3];
        OMX_S32 nSession;

        nSession = atoi((char *)pNumSession);

        RunTest(pTestName, pConfigFile, nSession);
    } else {
        VTEST_MSG_CONSOLE("invalid number of command args %d", argc);
        VTEST_MSG_CONSOLE("./mm-vidc-omx-test ENCODE/DECODE/TRANSCODE Config.cfg 1");
    }
    OMX_Deinit();
}
