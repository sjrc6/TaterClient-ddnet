#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <base/system.h>

#include <game/generated/protocol.h>
#include <game/localization.h>

#include "mod.h"

static constexpr const float MOD_WEAPON_TIME = 1.5f;

class CMod::CIden
{
private:
	enum class EType
	{
		ID,
		ADDR,
		NAME,
		ERROR,
	};
	EType m_Type;
	int m_ClientId;
	std::string m_Content;

public:
	[[nodiscard]] std::string Printable() const
	{
		switch(m_Type)
		{
		case EType::ID:
			return "#" + m_Content;
		case EType::ADDR:
			return std::to_string(m_ClientId);
		case EType::NAME:
			return "'" + std::to_string(m_ClientId) + ": " + m_Content + "'";
		case EType::ERROR:
			dbg_assert(false, "Tried to get Printable of error Iden");
		default:
			dbg_break();
		}
	}
	[[nodiscard]] std::string RCon() const
	{
		switch(m_Type)
		{
		case EType::ID:
			return std::to_string(m_ClientId);
		case EType::ADDR:
			return m_Content;
		case EType::NAME:
			return std::to_string(m_ClientId);
		case EType::ERROR:
			dbg_assert(false, "Tried to get RCon of error Iden");
		default:
			dbg_break();
		}
	}
	[[nodiscard]] const char *Error() const
	{
		return m_Type == EType::ERROR ? m_Content.c_str() : nullptr;
	}
	CIden() = delete;
	enum class EParseMode
	{
		NAME,
		ID_OR_ADDR,
		ID,
	};
	CIden(CMod &Mod, const char *pStr, EParseMode Mode)
	{
		CGameClient &This = *Mod.GameClient();
		if(Mode == EParseMode::NAME)
		{
			for(const auto &Player : This.m_aClients)
				if(str_comp(pStr, Player.m_aName) == 0)
				{
					m_Type = EType::NAME;
					m_ClientId = Player.ClientId();
					m_Content = Player.m_aName;
					return;
				}
			for(const auto &Player : This.m_aClients)
				if(str_comp_nocase(pStr, Player.m_aName) == 0)
				{
					m_Type = EType::NAME;
					m_ClientId = Player.ClientId();
					m_Content = Player.m_aName;
					return;
				}
			for(const auto &Player : This.m_aClients)
				if(str_utf8_comp_confusable(pStr, Player.m_aName) == 0)
				{
					m_Type = EType::NAME;
					m_ClientId = Player.ClientId();
					m_Content = Player.m_aName;
					return;
				}
			m_Type = EType::ERROR;
			dbg_assert(pStr, "pStr is nullptr");
			dbg_assert(strlen(pStr) < 128, "pStr is too big");
			m_Content = "'" + std::string(pStr) + "' was not found";
			return;
		}
		int Id;
		if(str_toint(pStr, &Id))
		{
			if(Id < 0 || Id > (int)std::size(This.m_aClients))
			{
				m_Type = EType::ERROR;
				m_Content = "Id " + std::to_string(Id) + " is not in range 0 to " + std::to_string(std::size(This.m_aClients));
				return;
			}
			const auto &Player = This.m_aClients[Id];
			if(!Player.m_Active)
			{
				m_Type = EType::ERROR;
				m_Content = "Id " + std::to_string(Id) + " is not connected";
				return;
			}
			m_Type = EType::NAME;
			m_Content = Player.m_aName;
			m_ClientId = Id;
			return;
		}
		if(Mode == EParseMode::ID_OR_ADDR)
		{
			NETADDR Addr;
			if(net_addr_from_str(&Addr, pStr) == 0)
			{
				char aAddr[128];
				net_addr_str(&Addr, aAddr, sizeof(aAddr), false);
				if(net_addr_is_local(&Addr))
				{
					m_Type = EType::ERROR;
					m_Content = "'" + std::string(aAddr) + "' is a local address";
					return;
				}
				m_Type = EType::ADDR;
				m_Content = std::string(aAddr);
			}
			m_Type = EType::ERROR;
			m_Content = "'" + std::string(pStr) + "' is not a valid address or id";
		}
		else
		{
			m_Type = EType::ERROR;
			m_Content = "'" + std::string(pStr) + "' is not a valid id";
		}
	}
};

