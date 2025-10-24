#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H

#include <engine/client/enums.h>
#include <engine/shared/console.h>
#include <engine/shared/http.h>

#include <game/client/component.h>

#include <deque>

class CTClient : public CComponent
{
	std::deque<vec2> m_aAirRescuePositions[NUM_DUMMIES];
	void AirRescue();
	static void ConAirRescue(IConsole::IResult *pResult, void *pUserData);

	static void ConCalc(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomTee(IConsole::IResult *pResult, void *pUserData);
	static void ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void RandomBodyColor();
	static void RandomFeetColor();
	static void RandomSkin(void *pUserData);
	static void RandomFlag(void *pUserData);

	static void ConSpecId(IConsole::IResult *pResult, void *pUserData);
	void SpecId(int ClientId);

	int m_EmoteCycle = 0;
	static void ConEmoteCycle(IConsole::IResult *pResult, void *pUserData);

	class IEngineGraphics *m_pGraphics = nullptr;

	char m_PreviousOwnMessage[2048] = {};

	bool SendNonDuplicateMessage(int Team, const char *pLine);

	float m_FinishTextTimeout = 0.0f;
	void DoFinishCheck();

	bool ServerCommandExists(const char *pCommand);

public:
	CTClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnRender() override;

	void OnStateChange(int OldState, int NewState) override;
	void OnNewSnapshot() override;
	void SetForcedAspect();

	std::shared_ptr<CHttpRequest> m_pTClientInfoTask = nullptr;
	void FetchTClientInfo();
	void FinishTClientInfo();
	void ResetTClientInfoTask();
	bool NeedUpdate();

	void RenderMiniVoteHud();
	void RenderCenterLines();
	void RenderCtfFlag(vec2 Pos, float Alpha);

	bool ChatDoSpecId(const char *pInput);
	bool InfoTaskDone() { return m_pTClientInfoTask && m_pTClientInfoTask->State() == EHttpState::DONE; }
	bool m_FetchedTClientInfo = false;
	char m_aVersionStr[10] = "0";

	void OnUpdate() override;
	void OnReset() override;

	class CTCClientData
	{
		public:
		int m_CustomClient = '\0';
		bool m_SentCustomClient = false;

		void Reset();

	} m_aClientData[MAX_CLIENTS];

	int ReplaceCountryFlagWithCustomClientId(int Country);
	bool IsCustomClientId(int Country);
	int m_SendingCustomClientTicks = -1;
	int HandleClientCountry(int Country, int ClientId);
};

#endif
