#include "game/packet_parsers.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace game {

// ============================================================================
// Classic 1.12.1 movement flag constants
// Key differences from TBC:
// - SPLINE_ENABLED at 0x00400000 (TBC/WotLK: 0x08000000)
// - No FLYING flag (flight was added in TBC)
// - ONTRANSPORT at 0x02000000 (not used for pitch in Classic)
// Same as TBC: ON_TRANSPORT=0x200, JUMPING=0x2000, SWIMMING=0x200000,
//              SPLINE_ELEVATION=0x04000000
// ============================================================================
namespace ClassicMoveFlags {
    constexpr uint32_t ONTRANSPORT      = 0x02000000;  // Gates transport data (vmangos authoritative)
    constexpr uint32_t JUMPING          = 0x00002000;  // Gates jump data
    constexpr uint32_t SWIMMING         = 0x00200000;  // Gates pitch
    constexpr uint32_t SPLINE_ENABLED   = 0x00400000;  // TBC/WotLK: 0x08000000
    constexpr uint32_t SPLINE_ELEVATION = 0x04000000;  // Same as TBC
}

// ============================================================================
// Classic parseMovementBlock
// Key differences from TBC:
// - NO moveFlags2 (TBC reads u8, WotLK reads u16)
// - SPLINE_ENABLED at 0x00400000 (not 0x08000000)
// - Transport data: NO timestamp (TBC adds u32 timestamp)
// - Pitch: only SWIMMING (no ONTRANSPORT secondary pitch, no FLYING)
// Same as TBC: u8 UpdateFlags, JUMPING=0x2000, 8 speeds, no pitchRate
// ============================================================================
bool ClassicPacketParsers::parseMovementBlock(network::Packet& packet, UpdateBlock& block) {
    // Classic: UpdateFlags is uint8 (same as TBC)
    uint8_t updateFlags = packet.readUInt8();
    block.updateFlags = static_cast<uint16_t>(updateFlags);

    LOG_DEBUG("  [Classic] UpdateFlags: 0x", std::hex, (int)updateFlags, std::dec);

    const uint8_t UPDATEFLAG_LIVING       = 0x20;
    const uint8_t UPDATEFLAG_HAS_POSITION = 0x40;
    const uint8_t UPDATEFLAG_HAS_TARGET   = 0x04;
    const uint8_t UPDATEFLAG_TRANSPORT    = 0x02;
    const uint8_t UPDATEFLAG_LOWGUID      = 0x08;
    const uint8_t UPDATEFLAG_HIGHGUID     = 0x10;

    if (updateFlags & UPDATEFLAG_LIVING) {
        // Movement flags (u32 only — NO extra flags byte in Classic)
        uint32_t moveFlags = packet.readUInt32();
        /*uint32_t time =*/ packet.readUInt32();

        // Position
        block.x = packet.readFloat();
        block.y = packet.readFloat();
        block.z = packet.readFloat();
        block.orientation = packet.readFloat();
        block.hasMovement = true;

        LOG_DEBUG("  [Classic] LIVING: (", block.x, ", ", block.y, ", ", block.z,
                  "), o=", block.orientation, " moveFlags=0x", std::hex, moveFlags, std::dec);

        // Transport data (Classic: ONTRANSPORT=0x02000000, no timestamp)
        if (moveFlags & ClassicMoveFlags::ONTRANSPORT) {
            block.onTransport = true;
            block.transportGuid = UpdateObjectParser::readPackedGuid(packet);
            block.transportX = packet.readFloat();
            block.transportY = packet.readFloat();
            block.transportZ = packet.readFloat();
            block.transportO = packet.readFloat();
            // Classic: NO transport timestamp (TBC adds u32 timestamp)
            // Classic: NO transport seat byte
        }

        // Pitch (Classic: only SWIMMING, no FLYING or ONTRANSPORT pitch)
        if (moveFlags & ClassicMoveFlags::SWIMMING) {
            /*float pitch =*/ packet.readFloat();
        }

        // Fall time (always present)
        /*uint32_t fallTime =*/ packet.readUInt32();

        // Jumping (Classic: JUMPING=0x2000, same as TBC)
        if (moveFlags & ClassicMoveFlags::JUMPING) {
            /*float jumpVelocity =*/ packet.readFloat();
            /*float jumpSinAngle =*/ packet.readFloat();
            /*float jumpCosAngle =*/ packet.readFloat();
            /*float jumpXYSpeed =*/ packet.readFloat();
        }

        // Spline elevation
        if (moveFlags & ClassicMoveFlags::SPLINE_ELEVATION) {
            /*float splineElevation =*/ packet.readFloat();
        }

        // Speeds (Classic: 6 values — no flight speeds, no pitchRate)
        // TBC added flying_speed + backwards_flying_speed (8 total)
        // WotLK added pitchRate (9 total)
        /*float walkSpeed =*/ packet.readFloat();
        float runSpeed = packet.readFloat();
        /*float runBackSpeed =*/ packet.readFloat();
        /*float swimSpeed =*/ packet.readFloat();
        /*float swimBackSpeed =*/ packet.readFloat();
        /*float turnRate =*/ packet.readFloat();

        block.runSpeed = runSpeed;

        // Spline data (Classic: SPLINE_ENABLED=0x00400000)
        if (moveFlags & ClassicMoveFlags::SPLINE_ENABLED) {
            uint32_t splineFlags = packet.readUInt32();
            LOG_DEBUG("  [Classic] Spline: flags=0x", std::hex, splineFlags, std::dec);

            if (splineFlags & 0x00010000) { // FINAL_POINT
                /*float finalX =*/ packet.readFloat();
                /*float finalY =*/ packet.readFloat();
                /*float finalZ =*/ packet.readFloat();
            } else if (splineFlags & 0x00020000) { // FINAL_TARGET
                /*uint64_t finalTarget =*/ packet.readUInt64();
            } else if (splineFlags & 0x00040000) { // FINAL_ANGLE
                /*float finalAngle =*/ packet.readFloat();
            }

            // Classic spline: timePassed, duration, id, nodes, finalNode (same as TBC)
            /*uint32_t timePassed =*/ packet.readUInt32();
            /*uint32_t duration =*/ packet.readUInt32();
            /*uint32_t splineId =*/ packet.readUInt32();

            uint32_t pointCount = packet.readUInt32();
            if (pointCount > 256) {
                LOG_WARNING("  [Classic] Spline pointCount=", pointCount, " exceeds max, capping");
                pointCount = 0;
            }
            for (uint32_t i = 0; i < pointCount; i++) {
                /*float px =*/ packet.readFloat();
                /*float py =*/ packet.readFloat();
                /*float pz =*/ packet.readFloat();
            }

            // Classic: NO splineMode byte
            /*float endPointX =*/ packet.readFloat();
            /*float endPointY =*/ packet.readFloat();
            /*float endPointZ =*/ packet.readFloat();
        }
    }
    else if (updateFlags & UPDATEFLAG_HAS_POSITION) {
        block.x = packet.readFloat();
        block.y = packet.readFloat();
        block.z = packet.readFloat();
        block.orientation = packet.readFloat();
        block.hasMovement = true;

        LOG_DEBUG("  [Classic] STATIONARY: (", block.x, ", ", block.y, ", ", block.z, ")");
    }

    // Target GUID
    if (updateFlags & UPDATEFLAG_HAS_TARGET) {
        /*uint64_t targetGuid =*/ UpdateObjectParser::readPackedGuid(packet);
    }

    // Transport time
    if (updateFlags & UPDATEFLAG_TRANSPORT) {
        /*uint32_t transportTime =*/ packet.readUInt32();
    }

    // Low GUID
    if (updateFlags & UPDATEFLAG_LOWGUID) {
        /*uint32_t lowGuid =*/ packet.readUInt32();
    }

    // High GUID
    if (updateFlags & UPDATEFLAG_HIGHGUID) {
        /*uint32_t highGuid =*/ packet.readUInt32();
    }

    return true;
}

