#include <base/log.h>

#include <engine/shared/protocol.h>

#include "tclient.h"

#include "data_version.h"

#include <engine/client/enums.h>
#include <engine/external/ddnet-custom-clients/custom_clients_ids.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

static constexpr const char *TCLIENT_INFO_URL = "https://update.tclient.app/info.json";

CTClient::CTClient()
{
	OnReset();
}

void CTClient::ConRandomTee(IConsole::IResult *pResult, void *pUserData) {}

void CTClient::ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// Resolve type to randomize
	// Check length of type (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag)
	bool RandomizeBody = false;
	bool RandomizeFeet = false;
	bool RandomizeSkin = false;
	bool RandomizeFlag = false;

	if(pResult->NumArguments() == 0)
	{
		RandomizeBody = true;
		RandomizeFeet = true;
		RandomizeSkin = true;
		RandomizeFlag = true;
	}
	else if(pResult->NumArguments() == 1)
	{
		const char *Type = pResult->GetString(0);
		int Length = Type ? str_length(Type) : 0;
		if(Length == 1 && Type[0] == '0')
		{ // Randomize all
			RandomizeBody = true;
			RandomizeFeet = true;
			RandomizeSkin = true;
			RandomizeFlag = true;
		}
		else if(Length == 1)
		{
			// Randomize body
			RandomizeBody = Type[0] == '1';
		}
		else if(Length == 2)
		{
			// Check for body and feet
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
		}
		else if(Length == 3)
		{
			// Check for body, feet and skin
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
		}
		else if(Length == 4)
		{
			// Check for body, feet, skin and flag
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
			RandomizeFlag = Type[3] == '1';
		}
	}

	if(RandomizeBody)
		RandomBodyColor();
	if(RandomizeFeet)
		RandomFeetColor();
	if(RandomizeSkin)
		RandomSkin(pUserData);
	if(RandomizeFlag)
		RandomFlag(pUserData);
	pThis->GameClient()->SendInfo(false);
}

void CTClient::OnInit()
{
	TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	FetchTClientInfo();

	char aError[512] = "";
	if(!Storage()->FileExists("tclient/gui_logo.png", IStorage::TYPE_ALL))
		str_format(aError, sizeof(aError), TCLocalize("%s not found", DATA_VERSION_PATH), "data/tclient/gui_logo.png");
	if(aError[0] == '\0')
		CheckDataVersion(aError, sizeof(aError), Storage()->OpenFile(DATA_VERSION_PATH, IOFLAG_READ, IStorage::TYPE_ALL));
	if(aError[0] != '\0')
	{
		SWarning Warning(aError, TCLocalize("You have probably only installed the TClient DDNet.exe which is not supported, please use the entire TClient folder", "data_version.h"));
		Client()->AddWarning(Warning);
	}
}

static bool LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHL = str_utf8_find_nocase(pLine, pName);
	if(pHL)
	{
		int Length = str_length(pName);
		if(Length > 0 && (pLine == pHL || pHL[-1] == ' ') && (pHL[Length] == 0 || pHL[Length] == ' ' || pHL[Length] == '.' || pHL[Length] == '!' || pHL[Length] == ',' || pHL[Length] == '?' || pHL[Length] == ':'))
			return true;
	}
	return false;
}

bool CTClient::SendNonDuplicateMessage(int Team, const char *pLine)
{
	if(str_comp(pLine, m_PreviousOwnMessage) != 0)
	{
		GameClient()->m_Chat.SendChat(Team, pLine);
		return true;
	}
	str_copy(m_PreviousOwnMessage, pLine);
	return false;
}

void CTClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		int ClientId = pMsg->m_ClientId;

		if(ClientId < 0 || ClientId > MAX_CLIENTS)
			return;
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(ClientId == LocalId)
			str_copy(m_PreviousOwnMessage, pMsg->m_pMessage);

		bool PingMessage = false;

		bool ValidIds = !(GameClient()->m_aLocalIds[0] < 0 || (GameClient()->Client()->DummyConnected() && GameClient()->m_aLocalIds[1] < 0));

		if(ValidIds && ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && (!GameClient()->Client()->DummyConnected() || ClientId != GameClient()->m_aLocalIds[1]))
		{
			PingMessage |= LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[0]].m_aName);
			PingMessage |= GameClient()->Client()->DummyConnected() && LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[1]].m_aName);
		}

		if(pMsg->m_Team == TEAM_WHISPER_RECV)
			PingMessage = true;

		if(!PingMessage)
			return;

		char aPlayerName[MAX_NAME_LENGTH];
		str_copy(aPlayerName, GameClient()->m_aClients[ClientId].m_aName, sizeof(aPlayerName));

		bool PlayerMuted = GameClient()->m_aClients[ClientId].m_Foe || GameClient()->m_aClients[ClientId].m_ChatIgnore;
		if(g_Config.m_TcAutoReplyMuted && PlayerMuted)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}

		bool WindowActive = m_pGraphics && m_pGraphics->WindowActive();
		if(g_Config.m_TcAutoReplyMinimized && !WindowActive && m_pGraphics)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}
	}

	if(MsgType == NETMSGTYPE_SV_VOTESET)
	{
		const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy]; // Do not care about spec behaviour
		const bool Afk = LocalId >= 0 && GameClient()->m_aClients[LocalId].m_Afk; // TODO Depends on server afk time
		CNetMsg_Sv_VoteSet *pMsg = (CNetMsg_Sv_VoteSet *)pRawMsg;
		if(pMsg->m_Timeout && !Afk)
		{
			char aDescription[VOTE_DESC_LENGTH];
			char aReason[VOTE_REASON_LENGTH];
			str_copy(aDescription, pMsg->m_pDescription);
			str_copy(aReason, pMsg->m_pReason);
			bool KickVote = str_startswith(aDescription, "Kick ") != 0 ? true : false;
			bool SpecVote = str_startswith(aDescription, "Pause ") != 0 ? true : false;
			bool SettingVote = !KickVote && !SpecVote;
			bool RandomMapVote = SettingVote && str_find_nocase(aDescription, "random");
			bool MapCoolDown = SettingVote && (str_find_nocase(aDescription, "change map") || str_find_nocase(aDescription, "no not change map"));
			bool CategoryVote = SettingVote && (str_find_nocase(aDescription, "☐") || str_find_nocase(aDescription, "☒"));
			bool FunVote = SettingVote && str_find_nocase(aDescription, "funvote");
			bool MapVote = SettingVote && !RandomMapVote && !MapCoolDown && !CategoryVote && !FunVote && (str_find_nocase(aDescription, "Map:") || str_find_nocase(aDescription, "★") || str_find_nocase(aDescription, "✰"));

			if(g_Config.m_TcAutoVoteWhenFar && (MapVote || RandomMapVote))
			{
				int RaceTime = 0;
				if(GameClient()->m_Snap.m_pGameInfoObj && GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
					RaceTime = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();

				if(RaceTime / 60 >= g_Config.m_TcAutoVoteWhenFarTime)
				{
					CGameClient::CClientData *pVoteCaller = nullptr;
					int CallerId = -1;
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(!GameClient()->m_aStats[i].IsActive())
							continue;

						char aBuf[MAX_NAME_LENGTH + 4];
						str_format(aBuf, sizeof(aBuf), "\'%s\'", GameClient()->m_aClients[i].m_aName);
						if(str_find_nocase(aBuf, pMsg->m_pDescription) == 0)
						{
							pVoteCaller = &GameClient()->m_aClients[i];
							CallerId = i;
						}
					}
					if(pVoteCaller)
					{
						bool Friend = pVoteCaller->m_Friend;
						bool SameTeam = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) == pVoteCaller->m_Team && pVoteCaller->m_Team != 0;
						bool MySelf = CallerId == GameClient()->m_Snap.m_LocalClientId;

						if(!Friend && !SameTeam && !MySelf)
						{
							GameClient()->m_Voting.Vote(-1);
							if(str_comp(g_Config.m_TcAutoVoteWhenFarMessage, "") != 0)
								SendNonDuplicateMessage(0, g_Config.m_TcAutoVoteWhenFarMessage);
						}
					}
				}
			}
		}
	}

	auto &vServerCommands = GameClient()->m_Chat.m_vServerCommands;
	auto AddSpecId = [&](bool Enable) {
		static const CChat::CCommand SpecId("specid", "v[id]", "Spectate a player");
		vServerCommands.erase(std::remove_if(vServerCommands.begin(), vServerCommands.end(), [](const CChat::CCommand &Command) { return Command == SpecId; }), vServerCommands.end());
		if(Enable)
			vServerCommands.push_back(SpecId);
		GameClient()->m_Chat.m_ServerCommandsNeedSorting = true;
	};
	if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(!ServerCommandExists("specid"));
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(false);
		return;
	}
	if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(false);
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(ServerCommandExists("spec"));
		return;
	}
}

