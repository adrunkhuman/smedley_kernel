#pragma once

#include "persistent.hpp"
#include "../std/string.hpp"

namespace smedley::clausewitz
{

    /**
     * Identifies a CReferenceObject instance.
     */
    class CID
    {
    protected:
        unsigned int _type; 
        unsigned int _id;
    public:
        /// @return The type ID of the CReferenceObject subclass.
        int type() const { return _type; }
        /// @return The instance ID.
        int id() const { return _id; }
    };

    static_assert(sizeof(CID) == 0x8);

    /**
     * The base class for all game objects indexable by a
     * CID.
     */
    class CReferenceObject : public CPersistent
    {
    protected:
        CID _id;
        bool _is_created;
        sstd::string _session_name;
    public:
        virtual void Create(CID &, bool);
        virtual void Create();
    };

    static_assert(sizeof(CReferenceObject) == 0x30);

}