// ============================================================================
// Classic writeMovementPayload
// Key differences from TBC:
// - NO flags2 byte (TBC writes u8)
// - Transport data: NO timestamp
// - Pitch: only SWIMMING (no ONTRANSPORT pitch)
// ============================================================================
void ClassicPacketParsers::writeMovementPayload(network::Packet& packet, const MovementInfo& info) {
    // Movement flags (uint32)
    packet.writeUInt32(info.flags);

    // Classic: NO flags2 byte (TBC has u8, WotLK has u16)

    // Timestamp
    packet.writeUInt32(info.time);

    // Position
    packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.x), sizeof(float));
    packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.y), sizeof(float));
    packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.z), sizeof(float));
    packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.orientation), sizeof(float));

    // Transport data (Classic ONTRANSPORT = 0x02000000, no timestamp)
    if (info.flags & ClassicMoveFlags::ONTRANSPORT) {
        // Packed transport GUID
        uint8_t transMask = 0;
        uint8_t transGuidBytes[8];
        int transGuidByteCount = 0;
        for (int i = 0; i < 8; i++) {
            uint8_t byte = static_cast<uint8_t>((info.transportGuid >> (i * 8)) & 0xFF);
            if (byte != 0) {
                transMask |= (1 << i);
                transGuidBytes[transGuidByteCount++] = byte;
            }
        }
        packet.writeUInt8(transMask);
        for (int i = 0; i < transGuidByteCount; i++) {
            packet.writeUInt8(transGuidBytes[i]);
        }

        // Transport local position
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.transportX), sizeof(float));
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.transportY), sizeof(float));
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.transportZ), sizeof(float));
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.transportO), sizeof(float));

        // Classic: NO transport timestamp
        // Classic: NO transport seat byte
    }

    // Pitch (Classic: only SWIMMING)
    if (info.flags & ClassicMoveFlags::SWIMMING) {
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.pitch), sizeof(float));
    }

    // Fall time (always present)
    packet.writeUInt32(info.fallTime);

    // Jump data (Classic JUMPING = 0x2000)
    if (info.flags & ClassicMoveFlags::JUMPING) {
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.jumpVelocity), sizeof(float));
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.jumpSinAngle), sizeof(float));
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.jumpCosAngle), sizeof(float));
        packet.writeBytes(reinterpret_cast<const uint8_t*>(&info.jumpXYSpeed), sizeof(float));
    }
}

