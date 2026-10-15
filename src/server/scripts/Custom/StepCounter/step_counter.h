#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include "ScriptMgr.h"
#include "ObjectGuid.h"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

class StepCounter : public PlayerScript
{
public:
    StepCounter();

    static StepCounter* Instance();

    void OnLogin(Player* player, bool firstLogin) override;
    void OnLogout(Player* player) override;
    void OnSave(Player* player) override;
    void OnMapChanged(Player* player) override;
    void OnUpdate(Player* player, uint32 diff);

    uint64 GetTotalSteps(Player* player) const;
    uint64 GetDailySteps(Player* player) const;
    uint64 GetWeeklySteps(Player* player) const;

    bool ResetSteps(Player* player);

private:

    enum class RewardPeriod : uint8
    {
        Daily = 0,
        Weekly = 1,
        Total = 2
    };

    enum class RewardType : uint8
    {
        Gold = 0,
        Item = 1
    };

    struct Reward
    {
        uint32 id = 0;

        RewardPeriod period = RewardPeriod::Daily;
        RewardType type = RewardType::Gold;

        uint64 requiredSteps = 0;

        uint32 itemEntry = 0;
        uint32 amount = 0;

        std::string message;
    };

    struct PlayerData
    {
        uint64 totalSteps = 0;
        uint64 dailySteps = 0;
        uint64 weeklySteps = 0;

        uint32 dayKey = 0;
        uint32 weekKey = 0;

        float accumulatedDistance = 0.0f;

        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastZ = 0.0f;

        uint32 updateTimer = 0;

        bool initialized = false;
        bool dirty = false;
    };

    PlayerData& GetData(Player* player);
    PlayerData const* FindData(Player* player) const;

    void Load(Player* player);
    void Save(Player* player);

    void ResetMovement(Player* player);

    void CheckPeriod(Player* player, PlayerData& data);

    void AddSteps(Player* player, uint32 steps);

    void LoadRewards();

    void CheckRewards(Player* player);

    bool HasClaimedReward(
        Player* player,
        Reward const& reward,
        uint32 periodKey) const;

    void MarkRewardClaimed(
        Player* player,
        Reward const& reward,
        uint32 periodKey);

    bool GiveReward(Player* player, Reward const& reward);

    uint32 GetCurrentDayKey() const;
    uint32 GetCurrentWeekKey() const;

    std::string GetPeriodName(RewardPeriod period) const;

    std::unordered_map<ObjectGuid, PlayerData> _players;

    std::vector<Reward> _rewards;

    bool _rewardsLoaded = false;

    static StepCounter* _instance;
};

class StepCounterCommand : public CommandScript
{
public:
    StepCounterCommand();

    std::span<Trinity::ChatCommands::ChatCommandBuilder const> GetCommands() const override;
};

void AddSC_step_counter();

#endif
