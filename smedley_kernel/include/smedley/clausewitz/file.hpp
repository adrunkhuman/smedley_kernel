#pragma once

#include <cstdint>
#include "../std/string.hpp"

namespace smedley::clausewitz
{

    struct PHYSFS_File
    {
        void *opaque;
    };

    class CBlob;

    /**
     * Base class for game file access.
     */
    class CFile
    {
    protected:
        uint32_t _unk_0x4;
        uint32_t _unk_0x8;
        uint32_t _unk_0xc;
        uint32_t _unk_0x10;
        sstd::string _filename;
    public:
        virtual ~CFile(); // Vtable slot 0x00
        virtual char Get(); // Vtable slot 0x04
        virtual bool ReadData(CBlob &blob); // Vtable slot 0x08
        virtual bool Read(void *, int n); // Vtable slot 0x0C
        virtual bool ReadString(CBlob &blob); // Vtable slot 0x10
        virtual void WriteString(sstd::string &); // Vtable slot 0x14
        virtual void WriteULong(unsigned long); // Vtable slot 0x18
        virtual void WriteByte(unsigned int); // Vtable slot 0x1C
        virtual void WriteData(void *, unsigned int); // Vtable slot 0x20
        virtual bool IsValid(); // Vtable slot 0x24
        virtual bool SeekStart(int); // Vtable slot 0x28
        virtual bool SeekEnd(); // Vtable slot 0x2C
        virtual bool Reset(); // Vtable slot 0x30
        virtual bool Flush(); // Vtable slot 0x38
        virtual int GetSize(); // Vtable slot 0x3C
        virtual int *GetFilePointer(); // Vtable slot 0x40
        virtual int CalculateChecksum(int); // Vtable slot 0x44
    };

    static_assert(sizeof(CFile) == 0x30);

}
