#include "extension.h"
#include "wrappers.h"

#include "sdk/public/engine/inetsupport.h"
#include "sdk/engine/networkstringtable.h"

#include <vector>
#include <am-string.h>
#include <checksum_crc.h>
#include <extensions/IBinTools.h>
#include <extensions/ISDKTools.h>

// Interfaces
INetworkStringTableContainer* networkStringTableContainerServer = NULL;
IHLTVDirector* hltvdirector = NULL;
IHLTVServer* g_pHLTVServer = NULL;
INetSupport* g_pNetSupport = NULL;
IPlayerInfoManager* playerinfomanager = NULL;
IServerGameEnts* gameents = NULL;
IBinTools* bintools = NULL;
ISDKTools* sdktools = NULL;

CGlobalVars* gpGlobals = NULL;

IServer* g_pGameIServer = NULL;

int CBasePlayer::sendprop_m_fFlags = 0;
int CBaseServer::offset_stringTableCRC = 0;

// =========================================================================
// VARIABLES NECESARIAS PARA QUE EL LINKER NO FALLE (WRAPPERS.CPP)
// Se mantienen declaradas e inicializadas en 0/NULL para compatibilidad,
// pero no se usarán en la lógica "Lite" para evitar crashes.
// =========================================================================

void* pfn_DataTable_WriteSendTablesBuffer = NULL;
void* pfn_SteamGameServer_GetHSteamPipe = NULL;
void* pfn_SteamGameServer_GetHSteamUser = NULL;
void* pfn_SteamInternal_CreateInterface = NULL;
void* pfn_SteamInternal_GameServer_Init = NULL;
void* pfn_OpenSocketInternal = NULL;

// Offsets y variables de clases
int CBaseServer::vtblindex_GetChallengeNr = 0;
int CBaseServer::vtblindex_GetChallengeType = 0;
int CBaseServer::vtblindex_ReplyChallenge = 0;
int CBaseServer::vtblindex_FillServerInfo = 0;
int CBaseServer::vtblindex_ConnectClient = 0;
int CHLTVServer::offset_m_DemoRecorder = 0;
int CHLTVServer::offset_CClientFrameManager = 0;
int CHLTVServer::offset_CBaseServer = 0;
int CHLTVServer::vtblindex_FillServerInfo = 0;
int CHLTVServer::shookid_ReplyChallenge = 0;
int CHLTVServer::shookid_FillServerInfo = 0;
int CHLTVServer::shookid_hltv_FillServerInfo = 0;
int CHLTVServer::shookid_ConnectClient = 0;
int CGameServer::shookid_IsPausable = 0;
int CBaseClient::offset_m_SteamID = 0;
int CFrameSnapshotManager::offset_m_PackedEntitiesPool = 0;
int HitAnnouncement::pzMsgId = 0;

// Punteros de Detour y Wrappers (Inicializados nulos)
void* CBaseServer::pfn_IsExclusiveToLobbyConnections = NULL;
CDetour* CBaseServer::detour_IsExclusiveToLobbyConnections = NULL;
ICallWrapper* CBaseServer::vcall_GetChallengeNr = NULL;
ICallWrapper* CBaseServer::vcall_GetChallengeType = NULL;
void* CHLTVServer::pfn_AddNewFrame = NULL;
CDetour* CHLTVServer::detour_AddNewFrame = NULL;
void* CBaseClient::pfn_SendFullConnectEvent = NULL;
CDetour* CBaseClient::detour_SendFullConnectEvent = NULL;
void* CSteam3Server::pfn_NotifyClientDisconnect = NULL;
CDetour* CSteam3Server::detour_NotifyClientDisconnect = NULL;
void* CFrameSnapshotManager::pfn_LevelChanged = NULL;
CDetour* CFrameSnapshotManager::detour_LevelChanged = NULL;
void* CBaseAbility::pfn_ShouldTransmit = NULL;
CDetour* CBaseAbility::detour_ShouldTransmit = NULL;
void* HitAnnouncement::pfn_ForEachTerrorPlayer = NULL;
CDetour* HitAnnouncement::detour_ForEachTerrorPlayer = NULL;
CDetour* detour_SteamInternal_GameServer_Init = NULL;

int shookid_CHLTVDemoRecorder_RecordStringTables = 0;
int shookid_CHLTVDemoRecorder_RecordServerClasses = 0;
int shookid_SteamGameServer_LogOff = 0;
int shookid_CServerGameEnts_CheckTransmit = 0;

// =========================================================================
// FIN DE VARIABLES DE COMPATIBILIDAD
// =========================================================================

// SourceHook
SH_DECL_HOOK1_void(IHLTVDirector, SetHLTVServer, SH_NOATTRIB, 0, IHLTVServer*);

// Detours
#include <CDetour/detours.h>

SMExtension g_Extension;
SMEXT_LINK(&g_Extension);