void CTClient::ConSpecId(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->SpecId(pResult->GetInteger(0));
}

bool CTClient::ChatDoSpecId(const char *pInput)
{
	const char *pNumber = str_startswith_nocase(pInput, "/specid ");
	if(!pNumber)
		return false;

	const int Length = str_length(pInput);
	CChat::CHistoryEntry *pEntry = GameClient()->m_Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
	pEntry->m_Team = 0;
	str_copy(pEntry->m_aText, pInput, Length + 1);

	int ClientId = 0;
	if(!str_toint(pNumber, &ClientId))
		return true;

	SpecId(ClientId);
	return true;
}

void CTClient::SpecId(int ClientId)
{
	char aBuf[256];
	if(ClientId < 0 || ClientId > (int)std::size(GameClient()->m_aClients))
	{
		str_format(aBuf, sizeof(aBuf), "Invalid Id '%d'", ClientId);
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		return;
	}
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(!Player.m_Active)
	{
		str_format(aBuf, sizeof(aBuf), "Id '%d' is not connected", ClientId);
		GameClient()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
		return;
	}
	str_copy(aBuf, "/spec \"");
	char *pDst = aBuf + strlen(aBuf);
	str_escape(&pDst, Player.m_aName, aBuf + sizeof(aBuf));
	str_append(aBuf, "\"");
	dbg_msg("tclient", "sending '%s'", aBuf);
	GameClient()->m_Chat.SendChat(0, aBuf);
}

void CTClient::ConEmoteCycle(IConsole::IResult *pResult, void *pUserData)
{
	CTClient &This = *(CTClient *)pUserData;
	This.m_EmoteCycle += 1;
	if(This.m_EmoteCycle > 15)
		This.m_EmoteCycle = 0;
	This.GameClient()->m_Emoticon.Emote(This.m_EmoteCycle);
}

void CTClient::AirRescue()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && (GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) == 0)
	{
		GameClient()->Echo("You are not in practice");
		return;
	}

	auto IsIndexAirLike = [&](int Index) {
		const auto Tile = Collision()->GetTileIndex(Index);
		return Tile == TILE_AIR || Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE;
	};
	auto IsPosAirLike = [&](vec2 Pos) {
		const int Index = Collision()->GetPureMapIndex(Pos);
		return IsIndexAirLike(Index);
	};
	auto IsRadiusAirLike = [&](vec2 Pos, int Radius) {
		for(int y = -Radius; y <= Radius; ++y)
			for(int x = -Radius; x <= Radius; ++x)
				if(!IsPosAirLike(Pos + vec2(x, y) * 32.0f))
					return false;
		return true;
	};

	auto &AirRescuePositions = m_aAirRescuePositions[g_Config.m_ClDummy];
	while(!AirRescuePositions.empty())
	{
		// Get latest pos from positions
		const vec2 NewPos = AirRescuePositions.front();
		AirRescuePositions.pop_front();
		// Check for safety
		if(!IsRadiusAirLike(NewPos, 2))
			continue;
		// Do it
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/tpxy %f %f", NewPos.x / 32.0f, NewPos.y / 32.0f);
		GameClient()->m_Chat.SendChat(0, aBuf);
		return;
	}

	GameClient()->Echo("No safe position found");
}

void CTClient::ConAirRescue(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->AirRescue();
}

void CTClient::ConCalc(IConsole::IResult *pResult, void *pUserData)
{
	int Error = 0;
	double Out = te_interp(pResult->GetString(0), &Error);
	if(Out == NAN || Error != 0)
		log_info("tclient", "Calc error: %d", Error);
	else
		log_info("tclient", "Calc result: %lf", Out);
}

void CTClient::OnConsoleInit()
{
	Console()->Register("calc", "r[expression]", CFGFLAG_CLIENT, ConCalc, this, "Evaluate an expression");
	Console()->Register("airrescue", "", CFGFLAG_CLIENT, ConAirRescue, this, "Rescue to a nearby air tile");

	Console()->Register("tc_random_player", "s[type]", CFGFLAG_CLIENT, ConRandomTee, this, "Randomize player color (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag) example: 0011 = randomize skin and flag [number is position]");
	Console()->Chain("tc_random_player", ConchainRandomColor, this);

	Console()->Register("spec_id", "v[id]", CFGFLAG_CLIENT, ConSpecId, this, "Spectate a player by Id");

	Console()->Register("emote_cycle", "", CFGFLAG_CLIENT, ConEmoteCycle, this, "Cycle through emotes");

	Console()->Chain(
		"tc_allow_any_resolution", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CTClient *)pUserData)->SetForcedAspect();
		},
		this);
}

