#include "Log.h"

void Addmod_playerhousingScripts();

void Addmod_playerhousing()
{
    LOG_INFO("server.loading", "mod_playerhousing: Loading scripts.");
    Addmod_playerhousingScripts();
}
