/*****************************************************************************/
/* Map.h                                  Copyright (c) Ladislav Zezula 2014 */
/*---------------------------------------------------------------------------*/
/* Interface of hash-to-ptr map for CascLib                                  */
/*---------------------------------------------------------------------------*/
/*   Date    Ver   Who  Comment                                              */
/* --------  ----  ---  -------                                              */
/* 10.06.14  1.00  Lad  The first version of Map.h                           */
/*****************************************************************************/

#ifndef __CASC_MAP_H__
#define __CASC_MAP_H__

//-----------------------------------------------------------------------------
// Structures

#define KEY_LENGTH_STRING       0xFFFFFFFF      // Pass this to Map_Create as dwKeyLength when you want map of string->object

#define MAX_HASH_TABLE_SIZE     0x00800000      // The largest size of the hash table. Should be enough for any game.


class CASC_MAP
{
    public:

    int Create(size_t MaxItems, size_t KeyLength, size_t KeyOffset, bool bKeyIsHash = false)
    {
        // Set the class variables
        m_KeyLength = CASCLIB_MIN(KeyLength, 8);
        m_KeyOffset = KeyOffset;
        m_ItemCount = 0;
        m_bKeyIsHash = bKeyIsHash;

        // Calculate the hash table size. Take 133% of the item count and round it up to the next power of two
        // This will make the hash table indexes somewhat more resilient against count changes and will make
        // e.g. file order in the file tree more stable.
        m_HashTableSize = GetNearestPowerOfTwo(MaxItems * 4 / 3);
        m_HashTableSize = CASCLIB_MAX(m_HashTableSize, 0x100);
        if(m_HashTableSize == 0)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Allocate new map for the objects
        m_HashTable = (void **)CASC_ALLOC(void *, m_HashTableSize);
        if(m_HashTable == NULL)
            return ERROR_NOT_ENOUGH_MEMORY;

        // Initialize the map object
        memset(m_HashTable, 0, m_HashTableSize * sizeof(void *));
        return ERROR_SUCCESS;
    }

    size_t EnumObjects(void **ppvArray)
    {
        size_t nIndex = 0;

        // Verify pointer to the map
        if(m_HashTable != NULL && ppvArray != NULL)
        {
            // Enumerate all items in main table
            for(size_t i = 0; i < m_HashTableSize; i++)
            {
                // Is that cell valid?
                if(m_HashTable[i] != NULL)
                {
                    ppvArray[nIndex++] = m_HashTable[i];
                }
            }
        }

        return nIndex;
    }

    void * FindObject(void * pvKey, PDWORD PtrIndex = NULL)
    {
        void * pvObject;
        DWORD dwHashIndex;

        // Verify pointer to the map
        if(m_HashTable != NULL)
        {
            // Construct the hash index
            dwHashIndex = CalcHashIndex_Key(pvKey);

            // Search the hash table
            while((pvObject = m_HashTable[dwHashIndex]) != NULL)
            {
                // Compare the hash
                if(CompareObject_Key(pvObject, pvKey))
                {
                    if(PtrIndex != NULL)
                        PtrIndex[0] = dwHashIndex;
                    return pvObject;
                }

                // Move to the next entry
                dwHashIndex = (dwHashIndex + 1) & (m_HashTableSize - 1);
            }
        }

        // Not found, sorry
        return NULL;
    }

    bool InsertObject(void * pvNewObject, void * pvKey)
    {
        void * pvExistingObject;
        DWORD dwHashIndex;

        // Verify pointer to the map
        if(m_HashTable != NULL)
        {
            // Limit check
            if(m_ItemCount >= m_HashTableSize)
                return false;

            // Construct the hash index
            dwHashIndex = CalcHashIndex_Key(pvKey);

            // Search the hash table
            while((pvExistingObject = m_HashTable[dwHashIndex]) != NULL)
            {
                // Check if hash being inserted conflicts with an existing hash
                if(CompareObject_Key(pvExistingObject, pvKey))
                    return false;

                // Move to the next entry
                dwHashIndex = (dwHashIndex + 1) & (m_HashTableSize - 1);
            }

            // Insert at that position
            m_HashTable[dwHashIndex] = pvNewObject;
            m_ItemCount++;
            return true;
        }

        // Failed
        return false;
    }

    bool InsertString(const char * szString, bool bCutExtension)
    {
        const char * szExistingString;
        const char * szStringEnd = NULL;
        DWORD dwHashIndex;

        // Verify pointer to the map
        if(m_HashTable != NULL)
        {
            // Limit check
            if(m_ItemCount >= m_HashTableSize)
                return false;

            // Retrieve the length of the string without extension
            if(bCutExtension)
                szStringEnd = GetFileExtension(szString);
            if(szStringEnd == NULL)
                szStringEnd = szString + strlen(szString);

            // Construct the hash index
            dwHashIndex = CalcHashIndex_String(szString, szStringEnd);

            // Search the hash table
            while((szExistingString = (const char *)m_HashTable[dwHashIndex]) != NULL)
            {
                // Check if hash being inserted conflicts with an existing hash
                if(CompareObject_String(szExistingString, szString, szStringEnd))
                    return false;

                // Move to the next entry
                dwHashIndex = (dwHashIndex + 1) & (m_HashTableSize - 1);
            }

            // Insert at that position
            m_HashTable[dwHashIndex] = (void *)szString;
            m_ItemCount++;
            return true;
        }

        // Failed
        return false;
    }

