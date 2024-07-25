// dllmain.cpp : Defines the entry point for the DLL application.
#include "framework.h"
#include <fstream>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)
#pragma intrinsic(_InterlockedCompareExchange)

#define E_FILE_OPEN_FAILED 0x81000001
#define E_ALLOC_FAILED 0x81000002
#define E_READ_FAILED 0x81000003
#define E_FILE_PARSE_FAILED 0x81000004

struct clumpheader
{
    int bool_hasNavVolume; // 0x0

    int navMeshSettingsBufferSize; // 0x4
    int userEdgePairsBufferSize; // 0x8
    int clusterGraphBufferSize; // 0xC
    int mediatorBufferSize; // 0x10
    int metadataBufferSize; // 0x14
    int clearanceCacheSeederBufferSize; // 0x18

    // ignore these values if havNavVolume is 0
    int smallNavVolumeSettingsBufferSize; // 0x1C
    int smallNavVolumeMediatorBufferSize; // 0x20
    int bigNavVolumeSettingsBufferSize; // 0x24
    int bigNavVolumeMediatorBufferSize; // 0x28
    int pad; // 0x2C
};

typedef unsigned char hkUint8;
typedef __int16 hkInt16;
typedef unsigned __int16 hkUint16;
typedef unsigned __int32 hkUint32;
typedef unsigned __int64 hk_size_t;

struct hkArrayBase
{
    void* m_data;
    int m_size;
    int m_capacityAndFlags;
};

struct alignas(8) hkDisplaySerializeOStream
{
    unsigned char dontcare[0x20];
};

struct hkStructureLayout
{
    unsigned char m_bytesInPointer;
    unsigned char m_littleEndian;
    unsigned char m_reusePaddingOptimization;
    unsigned char m_emptyBaseClassOptimization;
};

struct hkVariant
{
    void* m_object;
    const hkClass* m_class;
};

struct hkCustomAttributes_Attribute
{
    const char* m_name;
    hkVariant m_value;
};

struct alignas(8) hkCustomAttributes
{
    const hkCustomAttributes_Attribute* m_attributes;
    int m_numAttributes;
};

struct hkClassEnum_Item
{
    int m_value;
    const char* m_name;
};

struct alignas(8) hkClassEnum
{
    const char* m_name;
    const hkClassEnum_Item* m_items;
    int m_numItems;
    hkCustomAttributes* m_attributes;
    hkUint32 m_flags; // hkClassEnum::Flags
};

struct hkClassMember
{
    const char* m_name;
    const hkClass* m_class;
    const hkClassEnum* m_enum;
    hkUint8 m_type; // hkClassMember::Type
    hkUint8 m_subtype; // hkClassMember::Type
    hkInt16 m_cArraySize;
    hkUint16 m_flags; // hkClassMember::Flags
    hkUint16 m_offset;
    const hkCustomAttributes* m_attributes;
};

struct hkClass
{
    const char* m_name;
    const hkClass* m_parent;
    int m_objectSize;
    int m_numImplementedInterfaces;
    const hkClassEnum* m_declaredEnums;
    int m_numDeclaredEnums;
    const hkClassMember* m_declaredMembers;
    int m_numDeclaredMembers;
    const void* m_defaults;
    const hkCustomAttributes* m_attributes;
    hkUint32 m_flags; // hkClass::Flags
    int m_describedVersion;
};

typedef void (*hkTypeInfo_FinishLoadedObjectFunction)(void*, int);
typedef void (*hkTypeInfo_CleanupLoadedObjectFunction)(void*);

struct hkTypeInfo
{
    const char* m_typeName;
    const char* m_scopedName;
    hkTypeInfo_FinishLoadedObjectFunction m_finishLoadedObjectFunction;
    hkTypeInfo_CleanupLoadedObjectFunction m_cleanupLoadedObjectFunction;
    const void* m_vtable;
    hk_size_t m_size;
};

struct hkaiNavMeshClearanceCacheSeeder_vftable_s
{
    __int64 unk1;
    __int64 unk2;
    __int64 unk3;
    __int64 unk4;
};