// ============================================================================
// Classic buildMovementPacket
// Classic/TBC: client movement packets do NOT include PackedGuid prefix
// (WotLK added PackedGuid to client packets)
// ============================================================================
network::Packet ClassicPacketParsers::buildMovementPacket(LogicalOpcode opcode,
                                                           const MovementInfo& info,
                                                           uint64_t /*playerGuid*/) {
    network::Packet packet(wireOpcode(opcode));

    // Classic: NO PackedGuid prefix for client packets
    writeMovementPayload(packet, info);

    return packet;
}

// ============================================================================
// Classic buildCastSpell
// Vanilla 1.12.x: NO castCount prefix, NO castFlags byte
// Format: uint32 spellId + uint32 targetFlags + [PackedGuid if unit target]
// ============================================================================
network::Packet ClassicPacketParsers::buildCastSpell(uint32_t spellId, uint64_t targetGuid, uint8_t /*castCount*/) {
    network::Packet packet(wireOpcode(LogicalOpcode::CMSG_CAST_SPELL));

    packet.writeUInt32(spellId);

    // SpellCastTargets — vanilla/CMaNGOS uses uint32 target mask (same as WotLK)
    if (targetGuid != 0) {
        packet.writeUInt32(0x02); // TARGET_FLAG_UNIT

        // Write packed GUID
        uint8_t mask = 0;
        uint8_t bytes[8];
        int byteCount = 0;
        uint64_t g = targetGuid;
        for (int i = 0; i < 8; ++i) {
            uint8_t b = g & 0xFF;
            if (b != 0) {
                mask |= (1 << i);
                bytes[byteCount++] = b;
            }
            g >>= 8;
        }
        packet.writeUInt8(mask);
        for (int i = 0; i < byteCount; ++i) {
            packet.writeUInt8(bytes[i]);
        }
    } else {
        packet.writeUInt32(0x00); // TARGET_FLAG_SELF
    }

    return packet;
}

// ============================================================================
// Classic SMSG_CAST_FAILED: no castCount byte (added in TBC/WotLK)
// Format: spellId(u32) + result(u8)
// ============================================================================
bool ClassicPacketParsers::parseCastFailed(network::Packet& packet, CastFailedData& data) {
    data.castCount = 0;
    data.spellId = packet.readUInt32();
    data.result = packet.readUInt8();
    LOG_INFO("[Classic] Cast failed: spell=", data.spellId, " result=", (int)data.result);
    return true;
}

