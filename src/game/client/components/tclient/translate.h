#ifndef GAME_CLIENT_COMPONENTS_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_TRANSLATE_H
#include <game/client/component.h>
#include <optional>
#include <vector>

class CTranslate;

class ITranslateBackend
{
public:
	virtual ~ITranslateBackend(){};
	virtual const char *Name() const = 0;
	// "[translated message]\0[guessed language code]"
	virtual std::optional<bool> Update(char *pOut, size_t Length) = 0;
	virtual const char *Url() { return nullptr; }
};

class CTranslate : public CComponent
{
	class CTranslateJob
	{
	public:
		ITranslateBackend *m_pBackend = nullptr;
		// For chat translations
		CChat::CLine *m_pLine = nullptr;
		unsigned int m_pTranslateId;
	};
	std::vector<CTranslateJob> m_vJobs;

	static void ConTranslate(IConsole::IResult *pResult, void *pUserData);

	CChat::CLine *FindMessage(const char *pName);

public:
	virtual int Sizeof() const override { return sizeof(*this); }

	virtual void OnConsoleInit() override;
	virtual void OnRender() override;

	void Translate(const char *pName, bool ShowProgress = true);
	void Translate(CChat::CLine &Line, bool ShowProgress = true);
};

#endif