enum hkObjectCopier_ObjectCopierFlagBits : __int32
{
    FLAG_NONE = 0x0,
    FLAG_APPLY_DEFAULT_IF_SERIALIZE_IGNORED = 0x1,
    FLAG_RESPECT_SERIALIZE_IGNORED = 0x2,
};

void chgmem(__int64 addy, __int32 size, void* copy)
{
    DWORD oldprotect;
    VirtualProtect((void*)addy, size, PAGE_EXECUTE_READWRITE, &oldprotect);
    memcpy((void*)addy, copy, size);
    VirtualProtect((void*)addy, size, oldprotect, &oldprotect);
}

int debug_part = 0;
void dump_hk_object_mac(hkArrayBase& arr, void* obj, int objsize)
{
    // hkNativePackfileUtils::loadInPlace
    void* linked_object = ((char* (__fastcall*)(void* packfileData, int sizeOfData, __int64 discard1, __int64 discard2))0x1404A2140ull)(obj, objsize, 0, 0);

    DEBUG_PRINT("DBG: LNK %p size (%d)", linked_object, objsize);

    // create an out stream
    hkDisplaySerializeOStream stream{ 0 };
    // hkDisplaySerializeOStream::hkDisplaySerializeOStream
    ((void(__fastcall*)(hkDisplaySerializeOStream&, hkArrayBase&))0x1404B7B30ull)(stream, arr);

    // mac layout
    hkStructureLayout targetLayout;
    targetLayout.m_bytesInPointer = 8;
    targetLayout.m_littleEndian = 1;
    targetLayout.m_reusePaddingOptimization = 1;
    targetLayout.m_emptyBaseClassOptimization = 1;

    ((void(__fastcall*)(hkDisplaySerializeOStream& stream, void* object, bool writePacket, bool writePackFile, hkStructureLayout layout, __int64, hkObjectCopier_ObjectCopierFlagBits))0x1404B2010ull)(stream, linked_object, false, true, targetLayout, NULL, FLAG_RESPECT_SERIALIZE_IGNORED);

    DEBUG_PRINT("DBG: SL size (%d)", arr.m_size);
}