// ============================================================================
// Classic 1.12.1 parseCharEnum
// Differences from TBC:
// - Equipment: 20 items, but NO enchantment field per slot
//   Classic: displayId(u32) + inventoryType(u8) = 5 bytes/slot
//   TBC/WotLK: displayId(u32) + inventoryType(u8) + enchant(u32) = 9 bytes/slot
// - After flags: uint8 firstLogin (same as TBC)
// ============================================================================
bool ClassicPacketParsers::parseCharEnum(network::Packet& packet, CharEnumResponse& response) {
    uint8_t count = packet.readUInt8();

    LOG_INFO("[Classic] Parsing SMSG_CHAR_ENUM: ", (int)count, " characters");

    response.characters.clear();
    response.characters.reserve(count);

    for (uint8_t i = 0; i < count; ++i) {
        Character character;

        // GUID (8 bytes)
        character.guid = packet.readUInt64();

        // Name (null-terminated string)
        character.name = packet.readString();

        // Race, class, gender
        character.race = static_cast<Race>(packet.readUInt8());
        character.characterClass = static_cast<Class>(packet.readUInt8());
        character.gender = static_cast<Gender>(packet.readUInt8());

        // Appearance (5 bytes: skin, face, hairStyle, hairColor packed + facialFeatures)
        character.appearanceBytes = packet.readUInt32();
        character.facialFeatures = packet.readUInt8();

        // Level
        character.level = packet.readUInt8();

        // Location
        character.zoneId = packet.readUInt32();
        character.mapId = packet.readUInt32();
        character.x = packet.readFloat();
        character.y = packet.readFloat();
        character.z = packet.readFloat();

        // Guild ID
        character.guildId = packet.readUInt32();

        // Flags
        character.flags = packet.readUInt32();

        // Classic: uint8 firstLogin (same as TBC)
        /*uint8_t firstLogin =*/ packet.readUInt8();

        // Pet data (always present)
        character.pet.displayModel = packet.readUInt32();
        character.pet.level = packet.readUInt32();
        character.pet.family = packet.readUInt32();

        // Equipment (Classic: 20 items, NO enchantment field)
        character.equipment.reserve(20);
        for (int j = 0; j < 20; ++j) {
            EquipmentItem item;
            item.displayModel = packet.readUInt32();
            item.inventoryType = packet.readUInt8();
            item.enchantment = 0;  // Classic has no enchant field in char enum
            character.equipment.push_back(item);
        }

        LOG_INFO("  Character ", (int)(i + 1), ": ", character.name);
        LOG_INFO("    GUID: 0x", std::hex, character.guid, std::dec);
        LOG_INFO("    ", getRaceName(character.race), " ",
                 getClassName(character.characterClass), " (",
                 getGenderName(character.gender), ")");
        LOG_INFO("    Level: ", (int)character.level);
        LOG_INFO("    Location: Zone ", character.zoneId, ", Map ", character.mapId);

        response.characters.push_back(character);
    }

    LOG_INFO("[Classic] Parsed ", response.characters.size(), " characters");
    return true;
}

// ============================================================================
// Classic 1.12.1 parseMessageChat
// Differences from WotLK:
// - NO uint32 unknown field after senderGuid
// - CHANNEL type: channelName + rank(u32) + senderGuid (not just channelName)
// - No ACHIEVEMENT/GUILD_ACHIEVEMENT types
// ============================================================================
bool ClassicPacketParsers::parseMessageChat(network::Packet& packet, MessageChatData& data) {
    if (packet.getSize() < 10) {
        LOG_ERROR("[Classic] SMSG_MESSAGECHAT packet too small: ", packet.getSize(), " bytes");
        return false;
    }

    // Read chat type
    uint8_t typeVal = packet.readUInt8();
    data.type = static_cast<ChatType>(typeVal);

    // Read language
    uint32_t langVal = packet.readUInt32();
    data.language = static_cast<ChatLanguage>(langVal);

    // Classic: NO uint32 unknown field here (WotLK has one)

    // Type-specific data
    switch (data.type) {
        case ChatType::MONSTER_SAY:
        case ChatType::MONSTER_YELL:
        case ChatType::MONSTER_EMOTE: {
            // nameLen(u32) + name + targetGuid(u64)
            uint32_t nameLen = packet.readUInt32();
            if (nameLen > 0 && nameLen < 256) {
                data.senderName.resize(nameLen);
                for (uint32_t i = 0; i < nameLen; ++i) {
                    data.senderName[i] = static_cast<char>(packet.readUInt8());
                }
                // Remove null terminator if present
                if (!data.senderName.empty() && data.senderName.back() == '\0') {
                    data.senderName.pop_back();
                }
            }
            data.receiverGuid = packet.readUInt64();
            break;
        }

        case ChatType::CHANNEL: {
            // channelName(string) + rank(u32) + senderGuid(u64)
            data.channelName = packet.readString();
            /*uint32_t rank =*/ packet.readUInt32();
            data.senderGuid = packet.readUInt64();
            break;
        }

        default: {
            // Most types: senderGuid(u64)
            data.senderGuid = packet.readUInt64();
            break;
        }
    }

    // Classic SAY/YELL/PARTY/GUILD/etc: NO second GUID before message.
    // Only WHISPER_INFORM has a receiver GUID (same as sender for echo).
    // Do NOT read an extra uint64 here — it would consume messageLen + message bytes.

    // Read message length
    uint32_t messageLen = packet.readUInt32();

    // Read message
    if (messageLen > 0 && messageLen < 8192) {
        data.message.resize(messageLen);
        for (uint32_t i = 0; i < messageLen; ++i) {
            data.message[i] = static_cast<char>(packet.readUInt8());
        }
        // Remove null terminator if present
        if (!data.message.empty() && data.message.back() == '\0') {
            data.message.pop_back();
        }
    }

    // Read chat tag
    if (packet.getReadPos() < packet.getSize()) {
        data.chatTag = packet.readUInt8();
    }

    LOG_INFO("[Classic] Parsed SMSG_MESSAGECHAT:");
    LOG_INFO("  Type: ", getChatTypeString(data.type));
    LOG_INFO("  Sender GUID: 0x", std::hex, data.senderGuid, std::dec);
    if (!data.senderName.empty()) {
        LOG_INFO("  Sender name: ", data.senderName);
    }
    if (!data.channelName.empty()) {
        LOG_INFO("  Channel: ", data.channelName);
    }
    LOG_INFO("  Message: ", data.message);

    return true;
}

