/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_H

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

class CScoreboard : public CComponent
{
	struct CScoreboardRenderState
	{
		float m_TeamStartX;
		float m_TeamStartY;
		int m_CurrentDDTeamSize;

		CScoreboardRenderState() :
			m_TeamStartX(0), m_TeamStartY(0), m_CurrentDDTeamSize(0) {}
	};

	void RenderTitle(CUIRect TitleBar, int Team, const char *pTitle);
	void RenderGoals(CUIRect Goals);
	void RenderSpectators(CUIRect Spectators);
	void RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State);
	void RenderRecordingNotification(float x);

	static void ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData);
	const char *GetTeamName(int Team) const;

	void InviteButtonInitialize();
	void InviteButtonReset();
	bool ShouldShowInviteButton(int ClientId) const;
	void OnInviteButtonClick(int ClientId, const char *pPlayerName);
	void UpdateTeamStateTracking();
	void ResetInviteState(int ClientId = -1);
	bool IsGlobalInviteCooldownActive() const;
	bool IsServerJoinCooldownActive() const;
	bool IsLocalPlayerInActiveRun() const;
	bool IsLocalPlayerFrozen() const;
	bool ShouldDisableInteraction() const;
	int FindNextEmptyTeam() const;

	float m_InviteButtonWidth;
	float m_InviteButtonHeight;
	bool m_aInviteSent[64];
	
	int64_t m_GlobalInviteCooldownEnd;

	int m_LastLocalTeam;
	bool m_InAutoJoinSequence;

	bool m_aPlayerWasActive[64];
	
	int64_t m_ServerJoinTime;

	bool m_MouseModeWasAbsolute;

	bool m_Active;
	float m_ServerRecord;

	IGraphics::CTextureHandle m_DeadTeeTexture;

public:
	CScoreboard();
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	bool IsActive() const;
};

#endif
