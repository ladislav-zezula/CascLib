/*****************************************************************************/
/* HashToPtr.h                            Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Interface of hash-to-ptr map for CascLib                                  */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 10.06.14  1.00  Lad  The first version of HashToPtr.h                     */
/*****************************************************************************/

#ifndef __HASHTOPTR_H__
#define __HASHTOPTR_H__

//-----------------------------------------------------------------------------
// Structures

typedef struct _MAP_HASH_TO_PTR
{
    size_t ObjectCount;                         // Number of objects in the map
    size_t KeyLength;                           // Length of the hash key
    size_t HashMask;                            // Mask from hash to index
    size_t Reserved;
    void * HashTable[1];                        // Pointer table. Contains either direct pointers or subtables

} MAP_HASH_TO_PTR, *PMAP_HASH_TO_PTR;

//-----------------------------------------------------------------------------
// Functions

typedef bool (*PFN_ENUM_MAP_PROC)(PMAP_HASH_TO_PTR pMap, void * pvContext, LPBYTE pbHash);

PMAP_HASH_TO_PTR MapHashToPtr_Create(DWORD dwMaxItems, DWORD dwKeyLength);
void * MapHashToPtr_FindObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash);
void MapHashToPtr_EnumObjects(PMAP_HASH_TO_PTR pMap, PFN_ENUM_MAP_PROC pfnEnumProc, void * pvContext); 
bool MapHashToPtr_InsertObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash);
void MapHashToPtr_Free(PMAP_HASH_TO_PTR pMap);

#endif // __HASHTOPTR_H__