void CTClient::RandomBodyColor()
{
	g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomFeetColor()
{
	g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomSkin(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	const auto &Skins = pThis->GameClient()->m_Skins.SkinList().Skins();
	str_copy(g_Config.m_ClPlayerSkin, Skins[std::rand() % (int)Skins.size()].SkinContainer()->Name());
}

void CTClient::RandomFlag(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// get the flag count
	int FlagCount = pThis->GameClient()->m_CountryFlags.Num();

	// get a random flag number
	int FlagNumber = std::rand() % FlagCount;

	// get the flag name
	const CCountryFlags::CCountryFlag *pFlag = pThis->GameClient()->m_CountryFlags.GetByIndex(FlagNumber);

	// set the flag code as number
	g_Config.m_PlayerCountry = pFlag->m_CountryCode;
}

void CTClient::DoFinishCheck()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_TcChangeNameNearFinish <= 0)
		return;
	m_FinishTextTimeout -= Client()->RenderFrameTime();
	if(m_FinishTextTimeout > 0.0f)
		return;
	m_FinishTextTimeout = 1.0f;
	// Check for finish tile
	const auto &NearTile = [this](vec2 Pos, int RadiusInTiles, int Tile) -> bool {
		const CCollision *pCollision = GameClient()->Collision();
		for(int i = 0; i <= RadiusInTiles * 2; ++i)
		{
			const float h = std::ceil(std::pow(std::sin((float)i * pi / 2.0f / (float)RadiusInTiles), 0.5f) * pi / 2.0f * (float)RadiusInTiles);
			const vec2 Pos1 = vec2(Pos.x + (float)(i - RadiusInTiles) * 32.0f, Pos.y - h);
			const vec2 Pos2 = vec2(Pos.x + (float)(i - RadiusInTiles) * 32.0f, Pos.y + h);
			std::vector<int> vIndices = pCollision->GetMapIndices(Pos1, Pos2);
			if(vIndices.empty())
				vIndices.push_back(pCollision->GetPureMapIndex(Pos1));
			for(int &Index : vIndices)
			{
				if(pCollision->GetTileIndex(Index) == Tile)
					return true;
				if(pCollision->GetFrontTileIndex(Index) == Tile)
					return true;
			}
		}
		return false;
	};
	const auto &SendUrgentRename = [this](int Conn, const char *pNewName) {
		CNetMsg_Cl_ChangeInfo Msg;
		Msg.m_pName = pNewName;
		Msg.m_pClan = Conn == 0 ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan;
		Msg.m_Country = Conn == 0 ? g_Config.m_PlayerCountry : g_Config.m_ClDummyCountry;
		Msg.m_pSkin = Conn == 0 ? g_Config.m_ClPlayerSkin : g_Config.m_ClDummySkin;
		Msg.m_UseCustomColor = Conn == 0 ? g_Config.m_ClPlayerUseCustomColor : g_Config.m_ClDummyUseCustomColor;
		Msg.m_ColorBody = Conn == 0 ? g_Config.m_ClPlayerColorBody : g_Config.m_ClDummyColorBody;
		Msg.m_ColorFeet = Conn == 0 ? g_Config.m_ClPlayerColorFeet : g_Config.m_ClDummyColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(Conn, &Packer, MSGFLAG_VITAL);
		GameClient()->m_aCheckInfo[Conn] = Client()->GameTickSpeed(); // 1 second
	};
	int Dummy = g_Config.m_ClDummy;
	const auto &Player = GameClient()->m_aClients[GameClient()->m_aLocalIds[Dummy]];
	if(!Player.m_Active)
		return;
	const char *NewName = g_Config.m_TcFinishName;
	if(str_comp(Player.m_aName, NewName) == 0)
		return;
	if(!NearTile(Player.m_RenderPos, 10, TILE_FINISH))
		return;
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), TCLocalize("Changing name to %s near finish"), NewName);
	GameClient()->Echo(aBuf);
	SendUrgentRename(Dummy, NewName);
}

