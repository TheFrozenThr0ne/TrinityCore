#include "step_counter.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Configuration/Config.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <ctime>

using namespace Trinity::ChatCommands;

StepCounter* StepCounter::_instance = nullptr;

namespace StepCounterConfig
{
    /*
     * ---------------------------------------------------------
     * Movement
     * ---------------------------------------------------------
     */

    constexpr float DISTANCE_PER_STEP = 2.5f;

    constexpr uint32 UPDATE_INTERVAL = 500;

    constexpr float MAX_DISTANCE_PER_UPDATE = 50.0f;

    /*
     * ---------------------------------------------------------
     * Behaviour
     * ---------------------------------------------------------
     */

    constexpr bool COUNT_MOUNTED = true;
    constexpr bool COUNT_VEHICLES = true;

    /*
     * ---------------------------------------------------------
     * Ranking
     * ---------------------------------------------------------
     */

    constexpr uint32 TOP_LIMIT = 10;
}

/*
 * =============================================================
 * Constructor
 * =============================================================
 */

StepCounter::StepCounter()
    : PlayerScript("step_counter")
{
    _instance = this;
}

StepCounter* StepCounter::Instance()
{
    return _instance;
}

/*
 * =============================================================
 * Player data
 * =============================================================
 */

StepCounter::PlayerData& StepCounter::GetData(Player* player)
{
    return _players[player->GetGUID()];
}

StepCounter::PlayerData const* StepCounter::FindData(Player* player) const
{
    auto itr = _players.find(player->GetGUID());

    if (itr == _players.end())
        return nullptr;

    return &itr->second;
}

/*
 * =============================================================
 * Date helpers
 * =============================================================
 */

uint32 StepCounter::GetCurrentDayKey() const
{
    std::time_t now = std::time(nullptr);

    std::tm localTime{};

#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    return
        static_cast<uint32>(localTime.tm_year + 1900) * 10000 +
        static_cast<uint32>(localTime.tm_mon + 1) * 100 +
        static_cast<uint32>(localTime.tm_mday);
}

uint32 StepCounter::GetCurrentWeekKey() const
{
    std::time_t now = std::time(nullptr);

    std::tm localTime{};

#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    /*
     * Monday = first day of week.
     */
    int weekday = localTime.tm_wday;

    if (weekday == 0)
        weekday = 7;

    /*
     * Move to Thursday.
     */
    std::tm thursday = localTime;

    thursday.tm_mday += 4 - weekday;

    std::mktime(&thursday);

    int year = thursday.tm_year + 1900;

    /*
     * Find ISO week number.
     */
    std::tm jan4{};

    jan4.tm_year = year - 1900;
    jan4.tm_mon = 0;
    jan4.tm_mday = 4;

    std::mktime(&jan4);

    int jan4Weekday = jan4.tm_wday;

    if (jan4Weekday == 0)
        jan4Weekday = 7;

    int days =
        static_cast<int>(
            std::difftime(
                std::mktime(&thursday),
                std::mktime(&jan4))
            / (60 * 60 * 24));

    int week =
        (days + jan4Weekday - 1) / 7 + 1;

    return
        static_cast<uint32>(year) * 100 +
        static_cast<uint32>(week);
}


/*
 * =============================================================
 * Movement
 * =============================================================
 */

void StepCounter::ResetMovement(Player* player)
{
    PlayerData& data = GetData(player);

    data.lastX = player->GetPositionX();
    data.lastY = player->GetPositionY();
    data.lastZ = player->GetPositionZ();

    data.accumulatedDistance = 0.0f;

    data.updateTimer =
        StepCounterConfig::UPDATE_INTERVAL;

    data.initialized = true;
}

/*
 * =============================================================
 * Load
 * =============================================================
 */

