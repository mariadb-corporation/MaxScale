#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/**
 * @brief readenv Read enviromental variable, if emtpy - set dafault
 * @param name Name of the variable
 * @param format Default value
 * @return Enviromental variable value
 */
char * readenv(const char * name, const char *format, ...);

/**
 * @brief readenv_int Read integer value of enviromental variable, if empty - set dafault
 * @param name Name of the variable
 * @param def Default value
 * @return Enviromental variable value converted to int
 */
int readenv_int(const char * name, int def);

/**
 * @brief readenv_int Read boolean value of enviromental variable, if empty - set dafault
 * Values 'yes', 'y', 'true' (case independedant) are interpreted as TRUE, everything else - as FALSE
 * @param name Name of the variable
 * @param def Default value
 * @return Enviromental variable value converted to bool
 */
bool readenv_bool(const char * name, bool def);