bool CTClient::ServerCommandExists(const char *pCommand)
{
	for(const auto &Command : GameClient()->m_Chat.m_vServerCommands)
		if(str_comp_nocase(pCommand, Command.m_aName) == 0)
			return true;
	return false;
}

void CTClient::OnRender()
{
	if(m_pTClientInfoTask)
	{
		if(m_pTClientInfoTask->State() == EHttpState::DONE)
		{
			FinishTClientInfo();
			ResetTClientInfoTask();
		}
	}

	DoFinishCheck();
}

bool CTClient::NeedUpdate()
{
	return str_comp(m_aVersionStr, "0") != 0;
}

void CTClient::ResetTClientInfoTask()
{
	if(m_pTClientInfoTask)
	{
		m_pTClientInfoTask->Abort();
		m_pTClientInfoTask = NULL;
	}
}

void CTClient::FetchTClientInfo()
{
	if(m_pTClientInfoTask && !m_pTClientInfoTask->Done())
		return;
	char aUrl[256];
	str_copy(aUrl, TCLIENT_INFO_URL);
	m_pTClientInfoTask = HttpGet(aUrl);
	m_pTClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pTClientInfoTask->IpResolve(IPRESOLVE::V4);
	Http()->Run(m_pTClientInfoTask);
}

typedef std::tuple<int, int, int> TVersion;
static const TVersion gs_InvalidTCVersion = std::make_tuple(-1, -1, -1);

static TVersion ToTCVersion(char *pStr)
{
	int aVersion[3] = {0, 0, 0};
	const char *p = strtok(pStr, ".");

	for(int i = 0; i < 3 && p; ++i)
	{
		if(!str_isallnum(p))
			return gs_InvalidTCVersion;

		aVersion[i] = str_toint(p);
		p = strtok(NULL, ".");
	}

	if(p)
		return gs_InvalidTCVersion;

	return std::make_tuple(aVersion[0], aVersion[1], aVersion[2]);
}

void CTClient::FinishTClientInfo()
{
	json_value *pJson = m_pTClientInfoTask->ResultJson();
	if(!pJson)
		return;
	const json_value &Json = *pJson;
	const json_value &CurrentVersion = Json["version"];

	if(CurrentVersion.type == json_string)
	{
		char aNewVersionStr[64];
		str_copy(aNewVersionStr, CurrentVersion);
		char aCurVersionStr[64];
		str_copy(aCurVersionStr, TCLIENT_VERSION);
		if(ToTCVersion(aNewVersionStr) > ToTCVersion(aCurVersionStr))
		{
			str_copy(m_aVersionStr, CurrentVersion);
		}
		else
		{
			m_aVersionStr[0] = '0';
			m_aVersionStr[1] = '\0';
		}
		m_FetchedTClientInfo = true;
	}

	json_value_free(pJson);
}

void CTClient::SetForcedAspect()
{
	// TODO: Fix flashing on windows
	int State = Client()->State();
	bool Force = true;
	if(g_Config.m_TcAllowAnyRes == 0)
		;
	else if(State == CClient::EClientState::STATE_DEMOPLAYBACK)
		Force = false;
	else if(State == CClient::EClientState::STATE_ONLINE && GameClient()->m_GameInfo.m_AllowZoom && !GameClient()->m_Menus.IsActive())
		Force = false;
	Graphics()->SetForcedAspect(Force);
}

void CTClient::OnStateChange(int OldState, int NewState)
{
	SetForcedAspect();
	for(auto &AirRescuePositions : m_aAirRescuePositions)
		AirRescuePositions = {};
}

void CTClient::OnNewSnapshot()
{
	SetForcedAspect();
	// Update volleyball
	bool IsVolleyBall = false;
	if(g_Config.m_TcVolleyBallBetterBall > 0 && g_Config.m_TcVolleyBallBetterBallSkin[0] != '\0')
	{
		if(g_Config.m_TcVolleyBallBetterBall > 1)
			IsVolleyBall = true;
		else
			IsVolleyBall = str_startswith_nocase(Client()->GetCurrentMap(), "volleyball");
	};
	for(auto &Client : GameClient()->m_aClients)
	{
		Client.m_IsVolleyBall = IsVolleyBall && Client.m_DeepFrozen;
	}
	// Update air rescue
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int ClientId = GameClient()->m_aLocalIds[Dummy];
			if(ClientId == -1)
				continue;
			const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
			if(!Char.m_Active)
				continue;
			if(Client()->GameTick(Dummy) % 10 != 0) // Works for both 25tps and 50tps
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(Client.m_FreezeEnd == -1) // You aren't safe when frozen
				continue;
			const vec2 NewPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
			// If new pos is under 2 tiles from old pos, don't record a new position
			if(!m_aAirRescuePositions[Dummy].empty())
			{
				const vec2 OldPos = m_aAirRescuePositions[Dummy].front();
				if(distance(NewPos, OldPos) < 64.0f)
					continue;
			}
			if(m_aAirRescuePositions[Dummy].size() >= 256)
				m_aAirRescuePositions[Dummy].pop_back();
			m_aAirRescuePositions[Dummy].push_front(NewPos);
		}
	}
}

