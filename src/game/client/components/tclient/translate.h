#ifndef GAME_CLIENT_COMPONENTS_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_TRANSLATE_H
#include <game/client/component.h>
#include <vector>

class CTranslate;

class ITranslateBackend
{
public:
	virtual ~ITranslateBackend() {};
	virtual const char *Name() const = 0;
	virtual bool Update(bool &Success, char *pOut, size_t Length) = 0;
};

class CTranslate : public CComponent
{
	// Current translation task
	CChat::CLine m_Line;
	ITranslateBackend *m_pBackend = nullptr;

	static void ConTranslate(IConsole::IResult *pResult, void *pUserData);

	CChat::CLine *FindMessage(const char *pName);
	void Translate(const char *pName);

public:
	virtual int Sizeof() const override { return sizeof(*this); }

	virtual void OnConsoleInit() override;
	virtual void OnRender() override;

};

#endif