void StepCounter::Load(Player* player)
{
    PlayerData& data = GetData(player);

    uint32 currentDay = GetCurrentDayKey();
    uint32 currentWeek = GetCurrentWeekKey();

    if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT total_steps, daily_steps, weekly_steps, "
            "day_key, week_key "
            "FROM character_step_counter "
            "WHERE guid = {}",
            player->GetGUID().GetCounter()))
    {
        Field* fields = result->Fetch();

        data.totalSteps = fields[0].GetUInt64();
        data.dailySteps = fields[1].GetUInt64();
        data.weeklySteps = fields[2].GetUInt64();

        data.dayKey = fields[3].GetUInt32();
        data.weekKey = fields[4].GetUInt32();
    }
    else
    {
        data.totalSteps = 0;
        data.dailySteps = 0;
        data.weeklySteps = 0;

        data.dayKey = currentDay;
        data.weekKey = currentWeek;

        CharacterDatabase.PExecute(
            "INSERT INTO character_step_counter "
            "(guid, total_steps, daily_steps, weekly_steps, day_key, week_key) "
            "VALUES ({}, 0, 0, 0, {}, {})",
            player->GetGUID().GetCounter(),
            currentDay,
            currentWeek);
    }

    CheckPeriod(player, data);

    data.dirty = false;

    ResetMovement(player);
}

/*
 * =============================================================
 * Save
 * =============================================================
 */

void StepCounter::Save(Player* player)
{
    auto itr = _players.find(player->GetGUID());

    if (itr == _players.end())
        return;

    PlayerData& data = itr->second;

    if (!data.dirty)
        return;

    CharacterDatabase.PExecute(
        "INSERT INTO character_step_counter "
        "(guid, total_steps, daily_steps, weekly_steps, day_key, week_key) "
        "VALUES ({}, {}, {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "total_steps = VALUES(total_steps), "
        "daily_steps = VALUES(daily_steps), "
        "weekly_steps = VALUES(weekly_steps), "
        "day_key = VALUES(day_key), "
        "week_key = VALUES(week_key)",
        player->GetGUID().GetCounter(),
        data.totalSteps,
        data.dailySteps,
        data.weeklySteps,
        data.dayKey,
        data.weekKey);

    data.dirty = false;
}

/*
 * =============================================================
 * Period handling
 * =============================================================
 */

void StepCounter::CheckPeriod(Player* player, PlayerData& data)
{
    uint32 currentDay = GetCurrentDayKey();
    uint32 currentWeek = GetCurrentWeekKey();

    if (data.dayKey != currentDay)
    {
        data.dayKey = currentDay;
        data.dailySteps = 0;

        data.dirty = true;
    }

    if (data.weekKey != currentWeek)
    {
        data.weekKey = currentWeek;
        data.weeklySteps = 0;

        data.dirty = true;
    }

    (void)player;
}

/*
 * =============================================================
 * Add steps
 * =============================================================
 */

void StepCounter::AddSteps(Player* player, uint32 steps)
{
    if (!steps)
        return;

    PlayerData& data = GetData(player);

    CheckPeriod(player, data);

    data.totalSteps += steps;
    data.dailySteps += steps;
    data.weeklySteps += steps;

    data.dirty = true;

    /*
     * Check only after steps have actually been added.
     */
    CheckRewards(player);
}

/*
 * =============================================================
 * Reward loading
 * =============================================================
 */

