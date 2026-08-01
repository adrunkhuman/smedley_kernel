#pragma once

#include "reader.hpp"
#include "writer.hpp"

namespace smedley::clausewitz
{

    /**
     * Base class for data persisted by the game, including definition files in
     * the common directory, decisions, events, and history.
     */
    class CPersistent
    {
    protected:
        int _type_token;
    public:
        virtual ~CPersistent();
        /// @brief Writes the complete object to the specified CWriter.
        virtual void Write(CWriter &);
        virtual void WriteMembers(CWriter &);
        /// @brief Deserializes the object from the specified CReader.
        virtual void Read(CReader &);
        /// @brief Deserializes the specified member.
        /// @param reader Reader from which to deserialize.
        /// @param type Type token of the member to deserialize.
        virtual void ReadMember(CReader &reader, int type);
        /// @brief Initializes the object after deserialization; called at the end of CPersistent::Read.
        virtual void InitPostRead();
    };

    static_assert(sizeof(CPersistent) == 0x8);

}