constexpr const char STRIP_CHARS[] = {'-', '=', '+', '_', ' '};
static bool IsStripChar(char c)
{
	return std::any_of(std::begin(STRIP_CHARS), std::end(STRIP_CHARS), [c](char s) {
		return s == c;
	});
}

static void StripStr(const char *pIn, char *pOut, const char *pEnd)
{
	if(!pIn)
	{
		*pOut = '\0';
		return;
	}

	while(*pIn && IsStripChar(*pIn))
		pIn++;

	// Special behaviour for empty checkbox
	if((unsigned char)*pIn == 0xE2 && (unsigned char)(*(pIn + 1)) == 0x98 && (unsigned char)(*(pIn + 2)) == 0x90)
	{
		pIn += 3;
		while(*pIn && IsStripChar(*pIn))
			pIn++;
	}

	char *pLastValid = nullptr;
	while(*pIn && pOut < pEnd - 1)
	{
		*pOut = *pIn;
		if(!IsStripChar(*pIn))
			pLastValid = pOut;
		pIn++;
		pOut++;
	}

	if(pLastValid)
		*(pLastValid + 1) = '\0';
	else
		*pOut = '\0';
}

void CTClient::RenderMiniVoteHud()
{
	CUIRect View = {0.0f, 60.0f, 70.0f, 35.0f};
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_R, 3.0f);
	View.Margin(3.0f, &View);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = View.w;

	CUIRect Row, LeftColumn, RightColumn, ProgressSpinner;
	char aBuf[256];

	// Vote description
	View.HSplitTop(6.0f, &Row, &View);
	StripStr(GameClient()->m_Voting.VoteDescription(), aBuf, aBuf + sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, 6.0f, TEXTALIGN_ML, Props);

	// Vote reason
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(4.0f, &Row, &View);
	Ui()->DoLabel(&Row, GameClient()->m_Voting.VoteReason(), 4.0f, TEXTALIGN_ML, Props);

	// Time left
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), GameClient()->m_Voting.SecondsLeft());
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	Row.VSplitLeft(2.0f, nullptr, &Row);
	Row.VSplitLeft(3.0f, &ProgressSpinner, &Row);
	Row.VSplitLeft(2.0f, nullptr, &Row);

	SProgressSpinnerProperties ProgressProps;
	ProgressProps.m_Progress = std::clamp((time() - GameClient()->m_Voting.m_Opentime) / (float)(GameClient()->m_Voting.m_Closetime - GameClient()->m_Voting.m_Opentime), 0.0f, 1.0f);
	Ui()->RenderProgressSpinner(ProgressSpinner.Center(), ProgressSpinner.h / 2.0f, ProgressProps);

	Ui()->DoLabel(&Row, aBuf, 3.0f, TEXTALIGN_ML);

	// Bars
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	GameClient()->m_Voting.RenderBars(Row);

	// F3 / F4
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(0.5f, &Row, &View);
	Row.VSplitMid(&LeftColumn, &RightColumn, 4.0f);

	char aKey[64];
	GameClient()->m_Binds.GetKey("vote yes", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == 1 ? ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&LeftColumn, aKey[0] == '\0' ? "yes" : aKey, 0.5f, TEXTALIGN_ML);

	GameClient()->m_Binds.GetKey("vote no", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == -1 ? ColorRGBA(0.95f, 0.25f, 0.25f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&RightColumn, aKey[0] == '\0' ? "no" : aKey, 0.5f, TEXTALIGN_MR);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CTClient::RenderCenterLines()
{
	if(g_Config.m_TcShowCenter <= 0)
		return;

	if(GameClient()->m_Scoreboard.IsActive())
		return;

	Graphics()->TextureClear();

	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const float XMid = (X0 + X1) / 2.0f;
	const float YMid = (Y0 + Y1) / 2.0f;

	if(g_Config.m_TcShowCenterWidth == 0)
	{
		Graphics()->LinesBegin();
		IGraphics::CLineItem aLines[2] = {
			{XMid, Y0, XMid, Y1},
			{X0, YMid, X1, YMid}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->LinesDraw(aLines, std::size(aLines));
		Graphics()->LinesEnd();
	}
	else
	{
		const float W = g_Config.m_TcShowCenterWidth;
		Graphics()->QuadsBegin();
		IGraphics::CQuadItem aQuads[3] = {
			{XMid, mix(Y0, Y1, 0.25f) - W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{XMid, mix(Y0, Y1, 0.75f) + W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{XMid, YMid, X1 - X0, W}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->QuadsDraw(aQuads, std::size(aQuads));
		Graphics()->QuadsEnd();
	}
}

void CTClient::RenderCtfFlag(vec2 Pos, float Alpha)
{
	// from CItems::RenderFlag
	float Size = 42.0f;
	int QuadOffset;
	if(g_Config.m_TcFakeCtfFlags == 1)
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);
		QuadOffset = GameClient()->m_Items.m_RedFlagOffset;
	}
	else
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
		QuadOffset = GameClient()->m_Items.m_BlueFlagOffset;
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->RenderQuadContainerAsSprite(GameClient()->m_Items.m_ItemsQuadContainerIndex, QuadOffset, Pos.x, Pos.y - Size * 0.75f);
}

void CTClient::OnUpdate()
{
	bool MustSendCustomClient = false;

	for(auto &Client : GameClient()->m_aClients)
	{
		if(Client.m_Active)
		{
			if(Client.ClientId() == GameClient()->m_Snap.m_LocalClientId || Client.ClientId() == GameClient()->GetPredictedDummyId())
			{
				m_aClientData[Client.ClientId()].m_CustomClient = CUSTOM_CLIENT_ID_TATER; //force tater client for us
			}

			if(!m_aClientData[Client.ClientId()].m_SentCustomClient)
			{
				MustSendCustomClient = true;
				m_aClientData[Client.ClientId()].m_SentCustomClient = true;
			}
		}
		else
		{
			m_aClientData[Client.ClientId()].m_SentCustomClient = false;
		}
	}

	if(MustSendCustomClient)
	{
		m_SendingCustomClientTicks = 25;
	}

	switch(m_SendingCustomClientTicks)
	{
	case 25:
		GameClient()->SendInfo(false);
		GameClient()->SendDummyInfo(false);
		m_SendingCustomClientTicks = 24;
		break;
	case 0:
		GameClient()->SendInfo(false);
		GameClient()->SendDummyInfo(false);
		m_SendingCustomClientTicks = -1;
		break;
	default:
		if(m_SendingCustomClientTicks > 0)
			m_SendingCustomClientTicks--;
		break;
	}
}

void CTClient::OnReset()
{
	for(auto &ClientData : m_aClientData)
	{
		ClientData.Reset();
	}

	m_SendingCustomClientTicks = 25;
}

void CTClient::CTCClientData::Reset()
{
	m_CustomClient = 0;
	m_SentCustomClient = false;
}

// function originally from Kaizo Network by +KZ, credit if used
int CTClient::ReplaceCountryFlagWithCustomClientId(int Country)
{
	if(!g_Config.m_TcSendClientType)
		return Country;

	if(m_SendingCustomClientTicks <= 1) //dont send custom flag
		return Country;

	//if some random day amount of flags conflicts with invalid flag, just send normal country
	if(GameClient()->m_CountryFlags.Num() >= MINIMUM_CUSTOM_CLIENT_ID)
	{
		return Country;
	}

	return CUSTOM_CLIENT_ID_TATER;
}

// function originally from Kaizo Network by +KZ, credit if used
bool CTClient::IsCustomClientId(int Country)
{
	return (size_t)Country > GameClient()->m_CountryFlags.Num();
}

// code originally from Kaizo Network by +KZ, credit if used
int CTClient::HandleClientCountry(int Country, int ClientId)
{
	if(IsCustomClientId(Country)) //if it is a custom client id, set custom client id and keep country
	{
		m_aClientData[ClientId].m_CustomClient = Country;
		return GameClient()->m_aClients[ClientId].m_Country;
	}
	else //otherwise, set country
	{
		return Country;
	}
}