void StepCounter::LoadRewards()
{
    if (_rewardsLoaded)
        return;

    _rewardsLoaded = true;

    _rewards.clear();

    if (QueryResult result = CharacterDatabase.Query(
            "SELECT id, period_type, reward_type, required_steps, "
            "item_entry, amount, message "
            "FROM step_counter_rewards "
            "WHERE enabled = 1 "
            "ORDER BY required_steps ASC, id ASC"))
    {
        do
        {
            Field* fields = result->Fetch();

            Reward reward;

            reward.id = fields[0].GetUInt32();

            std::string period =
                fields[1].GetString();

            std::string type =
                fields[2].GetString();

            reward.requiredSteps =
                fields[3].GetUInt64();

            reward.itemEntry =
                fields[4].GetUInt32();

            reward.amount =
                fields[5].GetUInt32();

            reward.message =
                fields[6].GetString();

            if (period == "daily")
            {
                reward.period =
                    RewardPeriod::Daily;
            }
            else if (period == "weekly")
            {
                reward.period =
                    RewardPeriod::Weekly;
            }
            else if (period == "total")
            {
                reward.period =
                    RewardPeriod::Total;
            }
            else
            {
                continue;
            }

            if (type == "gold")
            {
                reward.type =
                    RewardType::Gold;
            }
            else if (type == "item")
            {
                reward.type =
                    RewardType::Item;
            }
            else
            {
                continue;
            }

            _rewards.push_back(
                std::move(reward));

        } while (result->NextRow());
    }
}

/*
 * =============================================================
 * Reward helpers
 * =============================================================
 */

std::string StepCounter::GetPeriodName(
    RewardPeriod period) const
{
    switch (period)
    {
        case RewardPeriod::Daily:
            return "daily";

        case RewardPeriod::Weekly:
            return "weekly";

        case RewardPeriod::Total:
            return "total";
    }

    return "unknown";
}

bool StepCounter::HasClaimedReward(
    Player* player,
    Reward const& reward,
    uint32 periodKey) const
{
    uint32 guid =
        player->GetGUID().GetCounter();

    if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_step_counter_rewards "
            "WHERE guid = {} "
            "AND reward_id = {} "
            "AND period_key = {} "
            "LIMIT 1",
            guid,
            reward.id,
            periodKey))
    {
        return true;
    }

    return false;
}

void StepCounter::MarkRewardClaimed(
    Player* player,
    Reward const& reward,
    uint32 periodKey)
{
    CharacterDatabase.PExecute(
        "INSERT IGNORE INTO character_step_counter_rewards "
        "(guid, reward_id, period_key) "
        "VALUES ({}, {}, {})",
        player->GetGUID().GetCounter(),
        reward.id,
        periodKey);
}

/*
 * =============================================================
 * Give reward
 * =============================================================
 */

bool StepCounter::GiveReward(
    Player* player,
    Reward const& reward)
{
    if (!reward.amount)
        return false;

    if (reward.type == RewardType::Gold)
    {
        constexpr uint64 COPPER_PER_GOLD = 10000;

        uint64 copper =
            static_cast<uint64>(reward.amount) *
            COPPER_PER_GOLD;

        if (copper >
            static_cast<uint64>(INT64_MAX))
        {
            return false;
        }

        if (!player->ModifyMoney(
                static_cast<int64>(copper)))
        {
            return false;
        }

        return true;
    }

    if (reward.type == RewardType::Item)
    {
        if (!reward.itemEntry)
            return false;

        if (!player->AddItem(
                reward.itemEntry,
                reward.amount))
        {
            player->GetSession()->SendNotification(
                "Deine Schrittbelohnung konnte nicht ins Inventar gelegt werden.");

            return false;
        }

        return true;
    }

    return false;
}

/*
 * =============================================================
 * Reward checking
 * =============================================================
 */

