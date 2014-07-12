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

typedef struct _MAP_SUBTABLE
{
    size_t ItemCount;                           // Number of items in the table
    size_t ItemCountMax;                        // Total number of items
} MAP_SUBTABLE, *PMAP_SUBTABLE;

typedef struct _MAP_HASH_TO_PTR
{
    size_t KeyLength;                           // Length of the hash key
    size_t HashShift;                           // Bit shift of the hash to get the index
    size_t TableSize;
    size_t ItemCount;                           // Number of items in the map
    size_t MemberOffset;                        // Bit shift of the hash to get the index
    void * HashTable[1];                        // Pointer table. Contains either direct pointers or subtables

} MAP_HASH_TO_PTR, *PMAP_HASH_TO_PTR;

//-----------------------------------------------------------------------------
// Functions

PMAP_HASH_TO_PTR MapHashToPtr_Create(DWORD dwMaxItems, DWORD dwKeyLength, DWORD dwMemberOffset);
size_t MapHashToPtr_EnumObjects(PMAP_HASH_TO_PTR pMap, void **ppvArray); 
void * MapHashToPtr_FindObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash);
bool MapHashToPtr_InsertObject(PMAP_HASH_TO_PTR pMap, LPBYTE pbHash);
void MapHashToPtr_Sort(PMAP_HASH_TO_PTR pMap);
void MapHashToPtr_Free(PMAP_HASH_TO_PTR pMap);

#endif // __HASHTOPTR_H__