int convert_navmesh(const char* input, const char* output)
{
    std::ifstream infile;
    infile.open(input, std::ifstream::in | std::ifstream::binary);
    if (!infile.is_open())
    {
        printf("C2MPatch: FAILED to open input file\n");
        return E_FILE_OPEN_FAILED;
    }

    DEBUG_PRINT("DBG: OPENED CLUMP");

    // parse the data we need
    infile.seekg(0, std::ios_base::end);
    auto size = infile.tellg();

    if (size < sizeof(clumpheader))
    {
        printf("C2MPatch: FAILED to parse input file\n");
        return E_FILE_PARSE_FAILED;
    }

    DEBUG_PRINT("DBG: INPUT SIZE %d (%x)", (int)size, (int)size);

    char* cl_buf = (char*)malloc(size);

    if (!cl_buf)
    {
        printf("C2MPatch: FAILED to allocate space for input file in memory\n");
        return E_ALLOC_FAILED;
    }

    DEBUG_PRINT("DBG: BUFFER ALLOCATED");

    infile.seekg(0, std::ios_base::beg);
    infile.read(cl_buf, size);

    if (infile.bad())
    {
        printf("C2MPatch: FAILED to read data for input file in memory\n");
        return E_READ_FAILED;
    }

    DEBUG_PRINT("DBG: READ DATA");

    infile.close();

    clumpheader* head = (clumpheader*)cl_buf;

    int* field = (int*)((char*)head + offsetof(clumpheader, navMeshSettingsBufferSize));
    char* field_data = (char*)head + sizeof(clumpheader);

    int numfields = (offsetof(clumpheader, smallNavVolumeSettingsBufferSize) - offsetof(clumpheader, navMeshSettingsBufferSize)) / sizeof(int);

    char navmesh_path[1024]{ 0 };
    strcat_s(navmesh_path, output);
    strcat_s(navmesh_path, "_navmesh.hkt");

    std::ofstream navout;
    navout.open(navmesh_path, std::ofstream::out | std::ofstream::binary);
    if (!navout.is_open())
    {
        printf("C2MPatch: FAILED to open output file (navmesh)\n");
        return E_FILE_OPEN_FAILED;
    }

    DEBUG_PRINT("DBG: BEGIN WRITE NAVMESH");

    for (int i = 0; i < numfields; i++, field++)
    {
        if (*field)
        {
            hkArrayBase navMeshArray{ 0 };
            navMeshArray.m_capacityAndFlags = 0x80000000;
            dump_hk_object_mac(navMeshArray, field_data, *field);
            navout.write((5 + (char*)navMeshArray.m_data), navMeshArray.m_size - 5); // I think size?? I don't know, always extra data 
        }
        field_data += *field;
    }
    
    navout.close();

    DEBUG_PRINT("DBG: FINISHED NAVMESH");

    if (!head->bool_hasNavVolume)
    {
        return 0;
    }

    char navvol_path[1024]{ 0 };
    strcat_s(navvol_path, output);
    strcat_s(navvol_path, "_navvolume.hkt");

    field = (int*)((char*)head + offsetof(clumpheader, smallNavVolumeSettingsBufferSize));
    numfields = (sizeof(clumpheader) - offsetof(clumpheader, smallNavVolumeSettingsBufferSize)) / sizeof(int);

    DEBUG_PRINT("DBG: BEGIN WRITE NAVVOLUME");

    std::ofstream navvolout;
    navvolout.open(navvol_path, std::ofstream::out | std::ofstream::binary);
    if (!navvolout.is_open())
    {
        printf("C2MPatch: FAILED to open output file (navvolume)\n");
        return E_FILE_OPEN_FAILED;
    }

    for (int i = 0; i < numfields; i++, field++)
    {
        if (*field)
        {
            hkArrayBase navMeshArray{ 0 };
            navMeshArray.m_capacityAndFlags = 0x80000000;
            dump_hk_object_mac(navMeshArray, field_data, *field);
            navvolout.write((5 + (char*)navMeshArray.m_data), navMeshArray.m_size - 5); // I think size?? I don't know, always extra data 
        }
        field_data += *field;
    }

    navvolout.close();

    DEBUG_PRINT("DBG: FINISHED NAVVOLUME");
    return 0;
}

#if TEST_MAPBUILD_THEORY
MDT_Define_FASTCALL(0x140084100ull, timer_hook, double, ())
{
    if ((__int64)_ReturnAddress() != 0x1400CA9FAull)
    {
        return MDT_ORIGINAL(timer_hook, ());
    }

    exit(convert_navmesh("convert.clump", "convert.test"));

    return MDT_ORIGINAL(timer_hook, ());
}
#endif

hkClass hkaiNavMeshClearanceCacheSeederClass{ 0 };
hkTypeInfo hkaiNavMeshClearanceCacheSeederTypeInfo{ 0 };
hkClassMember hkaiNavMeshClearanceCacheSeederMembers{ 0 };
// TODO: probably need RTTI Complete Object Locator
hkaiNavMeshClearanceCacheSeeder_vftable_s hkaiNavMeshClearanceCacheSeeder_vftable{ 0 };

void finishLoadedObjecthkaiNavMeshClearanceCacheSeeder(void* p, int finishing)
{
    DEBUG_PRINT("DBG: finishLoadedObjecthkaiNavMeshClearanceCacheSeeder");
    *(void**)p = &hkaiNavMeshClearanceCacheSeeder_vftable;
}

void cleanupLoadedObjecthkaiNavMeshClearanceCacheSeeder(void* a1)
{
    DEBUG_PRINT("DBG: cleanupLoadedObjecthkaiNavMeshClearanceCacheSeeder");
    ((void(__fastcall*)(void*, __int64))(**(__int64**)a1))(a1, 0);
}

void nullsub() {}
__int64 ret0() { return 0; }

