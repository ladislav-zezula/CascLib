/*****************************************************************************/
/* Sockets.h                              Copyright (c) Ladislav Zezula 2021 */
/*---------------------------------------------------------------------------*/
/* Socket functions for CascLib                                              */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 21.01.21  1.00  Lad  Created                                              */
/*****************************************************************************/

#ifndef __SOCKETS_H__
#define __SOCKETS_H__

//-----------------------------------------------------------------------------
// Socket helper functions

// Initialize sockets library
DWORD sockets_initialize();
LPBYTE sockets_read_response(CASC_SOCKET sock, const char * request, size_t requestLength, size_t * PtrLength);
void  sockets_free();

#endif // __SOCKETS_H__
