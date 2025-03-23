#include <algorithm>
#include <atomic>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>

#include "translate.h"

static void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		unsigned char c = *(const unsigned char *)p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			if(OutPos >= Length - 1)
				break;
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

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
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(pOut, "No pDetectedLanguage", Length);
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(pOut, "pDetectedLanguage is not object", Length);
			return false;
		}

		const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
		if(pConfidence == &json_value_none || ((pConfidence->type == json_double && pConfidence->u.dbl == 0.0f) ||
							      (pConfidence->type == json_integer && pConfidence->u.integer == 0)))
		{
			str_copy(pOut, "Language unknown, not detected or not installed", Length);
			return false;
		}

		const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
		if(pLanguage == &json_value_none)
		{
			str_copy(pOut, "No language", Length);
			return false;
		}
		if(pLanguage->type != json_string)
		{
			str_copy(pOut, "language is not string", Length);
			return false;
		}

		str_copy(pOut, pTranslatedText->u.string.ptr, Length - 1);
		str_copy(pOut + strlen(pOut) + 1, pLanguage->u.string.ptr, Length - strlen(pOut) - 1);

		return true;
	}

public:
	const char *Name() const override
	{
		return "LibreTranslate";
	}
	std::optional<bool> Update(char *pOut, size_t Length) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
		{
			return std::nullopt;
		}
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(pOut, "Aborted", Length);
			m_pHttpRequest = nullptr;
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_format(pOut, Length, "Got http code %d", m_pHttpRequest->StatusCode());
			m_pHttpRequest = nullptr;
			return false;
		}

		const json_value *Obj = m_pHttpRequest->ResultJson();
		if(Obj == nullptr)
		{
			str_copy(pOut, "Error while parsing JSON", Length);
			m_pHttpRequest = nullptr;
			return false;
		}
		const bool Result = ParseResponse(Obj, pOut, Length);
		m_pHttpRequest = nullptr;

		return Result;
	}
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("target");
		Json.WriteStrValue(g_Config.m_ClTranslateTarget);
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		Json.WriteAttribute("api_key");
		Json.WriteStrValue(g_Config.m_ClTranslateKey);
		Json.EndObject();
		std::string &&JsonString = Json.GetOutputString();

		auto pGet = std::make_shared<CHttpRequest>(g_Config.m_ClTranslateEndpoint);
		pGet->LogProgress(HTTPLOG::NONE);
		pGet->HeaderString("Content-Type", "application/json");
		pGet->Post((const unsigned char *)JsonString.data(), JsonString.size());
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = pGet;
		Http.Run(pGet);
	}
	~CTranslateBackendLibretranslate() override
	{
		m_pHttpRequest = nullptr;
	}
};

class CTranslateBackendFtapi : public ITranslateBackend
{
	std::shared_ptr<CHttpRequest> m_pHttpRequest = nullptr;
	bool ParseResponse(const json_value *Obj, char *pOut, size_t Length)
	{
		if(Obj->type != json_object)
		{
			str_copy(pOut, "Response is not object", Length);
			m_pHttpRequest = nullptr;
			return false;
		}

		const json_value *pTranslatedText = json_object_get(Obj, "destination-text");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(pOut, "No destination-text", Length);
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(pOut, "destination-text is not string", Length);
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(Obj, "source-language");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(pOut, "No source-language", Length);
			return false;
		}
		if(pDetectedLanguage->type != json_string)
		{
			str_copy(pOut, "source-language is not string", Length);
			return false;
		}

		str_copy(pOut, pTranslatedText->u.string.ptr, Length - 1);
		str_copy(pOut + strlen(pOut) + 1, pDetectedLanguage->u.string.ptr, Length - strlen(pOut) - 1);

		return true;
	}

