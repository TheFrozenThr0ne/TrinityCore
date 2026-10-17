#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "Config.h"
#include "Chat.h"
#include "Log.h"
#include "Language.h"

#include <ctime>
#include <string>

class ScheduledShutdownPlayer : public PlayerScript
{
public:
    ScheduledShutdownPlayer() : PlayerScript("ScheduledShutdownPlayer") {}

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (sConfigMgr->GetBoolDefault("ScheduledShutdown.Announce", true))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cff00ff00[ScheduledShutdown]|r Welcome! Module is running."
            );
        }
    }
};

class ScheduledShutdownWorld : public WorldScript
{
public:
    ScheduledShutdownWorld() : WorldScript("ScheduledShutdownWorld") {}

    void OnStartup() override
    {
        shutdownEnabled =
            sConfigMgr->GetBoolDefault("ScheduledShutdown.Enable", true);

        shutdownHour =
            sConfigMgr->GetIntDefault("ScheduledShutdown.Hour", 4);

        shutdownMinute =
            sConfigMgr->GetIntDefault("ScheduledShutdown.Minute", 0);

        announcementSeconds =
            sConfigMgr->GetIntDefault(
                "ScheduledShutdown.AnnouncementSeconds", 60);

        if (announcementSeconds < 1)
            announcementSeconds = 1;

        alreadyExecuted = false;

        if (shutdownEnabled)
        {
            TC_LOG_INFO(
                "server.scripts",
                "ScheduledShutdown: Enabled (Daily at {:02d}:{:02d})",
                shutdownHour,
                shutdownMinute
            );
        }
        else
        {
            TC_LOG_INFO(
                "server.scripts",
                "ScheduledShutdown: Disabled via config."
            );
        }
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        if (!shutdownEnabled)
            return;

        time_t now = time(nullptr);
        tm localTime;

#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        if (localTime.tm_hour == shutdownHour &&
            localTime.tm_min == shutdownMinute)
        {
            if (!alreadyExecuted)
            {
                std::string msg =
                    "Automatic server restart in " +
                    std::to_string(announcementSeconds) +
                    " seconds!";

                sWorld->SendWorldText(
                    LANG_SYSTEMMESSAGE,
                    msg.c_str()
                );

                sWorld->ShutdownServ(
                    announcementSeconds,
                    0,
                    RESTART_EXIT_CODE,
                    "Planned daily restart"
                );

                alreadyExecuted = true;
            }
        }
        else
        {
            alreadyExecuted = false;
        }
    }

private:
    bool shutdownEnabled = true;
    int32 shutdownHour = 4;
    int32 shutdownMinute = 0;
    int32 announcementSeconds = 60;
    bool alreadyExecuted = false;
};

void AddSC_ScheduledShutdownScripts()
{
    new ScheduledShutdownPlayer();
    new ScheduledShutdownWorld();
}