void StepCounter::CheckRewards(Player* player)
{
    LoadRewards();

    PlayerData& data =
        GetData(player);

    uint32 dayKey =
        GetCurrentDayKey();

    uint32 weekKey =
        GetCurrentWeekKey();

    for (Reward const& reward : _rewards)
    {
        uint64 currentSteps = 0;
        uint32 periodKey = 0;

        switch (reward.period)
        {
            case RewardPeriod::Daily:
                currentSteps =
                    data.dailySteps;
                periodKey =
                    dayKey;
                break;

            case RewardPeriod::Weekly:
                currentSteps =
                    data.weeklySteps;
                periodKey =
                    weekKey;
                break;

            case RewardPeriod::Total:
                currentSteps =
                    data.totalSteps;
                periodKey =
                    0;
                break;
        }

        if (currentSteps <
            reward.requiredSteps)
        {
            continue;
        }

        if (HasClaimedReward(
                player,
                reward,
                periodKey))
        {
            continue;
        }

        if (!GiveReward(
                player,
                reward))
        {
            continue;
        }

        MarkRewardClaimed(
            player,
            reward,
            periodKey);

            if (!reward.message.empty())
            {
                player->GetSession()->SendNotification(
                    "%s",
                    reward.message.c_str());
            }
            else
            {
                player->GetSession()->SendNotification(
                    "Schrittziel erreicht: %llu Schritte!",
                    static_cast<unsigned long long>(
                        reward.requiredSteps));
            }
    }
}

/*
 * =============================================================
 * Login / Logout / Save
 * =============================================================
 */

void StepCounter::OnLogin(
    Player* player,
    bool /*firstLogin*/)
{
    LoadRewards();
    Load(player);

    /*
     * =============================================================
     * Welcoming StepCounter Module
     * =============================================================
    */

    if (sConfigMgr->GetBoolDefault("StepCounter.Announce", true))
    {
        ChatHandler(player->GetSession()).SendSysMessage("|cff00ff00[StepCounter]|r Welcome! Module is running.");
    }
}

void StepCounter::OnLogout(
    Player* player)
{
    Save(player);

    _players.erase(
        player->GetGUID());
}

void StepCounter::OnSave(
    Player* player)
{
    Save(player);
}

void StepCounter::OnMapChanged(
    Player* player)
{
    ResetMovement(player);
}

/*
 * =============================================================
 * Movement update
 * =============================================================
 */

void StepCounter::OnUpdate(
    Player* player,
    uint32 diff)
{
    auto itr =
        _players.find(player->GetGUID());

    if (itr == _players.end())
        return;

    PlayerData& data =
        itr->second;

    if (!data.initialized)
        return;

    CheckPeriod(player, data);

    if (data.updateTimer > diff)
    {
        data.updateTimer -= diff;
        return;
    }

    data.updateTimer =
        StepCounterConfig::UPDATE_INTERVAL;

    /*
     * Do not count dead players.
     */
    if (!player->IsAlive())
    {
        ResetMovement(player);
        return;
    }

    /*
     * Do not count teleport movement.
     */
    if (player->IsBeingTeleported())
    {
        ResetMovement(player);
        return;
    }

    /*
     * Mounts.
     */
    if constexpr (!StepCounterConfig::COUNT_MOUNTED)
    {
        if (player->IsMounted())
        {
            ResetMovement(player);
            return;
        }
    }

    /*
     * Vehicles.
     */
    if constexpr (!StepCounterConfig::COUNT_VEHICLES)
    {
        if (player->GetVehicle())
        {
            ResetMovement(player);
            return;
        }
    }

    float x =
        player->GetPositionX();

    float y =
        player->GetPositionY();

    float z =
        player->GetPositionZ();

    float dx =
        x - data.lastX;

    float dy =
        y - data.lastY;

    float dz =
        z - data.lastZ;

    float distance =
        std::sqrt(
            dx * dx +
            dy * dy +
            dz * dz);

    data.lastX = x;
    data.lastY = y;
    data.lastZ = z;

    /*
     * Teleport / huge movement.
     */
    if (distance >
        StepCounterConfig::MAX_DISTANCE_PER_UPDATE)
    {
        data.accumulatedDistance = 0.0f;
        return;
    }

    if (distance <= 0.0f)
        return;

    data.accumulatedDistance +=
        distance;

    uint32 newSteps =
        static_cast<uint32>(
            data.accumulatedDistance /
            StepCounterConfig::DISTANCE_PER_STEP);

    if (!newSteps)
        return;

    data.accumulatedDistance -=
        static_cast<float>(newSteps) *
        StepCounterConfig::DISTANCE_PER_STEP;

    AddSteps(
        player,
        newSteps);
}