static int UnitLengthSeconds(char Unit)
{
	switch(Unit)
	{
	case 's':
	case 'S': return 1;
	case 'm':
	case 'M': return 60;
	case 'h':
	case 'H': return 60 * 60;
	case 'd':
	case 'D': return 60 * 60 * 24;
	default: return -1;
	}
}

int CMod::TimeFromStr(const char *pStr, char OutUnit)
{
	double Time = -1;
	char InUnit = OutUnit;
	std::sscanf(pStr, "%lf%c", &Time, &InUnit);
	if(Time < 0)
		return -1;
	int InUnitLength = UnitLengthSeconds(InUnit);
	if(InUnitLength < 0)
		return -1;
	int OutUnitLength = UnitLengthSeconds(OutUnit);
	if(OutUnitLength < 0)
		return -1;
	return std::round(Time * (float)InUnitLength / (float)OutUnitLength);
}

void CMod::Kill(const CMod::CIden &Iden, bool Silent)
{
	if(Iden.Error())
	{
		GameClient()->Echo(Iden.Error());
		return;
	}
	char aBuf[256];
	const std::string IdenRCon = Iden.RCon();
	const char *pIdenRCon = IdenRCon.c_str();
	if(Silent)
		str_format(aBuf, sizeof(aBuf), "set_team %s -1; set_team %s 0", pIdenRCon, pIdenRCon);
	else
		str_format(aBuf, sizeof(aBuf), "kill_pl %s", pIdenRCon);
	Client()->Rcon(aBuf);
	str_format(aBuf, sizeof(aBuf), "Killed %s", Iden.Printable().c_str());
	GameClient()->Echo(aBuf);
}