void** __fastcall sub_7FF79B686630(__int64 a1)
{
    DEBUG_PRINT("DBG: sub_7FF79B686630");
    __int64 v1; // rsi
    int v2; // eax
    __int64 v3; // rcx
    int v4; // eax
    __int64 v5; // rdi
    __int64* v6; // rbx
    signed __int32 v8; // eax
    int v9; // ett
    int v10; // er8
    void** result; // rax

    __int64 _RCX;

    v1 = a1;
    v2 = *(__int64*)(a1 + 24);
    v3 = *(__int64*)(a1 + 16);
    v4 = v2 - 1;
    v5 = v4;
    if (v4 >= 0)
    {
        v6 = (__int64*)(v3 + 8ull * v4);
        do
        {
            _RCX = *v6;
            if (*v6)
            {
                if (*(__int16*)(_RCX + 10))
                {
                    // __asm { prefetchw byte ptr[rcx + 8] }
                    do
                    {
                        v8 = *(__int32*)(_RCX + 8);
                        v9 = *(__int32*)(_RCX + 8);
                    } while (v9 != _InterlockedCompareExchange(
                        (volatile long*)(_RCX + 8),
                        v8 ^ (unsigned __int16)(v8 ^ (v8 - 1)),
                        v8));
                    if ((__int16)v8 == 1)
                        (*(void(__fastcall**)(__int64))(*(__int64*)_RCX + 24ull))(_RCX);
                }
            }
            *v6 = 0ull;
            --v6;
            --v5;
        } while (v5 >= 0);
    }
    *(__int32*)(v1 + 24) = 0;
    v10 = *(__int32*)(v1 + 28);

    if (v10 >= 0)
    {
        auto off_140DD9DB8 = *(__int64*)0x140DD9DB8ull;
        ((void(__fastcall*)(__int64, __int64, __int64))(*(__int64*)off_140DD9DB8 + 0x20))(
            0x140DD9DB8ull,
            *(__int64*)(v1 + 16),
            (unsigned int)(8 * v10));
    }

    *(__int64*)(v1 + 16) = 0ull;
    *(__int32*)(v1 + 28) = 2147483648;
    result = (void**)0x140A06690ull; // &hkBaseObject::`vftable'
    * (__int64*)v1 = 0x140A06690ull;
    return result;
}

__int64 __fastcall hkaiNavMeshClearanceCacheSeeder_ctor(__int64 a1, char a2)
{
    DEBUG_PRINT("DBG: hkaiNavMeshClearanceCacheSeeder_ctor");
    char v2; // bl
    __int64 v3; // rdi
    unsigned __int16 v4; // ax
    unsigned int v5; // ebx
    __int64** v6; // rax

    v2 = a2;
    v3 = a1;
    sub_7FF79B686630(a1);
    if (v2 & 1)
    {
        v4 = *(unsigned __int16*)(v3 + 10);
        v5 = 32;
        if (v4 != -1)
            v5 = v4;
        v6 = (__int64**)TlsGetValue(0x1B4D37E90ull); // dwTlsIndex
        (*(void(__fastcall**)(__int64*, __int64, __int64))(*v6[11] + 16))(v6[11], v3, v5);
    }
    return v3;
}

