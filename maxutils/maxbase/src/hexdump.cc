#include <maxbase/hexdump.hh>

#include <sstream>
#include <iomanip>
#include <cstring>

namespace maxbase
{

/*
 *  "Here we go again! =>\n"
 *  00000000  48 65 72 65 20 77 65 20  67 6f 20 61 67 61 69 6e  |Here we go again|
 *  00000010  21 20 3d 3e 0a                                    |! =>.|
 *  00000015
 *
 */
std::ostream& hexdump(std::ostream& out, const void* pBytes, int len)
{
    using namespace std;

    const int BYTES_PER_ROW         = 16;
    const size_t NUMERIC_CHAR_WIDTH = 10 + BYTES_PER_ROW * 3 + 2;  // nchars up to the first pipe symbol

    const uint8_t* const pBegin = (uint8_t*) pBytes;
    const uint8_t* const pEnd   = pBegin + len;

    bool already_said_same = false;
    const uint8_t* pPrev;
    const uint8_t* pCurr;
    for (pPrev = pCurr = pBegin; pCurr < pEnd; pPrev = pCurr, pCurr += BYTES_PER_ROW)
    {
        // Print a single '*' for repeating, identical rows including the last row even if it is shorter
        if (pPrev != pCurr && memcmp(pPrev, pCurr, min(BYTES_PER_ROW, int(pEnd - pCurr))) == 0)
        {
            if (!already_said_same)
            {
                out << "*\n";
                already_said_same = true;
            }
            continue;
        }
        already_said_same = false;

        std::ostringstream oss;

        // the address
        auto addr = pCurr - pBegin;
        oss << setw(8) << setfill('0') << right << hex << addr << ' ';

        // the bytes
        for (const uint8_t* ptr = pCurr; ptr < pEnd && ptr < pCurr + BYTES_PER_ROW; ++ptr)
        {
            if ((ptr - pCurr) % 8 == 0)
            {
                oss << ' ';
            }
            oss << setw(2) << int(*ptr) << ' ';
        }

        // padd up to the first pipe
        if (oss.str().size() < NUMERIC_CHAR_WIDTH)
        {
            oss << std::string(NUMERIC_CHAR_WIDTH - oss.str().size(), ' ');
        }

        // the chars inside pipes
        oss << '|';
        for (const uint8_t* ptr = pCurr; ptr < pEnd && ptr < pCurr + BYTES_PER_ROW; ++ptr)
        {
            auto ch = *ptr;
            oss << char(std::isprint(ch) ? ch : '.');
        }
        oss << '|';

        oss << '\n';

        out << oss.str();
    }

    // The end address on its own line
    out << setw(8) << setfill('0') << right << hex << len << '\n';

    return out;
}
}  // namespace maxbase