/*
 * =============================================================
 * Public API
 * =============================================================
 */

uint64 StepCounter::GetTotalSteps(
    Player* player) const
{
    PlayerData const* data =
        FindData(player);

    return data ?
        data->totalSteps :
        0;
}

uint64 StepCounter::GetDailySteps(
    Player* player) const
{
    PlayerData const* data =
        FindData(player);

    return data ?
        data->dailySteps :
        0;
}

uint64 StepCounter::GetWeeklySteps(
    Player* player) const
{
    PlayerData const* data =
        FindData(player);

    return data ?
        data->weeklySteps :
        0;
}

/*
 * =============================================================
 * Reset
 * =============================================================
 */

bool StepCounter::ResetSteps(
    Player* player)
{
    auto itr =
        _players.find(player->GetGUID());

    if (itr == _players.end())
        return false;

    PlayerData& data =
        itr->second;

    data.totalSteps = 0;
    data.dailySteps = 0;
    data.weeklySteps = 0;

    data.dayKey =
        GetCurrentDayKey();

    data.weekKey =
        GetCurrentWeekKey();

    data.accumulatedDistance = 0.0f;

    data.dirty = true;

    /*
     * Delete daily/weekly reward claims.
     *
     * Total rewards are intentionally retained.
     */
    CharacterDatabase.PExecute(
        "DELETE FROM character_step_counter_rewards "
        "WHERE guid = {} "
        "AND reward_id IN "
        "(SELECT id FROM step_counter_rewards "
        " WHERE period_type IN ('daily', 'weekly'))",
        player->GetGUID().GetCounter());

    ResetMovement(player);

    Save(player);

    return true;
}

/*
 * =============================================================
 * Commands
 * =============================================================
 */

static bool HandleStepsCommand(
    ChatHandler* handler)
{
    Player* player =
        handler->GetPlayer();

    if (!player)
        return false;

    StepCounter* counter =
        StepCounter::Instance();

    if (!counter)
        return false;

    handler->PSendSysMessage(
        "================================");

    handler->PSendSysMessage(
        "Schrittzähler: %s",
        player->GetName().c_str());

    handler->PSendSysMessage(
        "Heute:       %llu",
        static_cast<unsigned long long>(
            counter->GetDailySteps(player)));

    handler->PSendSysMessage(
        "Diese Woche: %llu",
        static_cast<unsigned long long>(
            counter->GetWeeklySteps(player)));

    handler->PSendSysMessage(
        "Gesamt:      %llu",
        static_cast<unsigned long long>(
            counter->GetTotalSteps(player)));

    handler->PSendSysMessage(
        "================================");

    return true;
}

static bool HandleStepsResetCommand(
    ChatHandler* handler)
{
    Player* player =
        handler->GetPlayer();

    if (!player)
        return false;

    StepCounter* counter =
        StepCounter::Instance();

    if (!counter)
        return false;

    if (!counter->ResetSteps(player))
    {
        handler->PSendSysMessage(
            "Schrittzähler konnte nicht zurückgesetzt werden.");

        return false;
    }

    handler->PSendSysMessage(
        "Dein Schrittzähler wurde zurückgesetzt.");

    return true;
}

/*
 * =============================================================
 * Top list
 * =============================================================
 */