void CMod::Kick(const CMod::CIden &Iden, const char *pReason)
{
	if(Iden.Error())
	{
		GameClient()->Echo(Iden.Error());
		return;
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kick %s %s", Iden.RCon().c_str(), pReason);
	Client()->Rcon(aBuf);
	if(pReason[0] == '\0')
		str_format(aBuf, sizeof(aBuf), "Kicked %s", Iden.Printable().c_str());
	else
		str_format(aBuf, sizeof(aBuf), "Kicked %s (%s)", Iden.Printable().c_str(), pReason);
	GameClient()->Echo(aBuf);
}

void CMod::Ban(const CMod::CIden &Iden, const char *pTime, const char *pReason)
{
	if(Iden.Error())
	{
		GameClient()->Echo(Iden.Error());
		return;
	}
	char aBuf[256];
	const int Minutes = TimeFromStr(pTime, 'm');
	str_format(aBuf, sizeof(aBuf), "ban %s %d %s", Iden.RCon().c_str(), Minutes, pReason);
	Client()->Rcon(aBuf);
	if(pReason[0] == '\0')
		str_format(aBuf, sizeof(aBuf), "Banned %s for %d minutes", Iden.Printable().c_str(), Minutes);
	else
		str_format(aBuf, sizeof(aBuf), "Banned %s for %d minutes (%s)", Iden.Printable().c_str(), Minutes, pReason);
	GameClient()->Echo(aBuf);
}

void CMod::Mute(const CMod::CIden &Iden, const char *pTime, const char *pReason)
{
	if(Iden.Error())
	{
		GameClient()->Echo(Iden.Error());
		return;
	}
	char aBuf[256];
	const int Seconds = TimeFromStr(pTime, 'm');
	str_format(aBuf, sizeof(aBuf), "muteid %s %d %s", Iden.RCon().c_str(), Seconds, pReason);
	Client()->Rcon(aBuf);
	if(pReason[0] == '\0')
		str_format(aBuf, sizeof(aBuf), "Muted %s for %d seconds", Iden.Printable().c_str(), Seconds);
	else
		str_format(aBuf, sizeof(aBuf), "Muted %s for %d seconds (%s)", Iden.Printable().c_str(), Seconds, pReason);
	GameClient()->Echo(aBuf);
}

void CMod::OnConsoleInit()
{
	auto RegisterModCommand = [&](const char *pName, const char *pParams, const char *pHelp, void (*FCallback)(IConsole::IResult *, void *)) {
		Console()->Register(pName, pParams, CFGFLAG_CLIENT, (CConsole::FCommandCallback)FCallback, this, pHelp);
	};

	RegisterModCommand("mod_rcon_ban", "s[id|ip] s[time (minutes)] ?r[reason]", "RCon ban someone", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		This.Ban(CIden(This, pResult->GetString(0), CIden::EParseMode::ID_OR_ADDR), pResult->GetString(1), pResult->GetString(2));
	});
	RegisterModCommand("mod_rcon_ban_name", "s[name] s[time (minutes)] ?r[reason]", "RCon ban someone by name", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		This.Ban(CIden(This, pResult->GetString(0), CIden::EParseMode::NAME), pResult->GetString(1), pResult->GetString(2));
	});

	RegisterModCommand("mod_rcon_kick", "s[id|ip] ?r[reason]", "RCon kick someone", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		This.Kick(CIden(This, pResult->GetString(0), CIden::EParseMode::ID), pResult->GetString(2));
	});
	RegisterModCommand("mod_rcon_kick_name", "s[name] ?r[reason]", "RCon kick someone by name", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		This.Kick(CIden(This, pResult->GetString(0), CIden::EParseMode::NAME), pResult->GetString(2));
	});

	RegisterModCommand("mod_rcon_mute", "s[id] s[time (minutes)] ?r[reason]", "RCon mute someone", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		This.Mute(CIden(This, pResult->GetString(0), CIden::EParseMode::ID), pResult->GetString(1), pResult->GetString(2));
	});
	RegisterModCommand("mod_rcon_mute_name", "s[name] s[time (minutes)] ?r[reason]", "RCon mute someone by name", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		This.Mute(CIden(This, pResult->GetString(0), CIden::EParseMode::NAME), pResult->GetString(1), pResult->GetString(2));
	});

	RegisterModCommand("mod_rcon_kill", "s[id/ip] ?s[2] ?s[3] ?s[4] ?s[5] ?s[6] ?s[7] ?s[8]", "RCon kill people", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		for(int i = 0; i < 8; ++i)
			if(pResult->GetString(i)[0] != '\0')
				This.Kill(CIden(This, pResult->GetString(i), CIden::EParseMode::ID), true);
	});
	RegisterModCommand("mod_rcon_kill_name", "s[name] ?s[2] ?s[3] ?s[4] ?s[5] ?s[6] ?s[7] ?s[8]", "RCon kill people by name", [](IConsole::IResult *pResult, void *pUserData) {
		CMod &This = *(CMod *)pUserData;
		for(int i = 0; i < 8; ++i)
			if(pResult->GetString(i)[0] != '\0')
				This.Kill(CIden(This, pResult->GetString(i), CIden::EParseMode::NAME), true);
	});

	Console()->Chain(
		"+fire", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CMod *)pUserData)->OnFire(pResult->GetInteger(0));
		},
		this);
}

