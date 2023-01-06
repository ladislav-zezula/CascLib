/*****************************************************************************/
/* TLogHelper.cpp                         Copyright (c) Ladislav Zezula 2013 */
/*---------------------------------------------------------------------------*/
/* Helper class for reporting StormLib tests                                 */
/* This file should be included directly from StormTest.cpp using #include   */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 26.11.13  1.00  Lad  The first version of TLogHelper.cpp                  */
/*****************************************************************************/

//-----------------------------------------------------------------------------
// String replacements for format strings

#ifdef _MSC_VER
#define fmt_I64u "%I64u"
#define fmt_I64X "%I64X"
#else
#define fmt_I64u "%llu"
#define fmt_I64X "%llX"
#endif

//-----------------------------------------------------------------------------
// Definition of the TLogHelper class

class TLogHelper
{
    public:

    //
    //  Constructor and destructor
    //

    TLogHelper(const char * szNewMainTitle = NULL, const char * /* szNewSubTitle */ = NULL)
    {
        // Fill the variables
        memset(this, 0, sizeof(TLogHelper));

#ifdef CASCLIB_PLATFORM_WINDOWS
        InitializeCriticalSection(&Locker);
        TickCount = GetTickCount();
#endif

        // Remember the startup time
        SetStartTime();
        TotalFiles = 1;

        // Print the initial information
        if(szNewMainTitle != NULL)
        {
            char szTitle[101];
            size_t nLength;

            nLength = CascStrPrintf(szTitle, _countof(szTitle), "-- \"%s\" --", szNewMainTitle);
            while(nLength < 90)
                szTitle[nLength++] = '-';
            if(nLength < sizeof(szTitle))
                szTitle[nLength++] = 0;

            printf("%s\n", szTitle);
        }
    }

    ~TLogHelper()
    {
        const char * szSaveMainTitle = szMainTitle;
        const char * szSaveSubTitle = szSubTitle;

        // Set both to NULL so the won't be printed
        szMainTitle = NULL;
        szSubTitle = NULL;

        // Print the final information
        if(szSaveMainTitle != NULL && bMessagePrinted == false)
        {
            if(DontPrintResult == false)
            {
                if(szSaveSubTitle != NULL)
                    PrintMessage("%s (%s) succeeded.", szSaveMainTitle, szSaveSubTitle);
                else
                    PrintMessage("%s succeeded.", szSaveMainTitle);
            }
            else
            {
                PrintProgress(" ");
                printf("\r");
            }
        }

#ifdef CASCLIB_PLATFORM_WINDOWS
        DeleteCriticalSection(&Locker);
#endif

        printf("\n");
    }

    //
    // Measurement of elapsed time
    //

    bool TimeElapsed(DWORD Milliseconds)
    {
        bool bTimeElapsed = false;

#ifdef CASCLIB_PLATFORM_WINDOWS
        if(GetTickCount() > (TickCount + Milliseconds))
        {
            TickCount = GetTickCount();
            if(CascInterlockedIncrement(&TimeTrigger) == 1)
            {
                bTimeElapsed = true;
            }
        }

#endif
        return bTimeElapsed;
    }

    //
    //  Printing functions
    //

    template <typename XCHAR>
    DWORD PrintWithClreol(const XCHAR * szFormat, va_list argList, bool bPrintPrefix, bool bPrintLastError, bool bPrintEndOfLine)
    {
        char * szBufferPtr;
        char * szBufferEnd;
        size_t nNewPrinted;
        size_t nLength = 0;
        DWORD dwErrCode = GetCascError();
        XCHAR szMessage[0x200];
        char szBuffer[0x200];

        // Always start the buffer with '\r'
        szBufferEnd = szBuffer + _countof(szBuffer);
        szBufferPtr = szBuffer;
        *szBufferPtr++ = '\r';

        // Print the prefix, if needed
        if(szMainTitle != NULL && bPrintPrefix)
        {
            while(szMainTitle[nLength] != 0)
                *szBufferPtr++ = szMainTitle[nLength++];
            
            *szBufferPtr++ = ':';
            *szBufferPtr++ = ' ';
        }

        // Construct the message
        nLength = CascStrPrintfV(szMessage, _countof(szMessage), szFormat, argList);
        CascStrCopy(szBufferPtr, (szBufferEnd - szBufferPtr), szMessage);
        szBufferPtr += nLength;

        // Append the last error
        if(bPrintLastError)
        {
            nLength = CascStrPrintf(szBufferPtr, (szBufferEnd - szBufferPtr), " (error code: %u)", dwErrCode);
            szBufferPtr += nLength;
        }

        // Remember how much did we print
        nNewPrinted = (szBufferPtr - szBuffer);

        // Shall we pad the string?
        if((nLength = (szBufferPtr - szMessage)) < nPrevPrinted)
        {
            size_t nPadding = nPrevPrinted - nLength;

            if((size_t)(nLength + nPadding) > (size_t)(szBufferEnd - szBufferPtr))
                nPadding = (szBufferEnd - szBufferPtr);
            
            memset(szBufferPtr, ' ', nPadding);
            szBufferPtr += nPadding;
        }

        // Shall we add new line?
        if((bPrintEndOfLine != false) && (szBufferPtr < szBufferEnd))
        {
            *szBufferPtr++ = '\n';
            *szBufferPtr = 0;
        }

        // Remember if we printed a message
        if(bPrintEndOfLine != false)
        {
            bMessagePrinted = true;
            nPrevPrinted = 0;
        }
        else
        {
            nPrevPrinted = nNewPrinted;
        }

        // Finally print the message
        printf("%s", szBuffer);
        nMessageCounter++;
        return dwErrCode;
    }

