/*
 * Copyright (c) 2025 Red Hat, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#define WORKING_DIR_A     "c:\\hcktrace"
#define WORKING_DIR       TEXT(WORKING_DIR_A)
#define LOGS_PATH         WORKING_DIR TEXT("\\log\\")
#define JOB_FILE          WORKING_DIR TEXT("\\hcktrace.job.ini")
#define ETL_PATH          WORKING_DIR TEXT("\\etl\\")
#define ZIP_PATH          WORKING_DIR TEXT("\\zip\\")
#define ETL_FILENAME      TEXT("log.etl")
#define ETL_FILE          ETL_PATH ETL_FILENAME
#define LOGGER_NAME       "VirtioHckTrace"
#define LOGGER_LNAME      TEXT(LOGGER_NAME)
#define __MAX_LOG_SIZE_KB (1024 * 1024 * 2 - 128)
// #define __MAX_LOG_SIZE_KB (2048)
#define MAX_LOG_SIZE      ((ULONG)(__MAX_LOG_SIZE_KB * 1024))

class CAutoTraceProvider
{
  public:
    CString ProviderGuid;
    CString m_Description;
    ULONG m_Enabled = 0;
    ULONG m_Level = 0;
};

class CAutoTrace
{
  public:
    // start the log recording
    void ConfigureRecording(LPCSTR Device);
    void StopLogging();
    void Process();
    // save the final portion of logs for the last test
    void StopTest();
    // just ensure that the recording runs;
    void StartTest();
    void RetrieveState();
    void SetDebugLevel(ULONG level);
    void ProcessEtlFile(ULONG i, bool Move);
    ULONG m_Active = 0;
    ULONG m_FileCounter = 0;
    ULONG m_FileMax = 16;
    ULONG m_Status = (ULONG)-1;
    ULONG m_DebugLevel = 4;
    CAtlArray<CAutoTraceProvider> m_Providers;
    CMutex m_Mutex;

  private:
    void RestartLogging();
};
