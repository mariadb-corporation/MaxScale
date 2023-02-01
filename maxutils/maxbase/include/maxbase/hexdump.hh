#pragma once

#include <maxbase/ccdefs.hh>
#include <iosfwd>

namespace maxbase
{

/**
 * @brief hexdump - Same output as command line hexdump -C.
 * @param out     - output stream
 * @param pBytes  - ptr to the buffer
 * @param len     - length of the buffer
 * @return std::ostream &out
 */
std::ostream& hexdump(std::ostream& out, const void* pBytes, int len);

/**
 * Overload for mxb::hexdump that returns a string
 *
 * @param pBytes Pointer to the start of the memory
 * @param len    Length of the memory in bytes
 *
 * @return Human-readable hexdump of the memory
 */

std::string hexdump(const void* pBytes, int len);
}