void CMod::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	GameClient()->RenderTools()->MapScreenToGroup(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, GameClient()->Layers()->GameGroup(), GameClient()->m_Camera.m_Zoom);

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	Graphics()->TextureClear();

	// Mod Weapon
	if(Client()->State() == IClient::STATE_ONLINE && m_ModWeaponActiveId >= 0 && m_ModWeaponActiveTimeLeft > 0.0f)
	{
		const auto &Player = GameClient()->m_aClients[m_ModWeaponActiveId];
		if(!Player.m_Active || g_Config.m_ClModWeaponCommand[0] == '\0') // Cancel if not active or empty command
		{
			m_ModWeaponActiveId = -1;
		}
		else
		{
			const float Delta = Client()->RenderFrameTime();
			if(Delta < 1.0f / 30.0f) // Don't do anything if lagging
			{
				m_ModWeaponActiveTimeLeft -= Delta;
				if(m_ModWeaponActiveTimeLeft <= 0.0f)
				{
					m_ModWeaponActiveTimeLeft = 0.0f;
					ModWeapon(m_ModWeaponActiveId);
					m_ModWeaponActiveId = -1;
				}
			}
			float Y = Player.m_RenderPos.y + 20.0f;
			{
				char aBuf[32];
				str_format(aBuf, sizeof(aBuf), "%.2f", m_ModWeaponActiveTimeLeft);
				STextContainerIndex TextContainer;
				TextContainer.Reset();
				CTextCursor Cursor;
				TextRender()->SetCursor(&Cursor, 0.0f, 0.0f, 25.0f, TEXTFLAG_RENDER);
				TextRender()->SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_ONE_TIME_USE);
				TextRender()->CreateTextContainer(TextContainer, &Cursor, aBuf);
				TextRender()->SetRenderFlags(0);
				if(TextContainer.Valid())
				{
					const auto Color = color_cast<ColorRGBA>(ColorHSLA(m_ModWeaponActiveTimeLeft / MOD_WEAPON_TIME, 0.5f, 0.5f, 1.0f));
					const auto BoundingBox = TextRender()->GetBoundingBoxTextContainer(TextContainer);
					TextRender()->RenderTextContainer(TextContainer,
						Color, TextRender()->DefaultTextOutlineColor(),
						Player.m_RenderPos.x - BoundingBox.m_W / 2.0f, Y);
					Y += BoundingBox.m_H + 15.0f;
				}
				TextRender()->DeleteTextContainer(TextContainer);
			}
			{
				STextContainerIndex TextContainer;
				TextContainer.Reset();
				CTextCursor Cursor;
				TextRender()->SetCursor(&Cursor, 0.0f, 0.0f, 15.0f, TEXTFLAG_RENDER);
				TextRender()->SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_ONE_TIME_USE);
				TextRender()->CreateTextContainer(TextContainer, &Cursor, g_Config.m_ClModWeaponCommand);
				TextRender()->SetRenderFlags(0);
				if(TextContainer.Valid())
				{
					const auto BoundingBox = TextRender()->GetBoundingBoxTextContainer(TextContainer);
					TextRender()->RenderTextContainer(TextContainer,
						ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), TextRender()->DefaultTextOutlineColor(),
						Player.m_RenderPos.x - BoundingBox.m_W / 2.0f, Y);
					Y += BoundingBox.m_H + 15.0f;
				}
				TextRender()->DeleteTextContainer(TextContainer);
			}
		}
	}

	// Hitboxes
	if(g_Config.m_ClShowPlayerHitBoxes > 0)
	{
		auto RenderHitbox = [&](vec2 Position, float Alpha) {
			if(Alpha <= 0.0f)
				return;
			const float RadiusInner = 16.0f;
			const float RadiusOuter = 30.0f;
			Graphics()->QuadsBegin();
			Graphics()->SetColor(ColorRGBA(0.0f, 1.0f, 0.0f, 0.2f * Alpha));
			Graphics()->DrawCircle(Position.x, Position.y, RadiusInner, 20);
			Graphics()->DrawCircle(Position.x, Position.y, RadiusOuter, 20);
			Graphics()->QuadsEnd();
			IEngineGraphics::CLineItem aLines[] = {
				{Position.x, Position.y - RadiusOuter, Position.x, Position.y + RadiusOuter},
				{Position.x - RadiusOuter, Position.y, Position.x + RadiusOuter, Position.y},
			};
			Graphics()->LinesBegin();
			Graphics()->SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 0.8f * Alpha));
			Graphics()->LinesDraw(aLines, std::size(aLines));
			Graphics()->LinesEnd();
		};

		for(const auto &Player : GameClient()->m_aClients)
		{
			const int ClientId = Player.ClientId();
			const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
			if(!Char.m_Active || !Player.m_Active)
				continue;
			if(Player.m_Team < 0)
				continue;

			if(!(in_range(Player.m_RenderPos.x, ScreenX0, ScreenX1) && in_range(Player.m_RenderPos.y, ScreenY0, ScreenY1)))
				continue;

			float Alpha = 1.0f;
			if(GameClient()->IsOtherTeam(ClientId))
				Alpha *= (float)g_Config.m_ClShowOthersAlpha / 100.0f;

			RenderHitbox(Player.m_RenderPos, Alpha);

			if(g_Config.m_ClShowPlayerHitBoxes > 1)
			{
				// From CPlayers::RenderPlayer
				vec2 ShadowPosition = mix(
					vec2(GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev.m_X, GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev.m_Y),
					vec2(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y),
					Client()->IntraGameTick(g_Config.m_ClDummy));
				RenderHitbox(ShadowPosition, Alpha * 0.75f);
			}
		}
	}
}