// ============================================================================
// Classic CMSG_JOIN_CHANNEL / CMSG_LEAVE_CHANNEL
// Classic format: just string channelName + string password (no channelId/hasVoice/joinedByZone)
// ============================================================================

network::Packet ClassicPacketParsers::buildJoinChannel(const std::string& channelName, const std::string& password) {
    network::Packet packet(wireOpcode(Opcode::CMSG_JOIN_CHANNEL));
    packet.writeString(channelName);
    packet.writeString(password);
    LOG_DEBUG("[Classic] Built CMSG_JOIN_CHANNEL: channel=", channelName);
    return packet;
}

network::Packet ClassicPacketParsers::buildLeaveChannel(const std::string& channelName) {
    network::Packet packet(wireOpcode(Opcode::CMSG_LEAVE_CHANNEL));
    packet.writeString(channelName);
    LOG_DEBUG("[Classic] Built CMSG_LEAVE_CHANNEL: channel=", channelName);
    return packet;
}

// ============================================================================
// Classic guild roster parser
// Differences from WotLK:
// - No rankCount field (fixed 10 ranks, read rights only)
// - No per-rank bank tab data
// - No gender byte per member
// ============================================================================

bool ClassicPacketParsers::parseGuildRoster(network::Packet& packet, GuildRosterData& data) {
    if (packet.getSize() < 4) {
        LOG_ERROR("Classic SMSG_GUILD_ROSTER too small: ", packet.getSize());
        return false;
    }
    uint32_t numMembers = packet.readUInt32();
    data.motd = packet.readString();
    data.guildInfo = packet.readString();

    // Classic: fixed 10 ranks, just uint32 rights each (no goldLimit, no bank tabs)
    data.ranks.resize(10);
    for (int i = 0; i < 10; ++i) {
        data.ranks[i].rights = packet.readUInt32();
        data.ranks[i].goldLimit = 0;
    }

    data.members.resize(numMembers);
    for (uint32_t i = 0; i < numMembers; ++i) {
        auto& m = data.members[i];
        m.guid = packet.readUInt64();
        m.online = (packet.readUInt8() != 0);
        m.name = packet.readString();
        m.rankIndex = packet.readUInt32();
        m.level = packet.readUInt8();
        m.classId = packet.readUInt8();
        // Classic: NO gender byte
        m.gender = 0;
        m.zoneId = packet.readUInt32();
        if (!m.online) {
            m.lastOnline = packet.readFloat();
        }
        m.publicNote = packet.readString();
        m.officerNote = packet.readString();
    }
    LOG_INFO("Parsed Classic SMSG_GUILD_ROSTER: ", numMembers, " members");
    return true;
}