void hkSetup()
{
    DEBUG_PRINT("DBG: PATCHING NAVMESH GENERATION");
    // patch the navmesh generation function so that it only sets up havok
    unsigned char patchdata[12] = { 0x48, 0xB8, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xFF, 0xE0 };
    *(__int64*)(patchdata + 2) = 0x1400CB2A7ull;

    chgmem(0x1400CA7D9ull, 12, patchdata);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)0x1400CA7D9ull, 12);

    DEBUG_PRINT("DBG: HAVOK INITIALIZATION");
    // setup havok
    ((void(__fastcall*)(LPVOID))0x1400CA620llu)((LPVOID)0x1864D5310ull);

    hkaiNavMeshClearanceCacheSeeder_vftable.unk1 = (__int64)&hkaiNavMeshClearanceCacheSeeder_ctor;
    hkaiNavMeshClearanceCacheSeeder_vftable.unk2 = (__int64)&nullsub;
    hkaiNavMeshClearanceCacheSeeder_vftable.unk3 = (__int64)&ret0;
    hkaiNavMeshClearanceCacheSeeder_vftable.unk4 = 0x14031E240ull; // hkReferencedObject has the same function pointer in mt linker so we will just do that here too

    hkaiNavMeshClearanceCacheSeederMembers.m_name = "caches";
    hkaiNavMeshClearanceCacheSeederMembers.m_class = (hkClass*)0x1B4D33660ull; // hkaiNavMeshClearanceCache class definition
    hkaiNavMeshClearanceCacheSeederMembers.m_type = 0x16;
    hkaiNavMeshClearanceCacheSeederMembers.m_subtype = 0x14;
    hkaiNavMeshClearanceCacheSeederMembers.m_offset = 0x10;

    hkaiNavMeshClearanceCacheSeederClass.m_name = "hkaiNavMeshClearanceCacheSeeder";
    hkaiNavMeshClearanceCacheSeederClass.m_parent = (hkClass*)0x1B4FF21A0ull; // hkReferencedObjectClass
    hkaiNavMeshClearanceCacheSeederClass.m_objectSize = 32;
    hkaiNavMeshClearanceCacheSeederClass.m_numImplementedInterfaces = 0;
    hkaiNavMeshClearanceCacheSeederClass.m_declaredEnums = 0;
    hkaiNavMeshClearanceCacheSeederClass.m_numDeclaredEnums = 0;
    hkaiNavMeshClearanceCacheSeederClass.m_declaredMembers = &hkaiNavMeshClearanceCacheSeederMembers;
    hkaiNavMeshClearanceCacheSeederClass.m_numDeclaredMembers = 1;

    hkaiNavMeshClearanceCacheSeederTypeInfo.m_typeName = "hkaiNavMeshClearanceCacheSeeder";
    hkaiNavMeshClearanceCacheSeederTypeInfo.m_scopedName = "!hkaiNavMeshClearanceCacheSeeder";
    hkaiNavMeshClearanceCacheSeederTypeInfo.m_finishLoadedObjectFunction = finishLoadedObjecthkaiNavMeshClearanceCacheSeeder;
    hkaiNavMeshClearanceCacheSeederTypeInfo.m_cleanupLoadedObjectFunction = cleanupLoadedObjecthkaiNavMeshClearanceCacheSeeder;
    hkaiNavMeshClearanceCacheSeederTypeInfo.m_vtable = &hkaiNavMeshClearanceCacheSeeder_vftable;
    hkaiNavMeshClearanceCacheSeederTypeInfo.m_size = 0x20uLL;

    // hkSingleton<hkBuiltinTypeRegistry>::s_instance
    auto singleton_instance = *(__int64*)0x1B503A988ull;

    DEBUG_PRINT("DBG: ADD TYPE hkaiNavMeshClearanceCacheSeeder");
    
    // hkBuiltinTypeRegistry::addType
    ((void(__fastcall*)(__int64, hkTypeInfo&, hkClass&))(*(__int64*)(*(__int64*)singleton_instance + 0x38)))(singleton_instance, hkaiNavMeshClearanceCacheSeederTypeInfo, hkaiNavMeshClearanceCacheSeederClass);

    DEBUG_PRINT("DBG: FINISHED HAVOK PATCHING");
}

MDT_Define_FASTCALL(0x1400786E0ull, entrypoint_hook, int, (int argc, const char** argv, const char** envp))
{
    // shenanigans: check
    if (argc == 4 && !strcmp(argv[1], "-mac_navmesh"))
    {
        printf("C2MPatch: Mac navmesh convert requested\n");
        printf("C2MPatch: Input: %s\n", argv[2]);
        printf("C2MPatch: Output: %s\n", argv[3]);

        hkSetup();

        exit(convert_navmesh(argv[2], argv[3]));
    }

    return MDT_ORIGINAL(entrypoint_hook, (argc, argv, envp));
}

EXPORT void dummy_proc() // for the iat entry, nothing more
{
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DEBUG_PRINT("DBG: Loaded c2mpatch!");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        MDT_Activate(entrypoint_hook);

#if TEST_MAPBUILD_THEORY
        MDT_Activate(timer_hook);
#endif

        DetourTransactionCommit();

        DEBUG_PRINT("DBG: Installed hooks!");
    }
    break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}