public:
	const char *Name() const override
	{
		return "FreeTranslateAPI";
	}
	std::optional<bool> Update(char *pOut, size_t Length) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
		{
			return std::nullopt;
		}
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(pOut, "Aborted", Length);
			m_pHttpRequest = nullptr;
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_format(pOut, Length, "Got http code %d", m_pHttpRequest->StatusCode());
			m_pHttpRequest = nullptr;
			return false;
		}

		const json_value *Obj = m_pHttpRequest->ResultJson();
		if(Obj == nullptr)
		{
			str_copy(pOut, "Error while parsing JSON", Length);
			m_pHttpRequest = nullptr;
			return false;
		}
		const bool Result = ParseResponse(Obj, pOut, Length);
		m_pHttpRequest = nullptr;

		return Result;
	}
	CTranslateBackendFtapi(IHttp &Http, const char *pText)
	{
		char aBuf[2048];
		str_format(aBuf, sizeof(aBuf), "%s/translate?dl=%s&text=",
			g_Config.m_ClTranslateEndpoint[0] ? g_Config.m_ClTranslateEndpoint : "https://ftapi.pythonanywhere.com",
			g_Config.m_ClTranslateTarget);

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		auto pGet = std::make_shared<CHttpRequest>(aBuf);
		pGet->LogProgress(HTTPLOG::NONE);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = pGet;
		Http.Run(pGet);
	}
	~CTranslateBackendFtapi() override
	{
		m_pHttpRequest = nullptr;
	}
};

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
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
	CChat::CLine *pLineBest = nullptr;
	int ScoreBest = -1;
	for(int i = 0; i < CChat::MAX_LINES; i++)
	{
		CChat::CLine *pLine = &GameClient()->m_Chat.m_aLines[((GameClient()->m_Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
		if(pLine->m_TranslateId.has_value())
			continue;
		if(pLine->m_ClientId == CChat::CLIENT_MSG)
			continue;
		if(pLine->m_ClientId == CChat::SERVER_MSG)
			continue;
		for(int Id : GameClient()->m_aLocalIds)
			if(pLine->m_ClientId == Id)
				continue;
		int Score = 0;
		if(pName)
		{
			if(str_comp(pLine->m_aName, pName) == 0)
				Score = 2;
			else if(str_comp_nocase(pLine->m_aName, pName) == 0)
				Score = 1;
			else
				Score = -1;
		}
		if(Score > ScoreBest)
		{
			ScoreBest = Score;
			pLineBest = pLine;
		}
	}
	return pLineBest;
}

static std::atomic<unsigned int> s_NextTranslateId = 0;

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	if(m_vJobs.size() > 10)
	{
		GameClient()->m_Chat.Echo("Too many ongoing translations!");
		return;
	}

	CChat::CLine *pLine = FindMessage(pName);
	if(!pLine || pLine->m_aText[0] == '\0')
	{
		GameClient()->m_Chat.Echo("No message to translate");
		return;
	}

	Translate(*pLine, ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress)
{
	CTranslateJob Job;
	Job.m_pLine = &Line;

	// Check to make sure the line is the same line we started with
	// Also thread safe for some reason
	Job.m_pTranslateId = std::atomic_fetch_add(&s_NextTranslateId, 1);
	Job.m_pLine->m_TranslateId = Job.m_pTranslateId;

	if(str_comp_nocase(g_Config.m_ClTranslateBackend, "libretranslate") == 0)
		Job.m_pBackend = new CTranslateBackendLibretranslate(*Http(), Job.m_pLine->m_aText);
	if(str_comp_nocase(g_Config.m_ClTranslateBackend, "ftapi") == 0)
		Job.m_pBackend = new CTranslateBackendFtapi(*Http(), Job.m_pLine->m_aText);
	else
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return;
	}

	if(ShowProgress)
	{
		str_format(Job.m_pLine->m_aTextTranslated, sizeof(Job.m_pLine->m_aTextTranslated), "[%s translating to %s]", Job.m_pBackend->Name(), g_Config.m_ClTranslateTarget);
		Job.m_pLine->m_Time = time();
		GameClient()->m_Chat.RebuildChat();
	}

	m_vJobs.emplace_back(std::move(Job));
}

void CTranslate::OnRender()
{
	auto Time = time();
	auto ForEach = [GameClient = GameClient(), Time](CTranslateJob &Job) {
		if(Job.m_pLine->m_TranslateId != Job.m_pTranslateId)
			return true; // Not the same line anymore
		char aBuf[sizeof(CChat::CLine::m_aText)];
		const std::optional<bool> Done = Job.m_pBackend->Update(aBuf, sizeof(aBuf));
		if(!Done.has_value())
			return false; // Keep ongoing tasks
		if(*Done)
		{
			// Check for no translation difference
			if(str_comp_nocase(Job.m_pLine->m_aText, aBuf) == 0)
				Job.m_pLine->m_aTextTranslated[0] = '\0';
			else
				str_format(Job.m_pLine->m_aTextTranslated, sizeof(Job.m_pLine->m_aTextTranslated), "%s [%s]", aBuf, aBuf + strlen(aBuf) + 1);
		}
		else
			str_format(Job.m_pLine->m_aTextTranslated, sizeof(Job.m_pLine->m_aTextTranslated), "[%s to %s failed: %s]", Job.m_pBackend->Name(), g_Config.m_ClTranslateTarget, aBuf);
		Job.m_pLine->m_Time = Time;
		GameClient->m_Chat.RebuildChat();
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());
}