// ============================================================================
// Classic guild query response parser
// Differences from WotLK:
// - No trailing rankCount uint32
// ============================================================================

bool ClassicPacketParsers::parseGuildQueryResponse(network::Packet& packet, GuildQueryResponseData& data) {
    if (packet.getSize() < 8) {
        LOG_ERROR("Classic SMSG_GUILD_QUERY_RESPONSE too small: ", packet.getSize());
        return false;
    }
    data.guildId = packet.readUInt32();
    data.guildName = packet.readString();
    for (int i = 0; i < 10; ++i) {
        data.rankNames[i] = packet.readString();
    }
    data.emblemStyle = packet.readUInt32();
    data.emblemColor = packet.readUInt32();
    data.borderStyle = packet.readUInt32();
    data.borderColor = packet.readUInt32();
    data.backgroundColor = packet.readUInt32();
    // Classic: NO trailing rankCount
    data.rankCount = 10;
    LOG_INFO("Parsed Classic SMSG_GUILD_QUERY_RESPONSE: guild=", data.guildName);
    return true;
}

// ============================================================================
// GameObject Query — Classic has no extra strings before data[]
// WotLK has iconName + castBarCaption + unk1 between names and data[].
// Vanilla: entry, type, displayId, name[4], data[24]
// ============================================================================

bool ClassicPacketParsers::parseGameObjectQueryResponse(network::Packet& packet, GameObjectQueryResponseData& data) {
    data.entry = packet.readUInt32();

    // High bit set means gameobject not found
    if (data.entry & 0x80000000) {
        data.entry &= ~0x80000000;
        data.name = "";
        return true;
    }

    data.type = packet.readUInt32();
    data.displayId = packet.readUInt32();
    // 4 name strings
    data.name = packet.readString();
    packet.readString();
    packet.readString();
    packet.readString();

    // Classic: data[24] comes immediately after names (no extra strings)
    size_t remaining = packet.getSize() - packet.getReadPos();
    if (remaining >= 24 * 4) {
        for (int i = 0; i < 24; i++) {
            data.data[i] = packet.readUInt32();
        }
        data.hasData = true;
    }

    if (data.type == 15) { // MO_TRANSPORT
        LOG_INFO("Classic GO query: MO_TRANSPORT entry=", data.entry,
                 " name=\"", data.name, "\" displayId=", data.displayId,
                 " taxiPathId=", data.data[0], " moveSpeed=", data.data[1]);
    } else {
        LOG_DEBUG("Classic GO query: ", data.name, " type=", data.type, " entry=", data.entry);
    }
    return true;
}

// ============================================================================
// Gossip — Classic has no menuId, and quest items lack questFlags + isRepeatable
// ============================================================================

bool ClassicPacketParsers::parseGossipMessage(network::Packet& packet, GossipMessageData& data) {
    size_t remaining = packet.getSize() - packet.getReadPos();
    if (remaining < 8 + 4 + 4) {
        LOG_ERROR("Classic SMSG_GOSSIP_MESSAGE too small: ", remaining, " bytes");
        return false;
    }

    data.npcGuid = packet.readUInt64();
    // Classic: NO menuId field (WotLK adds uint32 menuId here)
    data.menuId = 0;
    data.titleTextId = packet.readUInt32();
    uint32_t optionCount = packet.readUInt32();

    data.options.clear();
    data.options.reserve(optionCount);
    for (uint32_t i = 0; i < optionCount; ++i) {
        GossipOption opt;
        opt.id = packet.readUInt32();
        opt.icon = packet.readUInt8();
        opt.isCoded = (packet.readUInt8() != 0);
        opt.boxMoney = packet.readUInt32();
        opt.text = packet.readString();
        opt.boxText = packet.readString();
        data.options.push_back(opt);
    }

    uint32_t questCount = packet.readUInt32();
    data.quests.clear();
    data.quests.reserve(questCount);
    for (uint32_t i = 0; i < questCount; ++i) {
        GossipQuestItem quest;
        quest.questId = packet.readUInt32();
        quest.questIcon = packet.readUInt32();
        quest.questLevel = static_cast<int32_t>(packet.readUInt32());
        // Classic: NO questFlags, NO isRepeatable
        quest.questFlags = 0;
        quest.isRepeatable = 0;
        quest.title = packet.readString();
        data.quests.push_back(quest);
    }

    LOG_INFO("Classic Gossip: ", optionCount, " options, ", questCount, " quests");
    return true;
}

} // namespace game
} // namespace wowee
