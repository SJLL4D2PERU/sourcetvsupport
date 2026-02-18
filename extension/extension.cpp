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

// --- DESACTIVADO: Offsets innecesarios para la versión Lite ---
/*
int CBaseServer::vtblindex_GetChallengeNr = 0;
int CBaseServer::vtblindex_GetChallengeType = 0;
int CBaseServer::vtblindex_ReplyChallenge = 0;
int CBaseServer::vtblindex_FillServerInfo = 0;
int CBaseServer::vtblindex_ConnectClient = 0;
void* CBaseServer::pfn_IsExclusiveToLobbyConnections = NULL;
CDetour* CBaseServer::detour_IsExclusiveToLobbyConnections = NULL;
ICallWrapper* CBaseServer::vcall_GetChallengeNr = NULL;
ICallWrapper* CBaseServer::vcall_GetChallengeType = NULL;
int CHLTVServer::offset_m_DemoRecorder = 0;
int CHLTVServer::offset_CClientFrameManager = 0;
int CHLTVServer::offset_CBaseServer = 0;
int CHLTVServer::vtblindex_FillServerInfo = 0;
int CHLTVServer::shookid_ReplyChallenge = 0;
int CHLTVServer::shookid_FillServerInfo = 0;
int CHLTVServer::shookid_hltv_FillServerInfo = 0;
int CHLTVServer::shookid_ConnectClient = 0;
void* CHLTVServer::pfn_AddNewFrame = NULL;
CDetour* CHLTVServer::detour_AddNewFrame = NULL;
int CGameServer::shookid_IsPausable = 0;
int CBaseClient::offset_m_SteamID = 0;
void* CBaseClient::pfn_SendFullConnectEvent = NULL;
CDetour* CBaseClient::detour_SendFullConnectEvent = NULL;
void* CSteam3Server::pfn_NotifyClientDisconnect = NULL;
CDetour* CSteam3Server::detour_NotifyClientDisconnect = NULL;
int CFrameSnapshotManager::offset_m_PackedEntitiesPool = 0;
void* CFrameSnapshotManager::pfn_LevelChanged = NULL;
CDetour* CFrameSnapshotManager::detour_LevelChanged = NULL;
void* CBaseAbility::pfn_ShouldTransmit = NULL;
CDetour* CBaseAbility::detour_ShouldTransmit = NULL;
void* HitAnnouncement::pfn_ForEachTerrorPlayer = NULL;
CDetour* HitAnnouncement::detour_ForEachTerrorPlayer = NULL;
int HitAnnouncement::pzMsgId = 0;
int shookid_CHLTVDemoRecorder_RecordStringTables = 0;
int shookid_CHLTVDemoRecorder_RecordServerClasses = 0;
int shookid_SteamGameServer_LogOff = 0;
int shookid_CServerGameEnts_CheckTransmit = 0;
void* pfn_DataTable_WriteSendTablesBuffer = NULL;
void* pfn_SteamGameServer_GetHSteamPipe = NULL;
void* pfn_SteamGameServer_GetHSteamUser = NULL;
void* pfn_SteamInternal_CreateInterface = NULL;
void* pfn_SteamInternal_GameServer_Init = NULL;
void* pfn_OpenSocketInternal = NULL;
CDetour* detour_SteamInternal_GameServer_Init = NULL;
*/

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

	// --- DESACTIVADO: Hooks peligrosos ---
	/*
	CBaseAbility::detour_ShouldTransmit->EnableDetour();
	CBaseServer::detour_IsExclusiveToLobbyConnections->EnableDetour();
	CSteam3Server::detour_NotifyClientDisconnect->EnableDetour();
	CHLTVServer::detour_AddNewFrame->EnableDetour();
	CFrameSnapshotManager::detour_LevelChanged->EnableDetour();
	HitAnnouncement::detour_ForEachTerrorPlayer->EnableDetour();
	#if SOURCE_ENGINE == SE_LEFT4DEAD2
		detour_SteamInternal_GameServer_Init->EnableDetour();
		CBaseClient::detour_SendFullConnectEvent->EnableDetour();
	#endif
	*/

	// Solo necesitamos saber cuando el SourceTV arranca para inyectar el CRC
	SH_ADD_HOOK(IHLTVDirector, SetHLTVServer, hltvdirector, SH_MEMBER(this, &SMExtension::Handler_CHLTVDirector_SetHLTVServer), true);

	// Intentamos aplicar el fix inmediatamente si ya está cargado
	OnSetHLTVServer(hltvdirector->GetHLTVServer());
	// OnGameServer_Init(); // No necesario en versión Lite

	// Let plugins know when it's safe to use SourceTV features
	sharesys->RegisterLibrary(myself, "sourcetvsupport");
}

void SMExtension::Unload()
{
	// --- Limpieza de Detours desactivados ---
	/*
	if (CBaseAbility::detour_ShouldTransmit) { CBaseAbility::detour_ShouldTransmit->Destroy(); CBaseAbility::detour_ShouldTransmit = NULL; }
	// ... (Resto de limpieza omitida para limpieza visual) ...
	*/

	// OnGameServer_Shutdown();

	SH_REMOVE_HOOK(IHLTVDirector, SetHLTVServer, hltvdirector, SH_MEMBER(this, &SMExtension::Handler_CHLTVDirector_SetHLTVServer), true);
	OnSetHLTVServer(NULL);
}

bool SMExtension::SetupFromGameConfig(IGameConfig* gc, char* error, int maxlength)
{
	// Solo necesitamos offset_stringTableCRC. El resto causa inestabilidad si el gamedata es viejo.
	if (!gc->GetOffset("CBaseServer::stringTableCRC", &CBaseServer::offset_stringTableCRC)) {
		ke::SafeSprintf(error, maxlength, "Unable to get offset for \"CBaseServer::stringTableCRC\"");
		return false;
	}

	return true;
}

bool SMExtension::CreateDetours(char* error, size_t maxlength)
{
	// En esta versión Lite NO creamos Detours porque son la causa del crash realloc.
	// Solo usamos SourceHooks y manipulación de memoria directa segura.
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
	// EL SANTO GRIAL: ESTO ES LO ÚNICO QUE IMPORTA PARA ARREGLAR TUS DEMOS
	// =========================================================================
	// Bug#1 fix: Copia el CRC correcto del servidor al SourceTV.
	// Esto hace que el mensaje "String table differs" desaparezca.
	pServer->stringTableCRC() = CBaseServer::FromIServer(g_pGameIServer)->stringTableCRC();
	
	smutils->LogMessage(myself, "SourceTV CRC Patcher: Applied StringTableCRC fix successfully.");
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
	
	// No necesitamos detours ni librerías de Steam API complejas para esta versión Lite

	sharesys->AddDependency(myself, "sdktools.ext", true, true);
	// bintools ya no es estrictamente necesario si no hacemos llamadas virtuales complejas, 
	// pero lo dejamos por compatibilidad.
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

	if (sdktools != NULL) { // bintools opcional
		Load();
	}
}

bool SMExtension::SDK_OnMetamodLoad(ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	// Solo necesitamos lo básico
	GET_V_IFACE_CURRENT(GetEngineFactory, networkStringTableContainerServer, INetworkStringTableContainer, INTERFACENAME_NETWORKSTRINGTABLESERVER);
	GET_V_IFACE_CURRENT(GetServerFactory, hltvdirector, IHLTVDirector, INTERFACEVERSION_HLTVDIRECTOR);
	
	// GameEnts y PlayerInfoManager pueden ser útiles en el futuro, los dejamos
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
