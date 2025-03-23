// This file can be included several times.

#ifndef SET_CONFIG_DOMAIN
#error "SET_CONFIG_DOMAIN macro not defined"
#define SET_CONFIG_DOMAIN(ConfigDomain) ;
#endif

SET_CONFIG_DOMAIN(CONFIGDOMAIN::DDNET)
#include "config_variables.h"

SET_CONFIG_DOMAIN(CONFIGDOMAIN::TATER)
#include "config_variables_tater.h"
