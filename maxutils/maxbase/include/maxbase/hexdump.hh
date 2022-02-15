#pragma once

#include <maxbase/ccdefs.hh>
#include <iosfwd>

namespace maxbase
{

/**
 * @brief hexdump - Same output as command line hexdump -C.
 * @param pBytes  - ptr to the buffer
 * @param len     - length of the buffer
 * @return std::ostream &out
 */
std::ostream& hexdump(std::ostream& out, const void* pBytes, int len);
}  // namespace maxbase