static bool HandleStepsTopCommand(
    ChatHandler* handler,
    std::string const& type)
{
    std::string column;
    std::string title;

    if (type == "week")
    {
        column = "weekly_steps";
        title = "Top 10 - Diese Woche";
    }
    else if (type == "total")
    {
        column = "total_steps";
        title = "Top 10 - Gesamt";
    }
    else
    {
        column = "daily_steps";
        title = "Top 10 - Heute";
    }

    handler->PSendSysMessage(
        "================================");

    handler->PSendSysMessage(
        "%s",
        title.c_str());

    handler->PSendSysMessage(
        "================================");

    QueryResult result;

    if (type == "week")
    {
        result = CharacterDatabase.PQuery(
            "SELECT c.name, s.weekly_steps "
            "FROM character_step_counter s "
            "INNER JOIN characters c ON c.guid = s.guid "
            "WHERE s.weekly_steps > 0 "
            "ORDER BY s.weekly_steps DESC, c.name ASC "
            "LIMIT {}",
            StepCounterConfig::TOP_LIMIT);
    }
    else if (type == "total")
    {
        result = CharacterDatabase.PQuery(
            "SELECT c.name, s.total_steps "
            "FROM character_step_counter s "
            "INNER JOIN characters c ON c.guid = s.guid "
            "WHERE s.total_steps > 0 "
            "ORDER BY s.total_steps DESC, c.name ASC "
            "LIMIT {}",
            StepCounterConfig::TOP_LIMIT);
    }
    else
    {
        result = CharacterDatabase.PQuery(
            "SELECT c.name, s.daily_steps "
            "FROM character_step_counter s "
            "INNER JOIN characters c ON c.guid = s.guid "
            "WHERE s.daily_steps > 0 "
            "ORDER BY s.daily_steps DESC, c.name ASC "
            "LIMIT {}",
            StepCounterConfig::TOP_LIMIT);
    }

    if (result)
    {
        uint32 rank = 1;

        do
        {
            Field* fields = result->Fetch();

            std::string name = fields[0].GetString();
            uint64 steps = fields[1].GetUInt64();

            handler->PSendSysMessage(
                "#%u  %s - %llu",
                rank,
                name.c_str(),
                static_cast<unsigned long long>(steps));

            ++rank;

        } while (result->NextRow());
    }
    else
    {
        handler->PSendSysMessage("Noch keine Einträge.");
    }

    handler->PSendSysMessage("================================");

    return true;
}

static bool HandleStepsTopTodayCommand(
    ChatHandler* handler)
{
    return HandleStepsTopCommand(
        handler,
        "day");
}

static bool HandleStepsTopWeekCommand(
    ChatHandler* handler)
{
    return HandleStepsTopCommand(
        handler,
        "week");
}

static bool HandleStepsTopTotalCommand(
    ChatHandler* handler)
{
    return HandleStepsTopCommand(
        handler,
        "total");
}

/*
 * =============================================================
 * CommandScript
 * =============================================================
 */

StepCounterCommand::StepCounterCommand()
    : CommandScript("step_counter_commandscript")
{
}

std::span<ChatCommandBuilder const>
StepCounterCommand::GetCommands() const
{
    static ChatCommandTable topCommandTable =
    {
        {
            "week",
            HandleStepsTopWeekCommand,
            rbac::RBAC_PERM_COMMAND_COMMANDS,
            Console::No
        },
        {
            "total",
            HandleStepsTopTotalCommand,
            rbac::RBAC_PERM_COMMAND_COMMANDS,
            Console::No
        }
    };

    static ChatCommandTable stepsCommandTable =
    {
        {
            "",
            HandleStepsCommand,
            rbac::RBAC_PERM_COMMAND_COMMANDS,
            Console::No
        },
        {
            "reset",
            HandleStepsResetCommand,
            rbac::RBAC_PERM_COMMAND_COMMANDS,
            Console::No
        },
        {
            "top",
            HandleStepsTopTodayCommand,
            rbac::RBAC_PERM_COMMAND_COMMANDS,
            Console::No
        },
        {
            "top",
            topCommandTable
        }
    };

    static ChatCommandTable commandTable =
    {
        {
            "steps",
            stepsCommandTable
        }
    };

    return commandTable;
}

/*
 * =============================================================
 * Registration
 * =============================================================
 */

void AddSC_step_counter()
{
    new StepCounter();
    new StepCounterCommand();
}