void CMod::OnStateChange(int OldState, int NewState)
{
	m_ModWeaponActiveId = -1;
	m_ModWeaponActiveTimeLeft = -1.0f;
}

void CMod::ModWeapon(int Id)
{
	char aBuf[256];

	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
		return;

	str_format(aBuf, sizeof(aBuf), TCLocalize("Activating mod weapon on %d: %s\n"), Player.ClientId(), Player.m_aName);
	GameClient()->Echo(aBuf);

	class CResultModFire : public CConsole::IResult
	{
	public:
		const char *m_pBuf;
		CResultModFire(const char *pBuf) :
			IResult(0), m_pBuf(pBuf) {}
		int NumArguments() const
		{
			return 1;
		}
		const char *GetString(unsigned Index) const override
		{
			if(Index == 0)
				return m_pBuf;
			return "";
		}
		int GetInteger(unsigned Index) const override { return 0; };
		float GetFloat(unsigned Index) const override { return 0.0f; };
		std::optional<ColorHSLA> GetColor(unsigned Index, float DarkestLighting) const override { return std::nullopt; };
		void RemoveArgument(unsigned Index) override{};
		int GetVictim() const override { return -1; };
	};

	str_format(aBuf, sizeof(aBuf), "%d", Id);
	CResultModFire ResultModFire(aBuf);
	GameClient()->m_Conditional.m_pResult = &ResultModFire;
	Console()->ExecuteLine(g_Config.m_ClModWeaponCommand);
	GameClient()->m_Conditional.m_pResult = nullptr;
}

void CMod::OnFire(bool Pressed)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!Pressed)
	{
		m_ModWeaponActiveId = -1;
		return;
	}
	if(m_ModWeaponActiveId >= 0)
		return;
	if(g_Config.m_ClModWeapon == -1)
		return;
	if(!Client()->RconAuthed())
		return;
	const auto &Player = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId];
	if(Player.m_RenderPrev.m_Weapon != g_Config.m_ClModWeapon)
		return;
	if(!Player.m_Active)
		return;
	// Find person who we have shot
	const CGameClient::CClientData *pBestClient = nullptr;
	float BestClientScore = -INFINITY;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active || Player.m_Team == TEAM_SPECTATORS)
	{
		const vec2 Pos = GameClient()->m_Camera.m_Center;
		for(const CGameClient::CClientData &Other : GameClient()->m_aClients)
		{
			if(!Other.m_Active || !GameClient()->m_Snap.m_aCharacters[Other.ClientId()].m_Active)
				continue;
			const float PosDelta = distance(Other.m_RenderPos, Pos);
			const float MaxRange = 100.0f;
			if(PosDelta > MaxRange)
				continue;
			const float Score = MaxRange - PosDelta;
			if(Score > BestClientScore)
			{
				BestClientScore = Score;
				pBestClient = &Other;
			}
		}
	}
	else
	{
		const vec2 Pos = Player.m_RenderPos;
		const vec2 Angle = normalize(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy]);
		for(const CGameClient::CClientData &Other : GameClient()->m_aClients)
		{
			if(!Other.m_Active || !GameClient()->m_Snap.m_aCharacters[Other.ClientId()].m_Active || Player.ClientId() == Other.ClientId() || GameClient()->IsOtherTeam(Other.ClientId()))
				continue;
			const float PosDelta = distance(Other.m_RenderPos, Pos);
			const float MaxRange = (g_Config.m_ClModWeapon == 0 ? 100.0f : 750.0f);
			if(PosDelta > MaxRange)
				continue;
			const float AngleDelta = dot(normalize(Other.m_RenderPos - Pos), Angle);
			if(AngleDelta < 0.9f)
				continue;
			const float Score = (AngleDelta - 1.0f) * 10.0f * MaxRange + (MaxRange - PosDelta);
			if(Score > BestClientScore)
			{
				BestClientScore = Score;
				pBestClient = &Other;
			}
		}
	}
	if(!pBestClient)
		return;

	m_ModWeaponActiveId = pBestClient->ClientId();
	m_ModWeaponActiveTimeLeft = MOD_WEAPON_TIME;
}
