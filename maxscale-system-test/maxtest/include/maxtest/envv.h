#pragma once

#include <string>

/**
 * Read enviroment variable value. If variable is not set, set it to the given default value and return the
 * written value.
 *
 * @param name Name of the variable
 * @param format Default value format string
 * @return Enviromental variable value
 */
char * readenv(const char * name, const char *format, ...);

std::string envvar_read_write_def_str(const char* name, const char* format, ...);

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
