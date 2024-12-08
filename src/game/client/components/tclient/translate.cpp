#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <atomic>

#include "translate.h"

class CTranslateBackendLibretranslate : public ITranslateBackend
{
	std::shared_ptr<CHttpRequest> m_pHttpRequest = nullptr;
	bool ParseResponse(const json_value *Obj, char *pOut, size_t Length)
	{
		if(Obj->type != json_object)
		{
			str_copy(pOut, "Response is not object", Length);
			return false;
		}

		const json_value *pError = json_object_get(Obj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(pOut, "Error is not string", Length);
			else
				str_format(pOut, Length, "Error from server: %s", pError->u.string);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(Obj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(pOut, "No translatedText", Length);
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(pOut, "translatedText is not string", Length);
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(Obj, "detectedLanguage");
		if(pDetectedLanguage != &json_value_none)
		{
			const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
			if(pConfidence != &json_value_none && (
				(pConfidence->type == json_double && pConfidence->u.dbl == 0.0) ||
				(pConfidence->type == json_integer && pConfidence->u.integer == 0)))
			{
				str_copy(pOut, "Language unknown, not detected or not installed", Length);
				return false;
			}
		}

		str_copy(pOut, pTranslatedText->u.string.ptr, Length);
		if(pDetectedLanguage != &json_value_none)
		{
			const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
			if(pLanguage != &json_value_none && pLanguage->type == json_string)
			{
				str_append(pOut, " (", Length);
				str_append(pOut, pLanguage->u.string.ptr, Length);
				str_append(pOut, ")", Length);
			}
		}

		return true;
	}

public:
	const char *Name() const override
	{
		return "libretranslate";
	}
	bool Update(bool &Success, char *pOut, size_t Length) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
		{
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(pOut, "Error while translating", Length);
			Success = false;
			return true;
		}
		
		{
		char *pOut;
		size_t _Length;
		m_pHttpRequest->Result((unsigned char**)&pOut, &_Length);
		printf("%.*s\n", (int)_Length, pOut);
		}

		const json_value *Obj = m_pHttpRequest->ResultJson();
		if(Obj == nullptr)
		{
			str_copy(pOut, "Error while parsing JSON", Length);
			Success = false;
			return true;
		}
		Success = ParseResponse(Obj, pOut, Length);
		m_pHttpRequest = nullptr;

		return true;
	}
	template <size_t N>
	CTranslateBackendLibretranslate(IHttp &Http, const char (&Text)[N])
	{
		char aBufQ[N * 2];
		char *pBufQ = aBufQ;
		char **pBufQDest = &pBufQ;
		str_escape(pBufQDest, Text, aBufQ + sizeof(aBufQ));

		static const char FORMAT[] = "{\"q\":\"%s\",\"source\":\"auto\",\"target\":\"%s\",\"format\":\"text\",\"api_key\":\"%s\"}";
		char aBuf[sizeof(FORMAT) + sizeof(aBufQ) + 128];
		const size_t aBufSize = str_format(aBuf, sizeof(aBuf), FORMAT, aBufQ, g_Config.m_ClTranslateTarget, g_Config.m_ClTranslateKey);

		auto pGet = std::make_shared<CHttpRequest>(g_Config.m_ClTranslateEndpoint);
		pGet->HeaderString("Content-Type", "application/json");
		pGet->Post((const unsigned char *)aBuf, aBufSize);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		printf("%s\n", aBuf);

		m_pHttpRequest = pGet;
		Http.Run(pGet);
	}
	~CTranslateBackendLibretranslate() override
	{
		m_pHttpRequest = nullptr;
	}
};

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if (pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::OnConsoleInit()
{
	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
}

CChat::CLine *CTranslate::FindMessage(const char *pName)
{
	// No messages at all
	if(GameClient()->m_Chat.m_CurrentLine < 0)
		return nullptr;
	if(!pName)
		return &GameClient()->m_Chat.m_aLines[GameClient()->m_Chat.m_CurrentLine];
	CChat::CLine *pLine;
	for(int i = GameClient()->m_Chat.m_CurrentLine; i >= 0; --i)
	{
		pLine = &GameClient()->m_Chat.m_aLines[i];
		if(pLine->m_Translated)
			continue;
		if(pLine->m_ClientId == CChat::CLIENT_MSG)
			continue;
		for(int Id : GameClient()->m_aLocalIds)
			if(pLine->m_ClientId == Id)
				continue;
		if(str_comp(pLine->m_aName, pName) == 0)
			return pLine;
	}
	for(int i = GameClient()->m_Chat.m_CurrentLine; i >= 0; --i)
	{
		pLine = &GameClient()->m_Chat.m_aLines[i];
		if(pLine->m_Translated)
			continue;
		if(pLine->m_ClientId == CChat::CLIENT_MSG)
			continue;
		for(int Id : GameClient()->m_aLocalIds)
			if(pLine->m_ClientId == Id)
				continue;
		if(str_comp_nocase(pLine->m_aName, pName) == 0)
			return pLine;
	}
	return nullptr;
}

void CTranslate::Translate(const char *pName)
{
	if(m_pBackend)
	{
		GameClient()->m_Chat.Echo("Currently translating!");
		return;
	}

	CChat::CLine *pLine = FindMessage(pName);
	if(!pLine || pLine->m_aText[0] == '\0')
	{
		GameClient()->m_Chat.Echo("No message to translate");
		return;
	}

	const char *pBackendString = g_Config.m_ClTranslateBackend;
	if(str_comp_nocase(pBackendString, "libretranslate") == 0)
		m_pBackend = new CTranslateBackendLibretranslate(*Http(), pLine->m_aText);
	else
	{
		GameClient()->m_Chat.Echo("Invalid backend translate");
		return;
	}

	pLine->m_Translated = true; // ignore this line in the future
	m_Line = *pLine; // copy, as may be deleted later
}

void CTranslate::OnRender()
{
	char aBuf[sizeof(CChat::CLine::m_aText)];
	if(!m_pBackend)
		return;
	bool Success = false;
	const bool Done = m_pBackend->Update(Success, aBuf, sizeof(aBuf));
	if(!Done)
		return;
	if(Success)
	{
		GameClient()->m_Chat.AddLine(m_Line.m_ClientId, m_Line.m_TeamNumber, aBuf);
		GameClient()->m_Chat.m_aLines[GameClient()->m_Chat.m_CurrentLine].m_Translated = true; // ignore this line in the future
	}
	else
		GameClient()->m_Chat.Echo(aBuf);
	m_pBackend = nullptr;
}
