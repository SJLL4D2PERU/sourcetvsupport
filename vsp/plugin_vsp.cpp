#include "plugin_vsp.h"
#include <icvar.h>
#include <convar.h>

// Macro para contar elementos de la lista
#ifndef NELEMS
#define NELEMS(x) (sizeof(x) / sizeof(x[0]))
#endif

EXPOSE_SINGLE_INTERFACE(VSPPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS);

// Definimos el puntero global de la interfaz de comandos
ICvar *g_pCVar = NULL;

bool VSPPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	// 1. Obtenemos la interfaz de las ConVars
	g_pCVar = static_cast<ICvar*>(interfaceFactory(CVAR_INTERFACE_VERSION, NULL));
	if (g_pCVar == NULL) {
		return false;
	}

	// 2. Lista de comandos para L4D2 Fenix
	static const char* const cvars[] = {
		"tv_enable",
		"tv_maxclients",
		"tv_delay",
		"tv_snapshotrate",
		"tv_autorecord",
		"tv_transmitall",
		"tv_maxrate",
		"tv_port",
		"sv_hibernate_when_empty",
		"sv_master_share_game_socket"
	};

	size_t handled = 0;
	for (size_t i = 0; i < NELEMS(cvars); i++) {
		ConVar* pCvar = g_pCVar->FindVar(cvars[i]);
		
		if (pCvar) {
			handled++;
			// SOLUCIÓN AL ERROR: Usamos el método público 'RemoveFlags'
			// en lugar de intentar acceder a la variable privada m_nFlags
			pCvar->RemoveFlags(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN);
		}
	}

	printf(PLUGIN_LOG_PREFIX "SourceTV Unlocker: %u cvars successfully exposed.\n", (unsigned int)handled);

	// Devolvemos true para que el plugin se mantenga cargado
	return true;
}

// No añadimos las funciones Unload/Pause aquí porque ya están en el .h