    template <typename XCHAR>
    void PrintProgress(const XCHAR * szFormat, ...)
    {
        va_list argList;

        // Always reset the time trigger
        TimeTrigger = 0;

        // Only print progress when the cooldown is ready
        if(ProgressReady())
        {
            va_start(argList, szFormat);
            PrintWithClreol(szFormat, argList, true, false, false);
            va_end(argList);
        }
    }

    template <typename XCHAR>
    void PrintMessage(const XCHAR * szFormat, ...)
    {
        va_list argList;

        va_start(argList, szFormat);
        PrintWithClreol(szFormat, argList, true, false, true);
        va_end(argList);
    }

    void PrintTotalTime()
    {
        DWORD TotalTime = SetEndTime();

        if(TotalTime != 0)
            PrintMessage("TotalTime: %u.%u second(s)", (TotalTime / 1000), (TotalTime % 1000));
        PrintMessage("Work complete.");
    }

    template <typename XCHAR>
    int PrintErrorVa(const XCHAR * szFormat, ...)
    {
        va_list argList;
        int nResult;

        va_start(argList, szFormat);
        nResult = PrintWithClreol(szFormat, argList, true, true, true);
        va_end(argList);

        return nResult;
    }

    template <typename XCHAR>
    int PrintError(const XCHAR * szFormat, const XCHAR * szFileName)
    {
        return PrintErrorVa(szFormat, szFileName);
    }

    //
    // Locking functions (Windows only)
    //

    void Lock()
    {
#ifdef CASCLIB_PLATFORM_WINDOWS
        EnterCriticalSection(&Locker);
#endif
    }

    void Unlock()
    {
#ifdef CASCLIB_PLATFORM_WINDOWS
        LeaveCriticalSection(&Locker);
#endif
    }

    void IncrementTotalBytes(ULONGLONG IncrementValue)
    {
        // For some weird reason, this is measurably faster then InterlockedAdd64
        Lock();
        TotalBytes = TotalBytes + IncrementValue;
        Unlock();
    }

    //
    //  Time functions
    //

    ULONGLONG GetCurrentThreadTime()
    {
#ifdef _WIN32
        ULONGLONG TempTime = 0;

        GetSystemTimeAsFileTime((LPFILETIME)(&TempTime));
        return ((TempTime) / 10 / 1000);

        //ULONGLONG KernelTime = 0;
        //ULONGLONG UserTime = 0;
        //ULONGLONG TempTime = 0;

        //GetThreadTimes(GetCurrentThread(), (LPFILETIME)&TempTime, (LPFILETIME)&TempTime, (LPFILETIME)&KernelTime, (LPFILETIME)&UserTime);
        //return ((KernelTime + UserTime) / 10 / 1000);
#else
        return time(NULL) * 1000;
#endif
    }

    bool ProgressReady()
    {
        time_t dwTickCount = time(NULL);
        bool bResult = false;

        if(dwTickCount > dwPrevTickCount)
        {
            dwPrevTickCount = dwTickCount;
            bResult = true;
        }

        return bResult;
    }

    ULONGLONG SetStartTime()
    {
        StartTime = GetCurrentThreadTime();
        return StartTime;
    }

    DWORD SetEndTime()
    {
        EndTime = GetCurrentThreadTime();
        return (DWORD)(EndTime - StartTime);
    }

    void FormatTotalBytes(char * szBuffer, size_t ccBuffer)
    {
        ULONGLONG Bytes = TotalBytes;
        ULONGLONG Divider = 1000000000;
        char * szBufferEnd = szBuffer + ccBuffer;
        bool bDividingOn = false;

        while((szBuffer + 4) < szBufferEnd && Divider > 0)
        {
            // Are we already dividing?
            if(bDividingOn)
            {
                szBuffer += CascStrPrintf(szBuffer, ccBuffer, " %03u", (DWORD)(Bytes / Divider));
                Bytes = Bytes % Divider;
            }
            else if(Bytes > Divider)
            {
                szBuffer += CascStrPrintf(szBuffer, ccBuffer, "%u", (DWORD)(Bytes / Divider));
                Bytes = Bytes % Divider;
                bDividingOn = true;
            }
            Divider /= 1000;
        }
    }

#ifdef CASCLIB_PLATFORM_WINDOWS
    CRITICAL_SECTION Locker;
#endif

    ULONGLONG TotalBytes;                           // For user's convenience: Total number of bytes
    ULONGLONG ByteCount;                            // For user's convenience: Current number of bytes
    ULONGLONG StartTime;                            // Start time of an operation, in milliseconds
    ULONGLONG EndTime;                              // End time of an operation, in milliseconds
    DWORD TimeTrigger;                              // For triggering elapsed timers
    DWORD TotalFiles;                               // For user's convenience: Total number of files
    DWORD FileCount;                                // For user's convenience: Curernt number of files
    DWORD TickCount;
    DWORD DontPrintResult:1;                        // If true, supresset pringing result from the destructor

    protected:

    template <typename XCHAR>
    bool IsFormatSpecifier(const XCHAR * szFormat, const char * szSpecifier)
    {
        for(size_t i = 0; szSpecifier[i] != 0; i++)
        {
            if(szFormat[i] != szSpecifier[i])
                return false;
        }
        return true;
    }

    template <typename XCHAR>
    char * StringCopy(char * szBuffer, const XCHAR * szSource)
    {
        while(szSource[0] != 0)
            *szBuffer++ = (char)(*szSource++);
        return szBuffer;
    }

    const char * szMainTitle;                       // Title of the text (usually name)
    const char * szSubTitle;                        // Title of the text (can be name of the tested file)
    size_t nMessageCounter;
    size_t nPrevPrinted;                            // Length of the previously printed message
    time_t dwPrevTickCount;
    bool bMessagePrinted;
};
