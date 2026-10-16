#ifndef CORE_COSMETICS_UI_PROFILE_H
#define CORE_COSMETICS_UI_PROFILE_H
// Exact original entry bytes from the cloud-reviewed Core UUID.
typedef struct { uintptr_t rva; const char *hex; } CCPUIEntry;
static const CCPUIEntry CCPUIEntries[] = {
    {0xC88B0, "7f2303d5f657bda9f44f01a9fd7b02a9fd830091f30303aaf40301aaf50300aa"},
    {0xC8928, "7f2303d5ffc303d1ef3b056ded33066deb2b076de923086dfc6f09a9fa670aa9"},
    {0x12E620, "7f2303d5ff0302d1e923036df85f04a9f65705a9f44f06a9fd7b07a9fdc30191"},
    {0x133168, "085900b008a144f9085d4af92900805209390339001540fd014d40fd00d4a10e"},
    {0x14F980, "285800b008012591080140f9085d4af9290080520939033909454339a9020037"},
};
#endif
