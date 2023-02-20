#pragma once

#include <string>

/**
 * Read enviroment variable value. If variable is not set, set it to the given default value and return the
 * written value.
 *
 * @param name Name of the variable
 * @param format Default value format string
 * @return Environment variable value
 */
std::string readenv(const char* name, const char* format, ...) __attribute__ ((format (printf, 2, 3)));

std::string envvar_get_set(const char* name, const char* format, ...)
__attribute__ ((format (printf, 2, 3)));;

/**
 * @brief readenv_int Read integer value of environment variable, if empty - set default
 * @param name Name of the variable
 * @param def Default value
 * @return Environment variable value converted to int
 */
int readenv_int(const char * name, int def);

/**
 * @brief readenv_int Read boolean value of environment variable, if empty - set default
 * Values 'yes', 'y', 'true' (case independent) are interpreted as TRUE, everything else - as FALSE
 * @param name Name of the variable
 * @param def Default value
 * @return Environment variable value converted to bool
 */
bool readenv_bool(const char * name, bool def);
