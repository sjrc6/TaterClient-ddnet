#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/render.h>
#include <game/generated/client_data.h>
#include <game/generated/protocol.h>

#include <game/client/gameclient.h>

#include "bg_draw.h"
#include "base/vmath.h"
#include "engine/client/enums.h"

void CBgDraw::ConBgDraw(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->m_aInputData[g_Config.m_ClDummy] = (bool)pResult->GetInteger(0);
}

void CBgDraw::ConBgDrawReset(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Reset();
}

void CBgDraw::OnConsoleInit()
{
	Console()->Register("+bg_draw", "", CFGFLAG_CLIENT, ConBgDraw, this, "Draw on the in game background");
	Console()->Register("bg_draw_reset", "", CFGFLAG_CLIENT, ConBgDrawReset, this, "Reset all drawings on the background");
}

void CBgDraw::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	Graphics()->TextureClear();
	GameClient()->RenderTools()->MapScreenToGroup(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, GameClient()->Layers()->GameGroup(), GameClient()->m_Camera.m_Zoom);

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	auto QuadContainerAddCircle = [&](CBgDrawStroke &Stroke, vec2 Pos, float Angle1, float Angle2)
	{
		const float Width = (float)g_Config.m_ClBgDrawWidth * GameClient()->m_Camera.m_Zoom;
		const float Radius = Width / 2.0f;
		const int Segments = (int)(std::fabs(Angle2 - Angle1) / (pi * 2.0f) * 20.0f);
		const float SegmentAngle = (Angle2 - Angle1) / Segments;
		IGraphics::CFreeformItem FreeformItems[20];
		for(int i = 0; i < Segments; ++i)
		{
			const float A1 = Angle1 + (float)i * SegmentAngle;
			const float A2 = A1 + SegmentAngle;
			const vec2 P1 = Pos + direction(A1) * Radius;
			const vec2 P2 = Pos + direction(A2) * Radius;
			FreeformItems[i] = IGraphics::CFreeformItem(P1.x, P1.y, P2.x, P2.y, Pos.x, Pos.y, Pos.x, Pos.y);
		}
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClBgDrawColor)));
		Graphics()->QuadContainerAddQuads(Stroke.m_QuadContainerIndex, FreeformItems, Segments);
		if(Stroke.m_QuadCount == 0)
			Stroke.m_BoundingBoxMin = Stroke.m_BoundingBoxMin = Pos;
		Stroke.m_QuadCount += Segments;
		Stroke.m_SecondsAge = 0.0f;
	};
	auto QuadContainerAddLine = [&](CBgDrawStroke &Stroke, vec2 Pos) {
		const float Width = (float)g_Config.m_ClBgDrawWidth * GameClient()->m_Camera.m_Zoom;
		const float Angle = angle(Stroke.m_LastPos - Pos);
		const vec2 Offset = direction(Angle + pi / 2.0f) * (Width / 2.0f);
		const vec2 P1 = Stroke.m_LastPos + Offset;
		const vec2 P2 = Stroke.m_LastPos - Offset;
		const vec2 P3 = Pos + Offset;
		const vec2 P4 = Pos - Offset;
		IGraphics::CFreeformItem FreeformItem(P1.x, P1.y, P2.x, P2.y, P3.x, P3.y, P4.x, P4.y);
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClBgDrawColor)));
		Graphics()->QuadContainerAddQuads(Stroke.m_QuadContainerIndex, &FreeformItem, 1);
		Stroke.m_QuadCount += 1;
		if(Stroke.m_QuadCount == 1)
		{
			Stroke.m_BoundingBoxMin = Stroke.m_BoundingBoxMin = Pos;
			// Start cap
			QuadContainerAddCircle(Stroke, Stroke.m_LastPos, Angle + pi / 2.0f, Angle - pi / 2.0f);
		}
		else
		{
			if(Pos.x < Stroke.m_BoundingBoxMin.x)
				Stroke.m_BoundingBoxMin.x = Pos.x;
			else if(Pos.x > Stroke.m_BoundingBoxMax.x)
				Stroke.m_BoundingBoxMax.x = Pos.x;
			if(Pos.y < Stroke.m_BoundingBoxMin.y)
				Stroke.m_BoundingBoxMin.y = Pos.y;
			else if(Pos.y > Stroke.m_BoundingBoxMax.y)
				Stroke.m_BoundingBoxMax.y = Pos.y;
			// Lazy bevel
			QuadContainerAddCircle(Stroke, Stroke.m_LastPos, 0.0f, pi * 2.0f);
		}
		Stroke.m_LastPos = Pos;
		Stroke.m_SecondsAge = 0.0f;
	};

	float aDelta[NUM_DUMMIES];

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		const int Tick = Client()->GameTick(Dummy);
		aDelta[Dummy] = (float)(Tick - m_aLastTick[Dummy]) / (float)Client()->GameTickSpeed();
		m_aLastTick[Dummy] = Tick;
		const vec2 Pos = (m_pClient->m_Controls.m_aTargetPos[Dummy] - GameClient()->m_Camera.m_Center)
			* GameClient()->m_Camera.m_Zoom
			+ GameClient()->m_Camera.m_Center;
		const bool &Pressed = m_aInputData[Dummy];
		int &Id = m_aStrokeIds[Dummy];
		if(!Pressed)
		{
			if(Id != -1)
			{
				CBgDrawStroke &Stroke = m_aStrokes[Id];
				const float Distance = distance(Stroke.m_LastPos, Pos);
				const float Width = (float)g_Config.m_ClBgDrawWidth * GameClient()->m_Camera.m_Zoom;
				if(Stroke.m_QuadCount == 0 && Distance < Width)
				{
					// Add just a circle
					QuadContainerAddCircle(Stroke, Pos, 0.0f, pi * 2.0f);
				}
				else
				{
					const float Angle = angle(Stroke.m_LastPos - Pos);
					QuadContainerAddLine(Stroke, Pos);
					// End cap
					QuadContainerAddCircle(Stroke, Pos, Angle + pi / 2.0f, Angle + pi * 1.5f);
				}
				Id = -1;
			}
			continue;
		}
		if(Id == -1)
		{
			Id = m_aStrokeId;
			Graphics()->QuadContainerReset(m_aStrokes[Id].m_QuadContainerIndex);
			m_aStrokes[Id].m_QuadCount = 0;
			m_aStrokes[Id].m_Dummy = Dummy;
			m_aStrokes[Id].m_LastPos = Pos;
			m_aStrokeId = (m_aStrokeId + 1) % BG_DRAW_MAX_STROKES;
		}
		CBgDrawStroke &Stroke = m_aStrokes[Id];
		const float Distance = distance(Stroke.m_LastPos, Pos);
		const float Width = (float)g_Config.m_ClBgDrawWidth * GameClient()->m_Camera.m_Zoom;
		if(Distance < Width * 2.0f)
			continue;
		QuadContainerAddLine(Stroke, Pos);
	}
	for(int i = 0; i < BG_DRAW_MAX_STROKES; ++i)
	{
		int Id = m_aStrokeId - i;
		if(Id < 0)
			Id += BG_DRAW_MAX_STROKES;
		CBgDrawStroke &Stroke = m_aStrokes[Id];
		if(Stroke.m_QuadCount == 0)
			continue;
		Stroke.m_SecondsAge += aDelta[Stroke.m_Dummy];
		if(g_Config.m_ClBgDrawFadeTime > 0 && Stroke.m_SecondsAge > (float)g_Config.m_ClBgDrawFadeTime)
		{
			Graphics()->QuadContainerReset(Stroke.m_QuadContainerIndex);
			Stroke.m_QuadCount = 0;
			continue;
		}
		if(Stroke.m_BoundingBoxMin.x + 5.0f < ScreenX0 && Stroke.m_BoundingBoxMax.x - 5.0f > ScreenX1)
			continue;
		if(Stroke.m_BoundingBoxMin.y + 5.0f < ScreenY0 && Stroke.m_BoundingBoxMin.y - 5.0f > ScreenY1)
			continue;
		Graphics()->RenderQuadContainer(Stroke.m_QuadContainerIndex, Stroke.m_QuadCount);
	}
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

void CBgDraw::Reset()
{
	m_aStrokeId = 0;
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		m_aLastTick[Dummy] = Client()->GameTick(Dummy);
		m_aInputData[Dummy] = false;
		m_aStrokeIds[Dummy] = -1;
	}
	for(CBgDrawStroke &Stroke : m_aStrokes)
	{
		Graphics()->QuadContainerReset(Stroke.m_QuadContainerIndex);
		Stroke.m_QuadCount = 0;
	}
}

void CBgDraw::OnMapLoad()
{
	Reset();
}

void CBgDraw::OnInit()
{
	for(CBgDrawStroke &Stroke : m_aStrokes)
	{
		Stroke.m_QuadContainerIndex = Graphics()->CreateQuadContainer();
	}
	Reset();
}
