#include "PlayerHousingMgr.h"

#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Map.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Weather.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using namespace Housing;

// Island ambience: each island's own weather, time of day and music. All three are sent to
// the players on the island only (every island is the same zone), when they arrive, when
// the owner changes them, and now and then while they stay. Leaving puts back the real
// clock and the weather where they land.

namespace
{
    // Weather, time and music go to everyone on the island: a quarter second apart at most
    // (fast enough to step through the choices).
    constexpr uint32 AMBIENCE_COOLDOWN_MS = 250;
    struct WeatherChoice
    {
        char const* name;
        WeatherState state;
        float grade;
    };

    WeatherChoice const WEATHERS[] =
    {
        { "clear", WEATHER_STATE_FINE, 0.0f },
        { "fog", WEATHER_STATE_FOG, 0.6f },
        { "light rain", WEATHER_STATE_LIGHT_RAIN, 0.3f },
        { "rain", WEATHER_STATE_MEDIUM_RAIN, 0.6f },
        { "thunderstorm", WEATHER_STATE_THUNDERS, 0.9f },
        { "light snow", WEATHER_STATE_LIGHT_SNOW, 0.3f },
        { "snow", WEATHER_STATE_MEDIUM_SNOW, 0.6f },
        { "blizzard", WEATHER_STATE_HEAVY_SNOW, 0.9f },
        { "sandstorm", WEATHER_STATE_MEDIUM_SANDSTORM, 0.6f },
    };

    struct TimeChoice
    {
        char const* name;
        int32 minutes;  // after midnight; -1 follows the server's clock
    };

    TimeChoice const TIMES[] =
    {
        { "the server's time", -1 },
        { "dawn", 6 * 60 },
        { "midday", 12 * 60 },
        { "dusk", 19 * 60 + 30 },
        { "night", 0 },
    };

    // Zone music from SoundEntries.dbc.
    std::vector<std::pair<uint32, char const*>> const TRACKS =
    {
        { 2532, "Stormwind" }, { 7319, "Ironforge" }, { 3920, "Darnassus" }, { 2901, "Orgrimmar" },
        { 7077, "Thunder Bluff" }, { 9793, "Silvermoon" }, { 14906, "Dalaran" }, { 10608, "Shattrath" },
        { 4516, "Alliance tavern" }, { 5234, "Horde tavern" }, { 11806, "Dwarven tavern" }, { 11805, "Pirate tavern" },
        { 12137, "Undercity tavern" }, { 11810, "Brewfest" }, { 12154, "Karazhan" }, { 9012, "Nagrand" },
        { 9149, "Zangarmarsh" }, { 12800, "Howling Fjord" }, { 12816, "Grizzly Hills" }, { 13799, "The Storm Peaks" },
        { 14893, "Sholazar Basin" },
    };

    constexpr uint32 CLOCK_RESEND_MS = 15 * MINUTE * IN_MILLISECONDS;  // the client's clock keeps running
    constexpr uint32 MUSIC_REPLAY_MS = 4 * MINUTE * IN_MILLISECONDS;   // a track plays once

    void SendClock(Player* player, int32 minutes)
    {
        time_t now = GameTime::GetGameTime().count();
        std::tm lt = Acore::Time::TimeBreakdown(now);
        if (minutes >= 0)
        {
            lt.tm_hour = minutes / 60;
            lt.tm_min = minutes % 60;
        }

        // As ByteBuffer::AppendPackedTime packs it.
        uint32 packed = uint32((lt.tm_year - 100) << 24 | lt.tm_mon << 20 | (lt.tm_mday - 1) << 14 | lt.tm_wday << 11 | lt.tm_hour << 6 | lt.tm_min);
        WorldPacket data(SMSG_LOGIN_SETTIMESPEED, 4 + 4 + 4);
        data << uint32(packed);
        data << float(0.01666667f);
        data << uint32(0);
        player->SendDirectMessage(&data);
    }

    void SendWeather(Player* player, uint8 weather)
    {
        WeatherChoice const& choice = WEATHERS[weather < std::size(WEATHERS) ? weather : 0];
        WorldPackets::Misc::Weather packet(choice.state, choice.grade);
        player->SendDirectMessage(packet.Write());
    }
}

uint8 PlayerHousingMgr::WeatherCount()
{
    return uint8(std::size(WEATHERS));
}

char const* PlayerHousingMgr::WeatherName(uint8 weather)
{
    return WEATHERS[weather < std::size(WEATHERS) ? weather : 0].name;
}

uint8 PlayerHousingMgr::TimeOfDayCount()
{
    return uint8(std::size(TIMES));
}

char const* PlayerHousingMgr::TimeOfDayName(uint8 timeOfDay)
{
    return TIMES[timeOfDay < std::size(TIMES) ? timeOfDay : 0].name;
}

std::vector<std::pair<uint32, char const*>> const& PlayerHousingMgr::MusicTracks()
{
    return TRACKS;
}

char const* PlayerHousingMgr::MusicName(uint32 soundId)
{
    for (auto const& [id, name] : TRACKS)
        if (id == soundId)
            return name;
    return nullptr;
}

bool PlayerHousingMgr::HasMusicBox(ObjectGuid::LowType ownerGuid) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto sessionItr = _sessionsByOwner.find(ownerGuid);
    if (sessionItr == _sessionsByOwner.end())
        return false;

    for (auto const& [id, placement] : sessionItr->second.placements)
    {
        auto pieceItr = _pieces.find(placement.itemEntry);
        if (pieceItr != _pieces.end() && pieceItr->second.HasFlag(PIECE_FLAG_MUSIC))
            return true;
    }
    return false;
}

