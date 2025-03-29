#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_BG_DRAW_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_BG_DRAW_H
#include <game/client/component.h>

#include <engine/client/enums.h>
#include <engine/console.h>

#define BG_DRAW_MAX_STROKES 128

class CBgDrawStroke
{
public:
	int m_Dummy;
	float m_SecondsAge;
	vec2 m_LastPos;
	int m_QuadContainerIndex;
	int m_QuadCount;
	vec2 m_BoundingBoxMin;
	vec2 m_BoundingBoxMax;
	CBgDrawStroke() = default;
};

class CBgDraw : public CComponent
{
private:
	int m_aLastTick[NUM_DUMMIES];
	bool m_aInputData[NUM_DUMMIES];
	int m_aStrokeIds[NUM_DUMMIES];
	CBgDrawStroke m_aStrokes[BG_DRAW_MAX_STROKES];
	int m_aStrokeId;
	static void ConBgDraw(IConsole::IResult *pResult, void *pUserData);
	static void ConBgDrawReset(IConsole::IResult *pResult, void *pUserData);
	void Reset();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnMapLoad() override;
	void OnInit() override;
};

#endif
