#ifndef BUILTIN_FUNCTIONS_H
#define BUILTIN_FUNCTIONS_H
/**
 * @LICENCE@
 *
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_builtin_functions();
void finish_builtin_functions();

bool is_builtin_readonly_function(const char* zToken, bool check_oracle);

#ifdef __cplusplus
}
#endif

#endif