void PlayerHousingMgr::SendAmbience(Player* player, HouseRecord const& house, bool withMusic)
{
    SendWeather(player, house.weather);
    SendClock(player, TIMES[house.timeOfDay < std::size(TIMES) ? house.timeOfDay : 0].minutes);
    if (withMusic && house.music && MusicName(house.music) && HasMusicBox(house.ownerGuid))
        player->SendPlayMusic(house.music, true);

    std::lock_guard<std::recursive_mutex> guard(_lock);
    _ambienceTimers[player->GetGUID()] = AmbienceTimers{ CLOCK_RESEND_MS, MUSIC_REPLAY_MS };
}

void PlayerHousingMgr::RestoreAmbience(Player* player)
{
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (!_ambienceTimers.erase(player->GetGUID()))
            return;
    }

    // The real clock, and the weather of wherever the player is now.
    SendClock(player, -1);
    Weather::SendFineWeatherUpdateToPlayer(player);
    if (player->IsInWorld())
        player->GetMap()->SendZoneWeather(player->GetZoneId(), player);
}

void PlayerHousingMgr::UpdateAmbience(Player* player, uint32 diffMs)
{
    bool clockDue = false;
    bool musicDue = false;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        auto itr = _ambienceTimers.find(player->GetGUID());
        if (itr == _ambienceTimers.end())
            return;

        AmbienceTimers& timers = itr->second;
        clockDue = timers.clockMs <= diffMs;
        musicDue = timers.musicMs <= diffMs;
        timers.clockMs = clockDue ? CLOCK_RESEND_MS : timers.clockMs - diffMs;
        timers.musicMs = musicDue ? MUSIC_REPLAY_MS : timers.musicMs - diffMs;
    }
    if (!clockDue && !musicDue)
        return;

    ObjectGuid::LowType owner = GetIslandOwner(player);
    HouseRecord house;
    if (!owner || !GetHouseRecord(owner, house))
        return;

    if (clockDue && TIMES[house.timeOfDay < std::size(TIMES) ? house.timeOfDay : 0].minutes >= 0)
        SendClock(player, TIMES[house.timeOfDay].minutes);
    if (musicDue && house.music && MusicName(house.music) && HasMusicBox(owner))
        player->SendPlayMusic(house.music, true);
}

void PlayerHousingMgr::ApplyAmbienceToIsland(ObjectGuid::LowType ownerGuid, bool withMusic)
{
    HouseRecord house;
    if (!GetHouseRecord(ownerGuid, house))
        return;

    std::vector<ObjectGuid> occupants;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        auto sessionItr = _sessionsByOwner.find(ownerGuid);
        if (sessionItr == _sessionsByOwner.end())
            return;
        occupants.assign(sessionItr->second.occupants.begin(), sessionItr->second.occupants.end());
    }

    for (ObjectGuid const& guid : occupants)
        if (Player* occupant = ObjectAccessor::FindPlayer(guid))
            SendAmbience(occupant, house, withMusic);
}

bool PlayerHousingMgr::SetWeather(Player* player, uint8 weather, std::string& reason)
{
    // Everyone on the island gets each change: one a second.
    if (OnCooldown(player, COOLDOWN_AMBIENCE, AMBIENCE_COOLDOWN_MS, reason))
        return false;
    if (weather >= WeatherCount())
    {
        reason = "Unknown weather.";
        return false;
    }

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    EnsureHouse(self);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET weather={} WHERE owner_guid={}", uint32(weather), self);
    ApplyAmbienceToIsland(self, false);
    reason = Acore::StringFormat("Your island's weather: {}.", WeatherName(weather));
    return true;
}

bool PlayerHousingMgr::SetTimeOfDay(Player* player, uint8 timeOfDay, std::string& reason)
{
    // Everyone on the island gets each change: one a second.
    if (OnCooldown(player, COOLDOWN_AMBIENCE, AMBIENCE_COOLDOWN_MS, reason))
        return false;
    if (timeOfDay >= TimeOfDayCount())
    {
        reason = "Unknown time of day.";
        return false;
    }

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    EnsureHouse(self);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET time_of_day={} WHERE owner_guid={}", uint32(timeOfDay), self);
    ApplyAmbienceToIsland(self, false);
    reason = timeOfDay ? Acore::StringFormat("It's always {} on your island now.", TimeOfDayName(timeOfDay))
                       : std::string("Your island follows the server's clock again.");
    return true;
}

bool PlayerHousingMgr::SetMusic(Player* player, uint32 soundId, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    if (OnCooldown(player, COOLDOWN_AMBIENCE, AMBIENCE_COOLDOWN_MS, reason))
        return false;
    if (soundId && !MusicName(soundId))
    {
        reason = "The music box doesn't know that tune.";
        return false;
    }
    if (soundId && !HasMusicBox(self))
    {
        reason = "Place a Music Box first.";
        return false;
    }

    EnsureHouse(self);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET music={} WHERE owner_guid={}", soundId, self);
    ApplyAmbienceToIsland(self, true);
    reason = soundId ? Acore::StringFormat("The music box plays {}.", MusicName(soundId))
                     : std::string("The music box falls silent (the current tune plays out).");
    return true;
}
