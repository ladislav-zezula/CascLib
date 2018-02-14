/*****************************************************************************/
/* DumpContext.h                          Copyright (c) Ladislav Zezula 2015 */
/*---------------------------------------------------------------------------*/
/* Interface for TDumpContext                                                */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 16.03.15  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef DUMP_CONTEXT_H_
#define DUMP_CONTEXT_H_

//-----------------------------------------------------------------------------
// Defines

// Size of the buffer for the dump context
#define CASC_DUMP_BUFFER_SIZE   0x10000

// Structure for dump context
struct TDumpContext
{
    TFileStream * pStream;                      // Pointer to the open stream
    LPBYTE pbBufferBegin;                       // Begin of the dump buffer
    LPBYTE pbBufferPtr;                         // Current dump buffer pointer
    LPBYTE pbBufferEnd;                         // End of the dump buffer

    BYTE DumpBuffer[CASC_DUMP_BUFFER_SIZE];     // Dump buffer
};

//-----------------------------------------------------------------------------
// Dump context functions

TDumpContext * CreateDumpContext(struct _TCascStorage * hs, const TCHAR * szNameFormat);
int dump_print(TDumpContext * dc, const char * szFormat, ...);
int dump_close(TDumpContext * dc);

#endif  // DUMP_CONTEXT_H_
