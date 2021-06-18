//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#pragma once

#include <vector>

#include "AirPods.h"


// AppleCP = Apple Continuity Protocols
//
namespace Core::AppleCP
{
#pragma pack(push)
#pragma pack(1)
    enum class PacketType : uint8_t
    {
        AirPrint = 0x3,
        AirDrop = 0x5,
        HomeKit = 0x6,
        ProximityPairing = 0x7,
        HeySiri = 0x8,
        AirPlay = 0x9,
        MagicSwitch = 0xB,
        Handoff = 0xC,
        InstantHotspotTetheringTargetPresence = 0xD,
        InstantHotspotTetheringSourcePresence = 0xE,
        NearbyAction = 0xF,
        NearbyInfo = 0x10,
    };

    enum class Color : uint8_t
    {
        White = 0x0,
        Black = 0x1,
        Red = 0x2,
        Blue = 0x3,
        Pink = 0x4,
        Gray = 0x5,
        Silver = 0x6,
        Gold = 0x7,
        RoseGold = 0x8,
        SpaceGray = 0x9,
        DarkBlue = 0xA,
        LightBlue = 0xB,
        Yellow = 0xC,
    };

    struct Header
    {
        PacketType packetType;
        uint8_t remainingLength;                // Remaining length of this packet
    };


    // About "Flipped":
    // 
    //      In other similar projects, you may see a variable called "IsFlipped".
    // 
    //      When this variable is true, you need to return the right earphone data for the left
    //      earphone, and vice versa.
    // 
    //      In my guess, there is no distinction between left and right earphones in the protocol
    //      structure, and the left and right earphones use almost the same program.
    // 
    //      So there are no fields named "left" and "right", but "current" and "another" fields
    //      instead.
    // 
    // 
    // About 2 discoverable devices for 1 AirPods:
    // 
    //      So, if you've researched the AirPods protocol, you may have noticed a rather strange
    //      fact.
    // 
    //      That is, the only AirPods you have sometimes have two discoverable devices, and
    //      sometimes only one discoverable device.
    // 
    //      This is because AirPods currently have two Bluetooth modules in two earphones and none
    //      in the case.
    // 
    //      Perhaps for power saving and the battery balancing feature of the two earphones (I'm not
    //      sure if these are the reasons), when both earphones are working or charging at the same
    //      time, only the Bluetooth device in one of the earphones is discoverable, and when only
    //      one earphone is working and the other is charging (lid opened), the Bluetooth device in
    //      both earphones is made discoverable, and the battery of the case is sent and synced.
    //
    class AirPods : Header
    {
    public:
        static bool IsValid(const std::vector<uint8_t> &data);

        Core::AirPods::Side GetBroadcastedSide() const;
        bool IsLeftBroadcasted() const;
        bool IsRightBroadcasted() const;

        Core::AirPods::Model GetModel() const;

        Core::AirPods::Battery GetLeftBattery() const;
        Core::AirPods::Battery GetRightBattery() const;
        Core::AirPods::Battery GetCaseBattery() const;

        bool IsLeftCharging() const;
        bool IsRightCharging() const;
        bool IsCaseCharging() const;

        bool IsBothPodsInCase() const;
        bool IsLidOpened() const;

        bool IsLeftInEar() const;
        bool IsRightInEar() const;

        AirPods Desensitize() const;

    private:
        uint8_t unk1[1];
        uint16_t modelId;
        struct {
            uint8_t unk4 : 1;
            uint8_t currInEar : 1;
            uint8_t bothInCase : 1;
            uint8_t anotInEar : 1;
            uint8_t unk6 : 1;
            uint8_t broadcastFrom : 1;  // This advertisement is broadcast from which earphone.
            uint8_t unk7 : 1;
            uint8_t unk8 : 1;
        };
        struct {
            uint8_t curr : 4;           // Battery remaining [0, 10], otherwise unavailable
            uint8_t anot : 4;           // Battery remaining [0, 10], otherwise unavailable
            uint8_t caseBox : 4;        // Battery remaining [0, 10], otherwise unavailable
            uint8_t currCharging : 1;   // If it's charging (value != 0)
            uint8_t anotCharging : 1;   // If it's charging (value != 0)
            uint8_t caseCharging : 1;   // If it's charging (value != 0)
            uint8_t unk9 : 1;
        } battery;
        uint8_t lidState;               // 1/2/3/4/5/6/7/0 == opened, 9/a/b/c/d/e/f/8 == closed
        Color color;                    // Untested because I don't have a device other than white
        uint8_t unk11[1];
        uint8_t unk12[16];              // Hash or encrypted payload
    };
    static_assert(sizeof(AirPods) == 27);
#pragma pack(pop)

    template <class T>
    constexpr inline bool IsACPStruct = std::is_base_of_v<Header, T>;

    template <class T, std::enable_if_t<IsACPStruct<T>, int> = 0>
    std::optional<T> As(const std::vector<uint8_t> &data)
    {
        if (!T::IsValid(data)) {
            return std::nullopt;
        }

        return *(T*)data.data();
    }

} // namespace Core::AppleCP
