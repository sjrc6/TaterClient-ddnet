/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include "kernel.h"

#define CONFIG_DOMAIN(Name, ConfigPath, HasVars) Name,
enum CONFIGDOMAIN
{
#include "shared/config_domains.h"
	NUM,
	START = 0
};
#undef CONFIG_DOMAIN
static inline CONFIGDOMAIN& operator++(CONFIGDOMAIN& domain)
{
	return domain = static_cast<CONFIGDOMAIN>(static_cast<int>(domain) + 1);
}

class CConfigDomain
{
public:
	const char *m_aConfigPath;
	bool m_HasVars;
}
#define CONFIG_DOMAIN(Name, ConfigPath, HasVars) {ConfigPath, HasVars},
static const s_aConfigDomains[CONFIGDOMAIN::NUM] = {
#include "shared/config_domains.h"
};
#undef CONFIG_DOMAIN

class IConfigManager : public IInterface
{
	MACRO_INTERFACE("config")
public:
	typedef void (*SAVECALLBACKFUNC)(IConfigManager *pConfig, void *pUserData);
	typedef void (*POSSIBLECFGFUNC)(const struct SConfigVariable *, void *pUserData);

	virtual void Init() = 0;
	virtual void Reset(const char *pScriptName) = 0;
	virtual void ResetGameSettings() = 0;
	virtual void SetReadOnly(const char *pScriptName, bool ReadOnly) = 0;
	virtual bool Save() = 0;
	virtual class CConfig *Values() = 0;

	virtual void RegisterCallback(CONFIGDOMAIN ConfigDomain, SAVECALLBACKFUNC pfnFunc, void *pUserData) = 0;

	virtual void WriteLine(CONFIGDOMAIN ConfigDomain, const char *pLine) = 0;

	virtual void StoreUnknownCommand(const char *pCommand) = 0;

	virtual void PossibleConfigVariables(const char *pStr, int FlagMask, POSSIBLECFGFUNC pfnCallback, void *pUserData) = 0;
};

extern IConfigManager *CreateConfigManager();

#endif
