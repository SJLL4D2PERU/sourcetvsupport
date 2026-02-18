#include "plugin_vsp.h"
#include <icvar.h>

EXPOSE_SINGLE_INTERFACE(VSPPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS);

bool VSPPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
    // 1. Verificación de Interfaz
    g_pCVar = static_cast<ICvar*>(interfaceFactory(CVAR_INTERFACE_VERSION, NULL));
    if (g_pCVar == NULL) {
        return false; // Si no hay interfaz, salimos antes de romper nada
    }

    // 2. Lista de comandos (He quitado los que suelen dar problemas en Linux)
    static const char* const cvars[] = {
        "tv_enable", "tv_maxclients", "tv_delay", "tv_snapshotrate",
        "tv_autorecord", "tv_transmitall", "tv_maxrate", "tv_port",
        "sv_hibernate_when_empty", "sv_master_share_game_socket"
    };

    size_t handled = 0;
    for (auto&& name : cvars) {
        ConVar* pCvar = g_pCVar->FindVar(name);
        
        // 3. Verificación doble de seguridad
        if (pCvar == NULL || pCvar == (ConVar*)0x0) {
            continue;
        }

        handled++;
        
        // 4. Limpieza selectiva de Flags
        // Quitamos DEVELOPMENTONLY y HIDDEN para asegurar que sean visibles
        int flags = pCvar->GetFlags();
        flags &= ~FCVAR_DEVELOPMENTONLY;
        flags &= ~FCVAR_HIDDEN;
        pCvar->SetFlags(flags);
    }

    printf(PLUGIN_LOG_PREFIX "SourceTV Unlocker: %u cvars unlocked for L4D2 Fenix.\n", (unsigned int)handled);

    // 5. CAMBIO DE ESTABILIDAD:
    // Devolvemos 'true' para que el plugin se quede cargado. 
    // Descargar un plugin (return false) justo en el arranque a veces causa 
    // que el motor intente acceder a memoria que ya fue liberada, provocando el crash.
    return true; 
}

// Implementación de funciones vacías para evitar errores de Linker
void VSPPlugin::Unload() {}
void VSPPlugin::Pause() {}
void VSPPlugin::UnPause() {}
const char* VSPPlugin::GetPluginDescription() { return "L4D2 Fenix: SourceTV Unlocker (Stable)"; }
void VSPPlugin::LevelInit(char const *pMapName) {}
void VSPPlugin::ServerActivate(edict_t *pEdictList, int edictCount, int clientMax) {}
void VSPPlugin::GameFrame(bool simulating) {}
void VSPPlugin::LevelShutdown() {}
void VSPPlugin::ClientActive(edict_t *pEntity) {}
void VSPPlugin::ClientDisconnect(edict_t *pEntity) {}
void VSPPlugin::ClientPutInServer(edict_t *pEntity, char const *playername) {}
void VSPPlugin::SetCommandClient(int index) {}
void VSPPlugin::ClientSettingsChanged(edict_t *pEdict) {}
PLUGIN_RESULT VSPPlugin::ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen) { return PLUGIN_CONTINUE; }
PLUGIN_RESULT VSPPlugin::ClientCommand(edict_t *pEntity, const CCommand &args) { return PLUGIN_CONTINUE; }
PLUGIN_RESULT VSPPlugin::NetworkIDValidated(const char *pszUserName, const char *pszNetworkID) { return PLUGIN_CONTINUE; }
void VSPPlugin::OnQueryCvarCookieCompleted(QueryCvarCookie_t cookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue) {}
void VSPPlugin::OnEdictAllocated(edict_t *pEdict) {}
void VSPPlugin::OnEdictFreed(const edict_t *pEdict) {}