    const char * FindString(const char * szString, const char * szStringEnd)
    {
        const char * szExistingString;
        DWORD dwHashIndex;

        // Verify pointer to the map
        if(m_HashTable != NULL)
        {
            // Construct the main index
            dwHashIndex = CalcHashIndex_String(szString, szStringEnd);

            // Search the hash table
            while((szExistingString = (const char *)m_HashTable[dwHashIndex]) != NULL)
            {
                // Compare the hash
                if(CompareObject_String(szExistingString, szString, szStringEnd))
                    return szExistingString;

                // Move to the next entry
                dwHashIndex = (dwHashIndex + 1) & (m_HashTableSize - 1);
            }
        }

        // Not found, sorry
        return NULL;
    }

    void * ItemAt(size_t nIndex)
    {
        assert(nIndex < m_HashTableSize);
        return m_HashTable[nIndex];
    }

    size_t HashTableSize()
    {
        return m_HashTableSize;
    }

    size_t ItemCount()
    {
        return m_ItemCount;
    }

    bool IsInitialized()
    {
        return (m_HashTable && m_HashTableSize);
    }

    void Free()
    {
        if(m_HashTable != NULL)
            CASC_FREE(m_HashTable);
        m_HashTable = NULL;
    }

    protected:

    // Calculates hash index from a key
    DWORD CalcHashIndex_Key(void * pvKey)
    {
        if(!m_bKeyIsHash)
        {
            LPBYTE pbKey = (LPBYTE)pvKey;
            DWORD dwHash = 0x7EEE7EEE;

            // Construct the hash from the key
            for(DWORD i = 0; i < m_KeyLength; i++)
                dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ pbKey[i];

            // Return the hash limited by the table size
            return dwHash & (m_HashTableSize - 1);
        }
        else
        {
            // Get the hash directly as value
            DWORD dwHash = ConvertBytesToInteger_4((LPBYTE)pvKey);

            // Return the hash limited by the table size
            return dwHash % (m_HashTableSize - 1);
        }
    }

    DWORD CalcHashIndex_String(const char * szString, const char * szStringEnd)
    {
        LPBYTE pbKeyEnd = (LPBYTE)szStringEnd;
        LPBYTE pbKey = (LPBYTE)szString;
        DWORD dwHash = 0x7EEE7EEE;

        // Hash the string itself
        while(pbKey < pbKeyEnd)
        {
            dwHash = (dwHash >> 24) ^ (dwHash << 5) ^ dwHash ^ AsciiToUpperTable_BkSlash[pbKey[0]];
            pbKey++;
        }

        // Return the hash limited by the table size
        return dwHash & (m_HashTableSize - 1);
    }

    bool CompareObject_Key(void * pvObject, void * pvKey)
    {
        LPBYTE pbObjectKey = (LPBYTE)pvObject + m_KeyOffset;

        return (memcmp(pbObjectKey, pvKey, m_KeyLength) == 0);
    }

    bool CompareObject_String(const char * szExistingString, const char * szString, const char * szStringEnd)
    {
        // Compare the whole part, case insensitive
        while(szString < szStringEnd)
        {
            if(AsciiToUpperTable_BkSlash[*szExistingString] != AsciiToUpperTable_BkSlash[*szString])
                return false;

            szExistingString++;
            szString++;
        }

        return true;
    }

    size_t GetNearestPowerOfTwo(size_t MaxItems)
    {
        size_t PowerOfTwo;
        
        // Round the hash table size up to the nearest power of two
        for(PowerOfTwo = 0x1000; PowerOfTwo < MAX_HASH_TABLE_SIZE; PowerOfTwo <<= 1)
        {
            if(PowerOfTwo > MaxItems)
            {
                return PowerOfTwo;
            }
        }

        // If the hash table is too big, we cannot create the map
        assert(false);
        return 0;
    }


    void ** m_HashTable;                        // Hash table
    size_t m_HashTableSize;                     // Size of the hash table, in entries. Always a power of two.
    size_t m_ItemCount;                         // Number of objects in the map
    size_t m_KeyOffset;                         // How far is the hash from the begin of the objects (in bytes)
    size_t m_KeyLength;                         // Length of the hash key, in bytes
    bool m_bKeyIsHash;                          // If set, then it means that the key is a hash of some sort.
                                                // Will improve performance, as we will not hash a hash :-)
};

#endif // __CASC_MAP_H__