// SMExtension
void SMExtension::Load()
{
	if ((g_pGameIServer = sdktools->GetIServer()) == NULL) {
		smutils->LogError(myself, "Unable to retrieve sv instance pointer!");
		return;
	}

	// NOTA: No activamos ningún Detour aquí para evitar el crash 'realloc'.
	// Solo hookeamos el director para saber cuándo aplicar el fix del CRC.

	SH_ADD_HOOK(IHLTVDirector, SetHLTVServer, hltvdirector, SH_MEMBER(this, &SMExtension::Handler_CHLTVDirector_SetHLTVServer), true);

	// Intentamos aplicar el fix inmediatamente si el SourceTV ya está activo
	OnSetHLTVServer(hltvdirector->GetHLTVServer());
	
	// Registramos la librería para que otros plugins sepan que estamos aquí
	sharesys->RegisterLibrary(myself, "sourcetvsupport");
}

void SMExtension::Unload()
{
	// Limpieza básica
	SH_REMOVE_HOOK(IHLTVDirector, SetHLTVServer, hltvdirector, SH_MEMBER(this, &SMExtension::Handler_CHLTVDirector_SetHLTVServer), true);
	OnSetHLTVServer(NULL);
}

bool SMExtension::SetupFromGameConfig(IGameConfig* gc, char* error, int maxlength)
{
	// Para la versión Lite, SOLO necesitamos el offset del CRC.
	// Ignoramos el resto para no fallar si el gamedata es antiguo.
	if (!gc->GetOffset("CBaseServer::stringTableCRC", &CBaseServer::offset_stringTableCRC)) {
		ke::SafeSprintf(error, maxlength, "Unable to get offset for \"CBaseServer::stringTableCRC\"");
		return false;
	}

	return true;
}

bool SMExtension::CreateDetours(char* error, size_t maxlength)
{
	// Versión Lite: Retornamos true pero NO creamos ningún detour.
	// Esto evita tocar la gestión de memoria que causa el crash.
	return true;
}

void SMExtension::OnSetHLTVServer(IHLTVServer* pIHLTVServer)
{
	g_pHLTVServer = pIHLTVServer;

	if (pIHLTVServer == NULL) {
		return;
	}

	CBaseServer* pServer = CBaseServer::FromIHLTVServer(pIHLTVServer);
	if (pServer == NULL) {
		return;
	}

	// =========================================================================
	// EL ARREGLO PRINCIPAL (THE FIX)
	// =========================================================================
	// Copiamos el CRC (checksum) del mapa desde el servidor principal al SourceTV.
	// Esto elimina el error "Map differs / String table differs" al reproducir demos.
	pServer->stringTableCRC() = CBaseServer::FromIServer(g_pGameIServer)->stringTableCRC();
	
	smutils->LogMessage(myself, "SourceTV Lite: StringTableCRC fix applied successfully.");
}

void SMExtension::Handler_CHLTVDirector_SetHLTVServer(IHLTVServer* pIHLTVServer)
{
	OnSetHLTVServer(pIHLTVServer);
}

bool SMExtension::SDK_OnLoad(char* error, size_t maxlength, bool late)
{
	IGameConfig* gc = NULL;
	if (!gameconfs->LoadGameConfigFile(GAMEDATA_FILE, &gc, error, maxlength)) {
		ke::SafeStrcpy(error, maxlength, "Unable to load a gamedata file \"" GAMEDATA_FILE ".txt\"");
		return false;
	}

	if (!SetupFromGameConfig(gc, error, maxlength)) {
		gameconfs->CloseGameConfigFile(gc);
		return false;
	}

	gameconfs->CloseGameConfigFile(gc);
	
	sharesys->AddDependency(myself, "sdktools.ext", true, true);
	sharesys->AddDependency(myself, "bintools.ext", true, true); 

	return true;
}

void SMExtension::SDK_OnUnload()
{
	Unload();
}

void SMExtension::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, sdktools);
	SM_GET_LATE_IFACE(BINTOOLS, bintools);

	if (sdktools != NULL) {
		Load();
	}
}

bool SMExtension::SDK_OnMetamodLoad(ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	// Obtenemos interfaces básicas necesarias
	GET_V_IFACE_CURRENT(GetEngineFactory, networkStringTableContainerServer, INetworkStringTableContainer, INTERFACENAME_NETWORKSTRINGTABLESERVER);
	GET_V_IFACE_CURRENT(GetServerFactory, hltvdirector, IHLTVDirector, INTERFACEVERSION_HLTVDIRECTOR);
	
	// Mantenemos estas por compatibilidad, aunque no se usen intensivamente en la versión Lite
	GET_V_IFACE_CURRENT(GetServerFactory, playerinfomanager, IPlayerInfoManager, INTERFACEVERSION_PLAYERINFOMANAGER);
	GET_V_IFACE_CURRENT(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);

	gpGlobals = ismm->GetCGlobals();

	return true;
}

bool SMExtension::QueryRunning(char* error, size_t maxlength)
{
	SM_CHECK_IFACE(SDKTOOLS, sdktools);
	return true;
}
