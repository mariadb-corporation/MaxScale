/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */


// The following is adapted from SQLite3 version 3.30.1.
// https://www.sqlite.org/index.html

#include "sqlite_strlike.hh"
#include <cstdint>
#include <cstring>
#include <maxbase/assert.h>

namespace
{
using u32 = uint32_t;           /* 4-byte unsigned integer */
using u8 = uint8_t;             /* 1-byte unsigned integer */

/*
** A structure defining how to do GLOB-style comparisons.
*/
struct compareInfo
{
    u8 matchAll;            /* "*" or "%" */
    u8 matchOne;            /* "?" or "_" */
    u8 matchSet;            /* "[" or 0 */
    u8 noCase;              /* true to ignore case differences */
};

/**
 * Pattern compare implementation.
 *
 * @param zPattern The glob pattern
 * @param zString The string to compare against the glob
 * @param pInfo Information about how to do the compare
 * @param matchOther The escape char (LIKE) or '[' (GLOB)
 * @return 0 on match
 */
int patternCompare(const u8* zPattern, const u8* zString, const compareInfo* pInfo, u32 matchOther);

/* An array to map all upper-case characters into their corresponding
** lower-case character.
**
** SQLite only considers US-ASCII (or EBCDIC) characters.  We do not
** handle case conversions for the UTF character set since the tables
** involved are nearly as big or bigger than SQLite itself.
*/
const unsigned char sqlite3UpperToLower[] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,
    18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,
    36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,
    54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  97,  98,  99,  100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
    122, 91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107,
    108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
    126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161,
    162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197,
    198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,
    216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
    234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251,
    252, 253, 254, 255
};

/*
** The following 256 byte lookup table is used to support SQLites built-in
** equivalents to the following standard library functions:
**
**   isspace()                        0x01
**   isalpha()                        0x02
**   isdigit()                        0x04
**   isalnum()                        0x06
**   isxdigit()                       0x08
**   toupper()                        0x20
**   SQLite identifier character      0x40
**   Quote character                  0x80
**
** Bit 0x20 is set if the mapped character requires translation to upper
** case. i.e. if the character is a lower-case ASCII character.
** If x is a lower-case ASCII character, then its upper-case equivalent
** is (x - 0x20). Therefore toupper() can be implemented as:
**
**   (x & ~(map[x]&0x20))
**
** The equivalent of tolower() is implemented using the sqlite3UpperToLower[]
** array. tolower() is used more often than toupper() by SQLite.
**
** Bit 0x40 is set if the character is non-alphanumeric and can be used in an
** SQLite identifier.  Identifiers are alphanumerics, "_", "$", and any
** non-ASCII UTF character. Hence the test for whether or not a character is
** part of an identifier is 0x46.
*/
const unsigned char sqlite3CtypeMap[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 00..07    ........ */
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,     /* 08..0f    ........ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 10..17    ........ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 18..1f    ........ */
    0x01, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x80,     /* 20..27     !"#$%&' */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 28..2f    ()*+,-./ */
    0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,     /* 30..37    01234567 */
    0x0c, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 38..3f    89:;<=>? */

    0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x02,     /* 40..47    @ABCDEFG */
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,     /* 48..4f    HIJKLMNO */
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,     /* 50..57    PQRSTUVW */
    0x02, 0x02, 0x02, 0x80, 0x00, 0x00, 0x00, 0x40,     /* 58..5f    XYZ[\]^_ */
    0x80, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x22,     /* 60..67    `abcdefg */
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,     /* 68..6f    hijklmno */
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,     /* 70..77    pqrstuvw */
    0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 78..7f    xyz{|}~. */

    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* 80..87    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* 88..8f    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* 90..97    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* 98..9f    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* a0..a7    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* a8..af    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* b0..b7    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* b8..bf    ........ */

    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* c0..c7    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* c8..cf    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* d0..d7    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* d8..df    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* e0..e7    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* e8..ef    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,     /* f0..f7    ........ */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40      /* f8..ff    ........ */
};

/*
** This lookup table is used to help decode the first byte of
** a multi-byte UTF8 character.
*/
const unsigned char sqlite3Utf8Trans1[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
};

/*
** The following macros mimic the standard library functions toupper(),
** isspace(), isalnum(), isdigit() and isxdigit(), respectively. The
** sqlite versions only work for ASCII characters, regardless of locale.
*/
# define sqlite3Toupper(x) ((x) & ~(sqlite3CtypeMap[(unsigned char)(x)] & 0x20))
# define sqlite3Tolower(x) (sqlite3UpperToLower[(unsigned char)(x)])

/**
 * Read a utf8-character from string.
 *
 * @param pz Pointer to string from which to read char
 * @return Next utf8-character
 */
u32 sqlite3Utf8Read(const unsigned char** pz)
{
    unsigned int c;

    /* Same as READ_UTF8() above but without the zTerm parameter.
    ** For this routine, we assume the UTF8 string is always zero-terminated.
    */
    c = *((*pz)++);
    if (c >= 0xc0)
    {
        c = sqlite3Utf8Trans1[c - 0xc0];
        while ((*(*pz) & 0xc0) == 0x80)
        {
            c = (c << 6) + (0x3f & *((*pz)++));
        }
        if (c < 0x80
            || (c & 0xFFFFF800) == 0xD800
            || (c & 0xFFFFFFFE) == 0xFFFE)
        {
            c = 0xFFFD;
        }
    }
    return c;
}

/*
** Provide the Utf8Read() macro for fast reading of the next character in the common case where
** the next character is ASCII.
*/
# define Utf8Read(A) (A[0] < 0x80 ? *(A++) : sqlite3Utf8Read(&A))

/*
** Assuming zIn points to the first byte of a UTF-8 character,
** advance zIn to point to the first byte of the next UTF-8 character.
*/
#define SQLITE_SKIP_UTF8(zIn) \
    {                        \
        if ((*(zIn++)) >= 0xc0) {                              \
            while ((*zIn & 0xc0) == 0x80) {zIn++;}             \
        }                                                    \
    }

/*
** Possible error returns from patternMatch()
*/
#define SQLITE_MATCH           0
#define SQLITE_NOMATCH         1
#define SQLITE_NOWILDCARDMATCH 2

/*
** Compare two UTF-8 strings for equality where the first string is
** a GLOB or LIKE expression.  Return values:
**
**    SQLITE_MATCH:            Match
**    SQLITE_NOMATCH:          No match
**    SQLITE_NOWILDCARDMATCH:  No match in spite of having * or % wildcards.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
**
** With the [...] and [^...] matching, a ']' character can be included
** in the list by making it the first character after '[' or '^'.  A
** range of characters can be specified using '-'.  Example:
** "[a-z]" matches any single lower-case letter.  To match a '-', make
** it the last character in the list.
**
** Like matching rules:
**
**      '%'       Matches any sequence of zero or more characters
**
***     '_'       Matches any one character
**
**      Ec        Where E is the "esc" character and c is any other
**                character, including '%', '_', and esc, match exactly c.
**
** The comments within this routine usually assume glob matching.
**
** This routine is usually quick, but can be N**2 in the worst case.
*/
int patternCompare(const u8* zPattern, const u8* zString, const compareInfo* pInfo, u32 matchOther)
{
    u32 c, c2;                      /* Next pattern and input string chars */
    u32 matchOne = pInfo->matchOne; /* "?" or "_" */
    u32 matchAll = pInfo->matchAll; /* "*" or "%" */
    u8 noCase = pInfo->noCase;      /* True if uppercase==lowercase */
    const u8* zEscaped = 0;         /* One past the last escaped input char */

    while ((c = Utf8Read(zPattern)) != 0)
    {
        if (c == matchAll)  /* Match "*" */
        {                   /* Skip over multiple "*" characters in the pattern.  If there
                            ** are also "?" characters, skip those as well, but consume a
                            ** single character of the input string for each "?" skipped */
            while ((c = Utf8Read(zPattern)) == matchAll || c == matchOne)
            {
                if (c == matchOne && sqlite3Utf8Read(&zString) == 0)
                {
                    return SQLITE_NOWILDCARDMATCH;
                }
            }
            if (c == 0)
            {
                return SQLITE_MATCH;    /* "*" at the end of the pattern matches */
            }
            else if (c == matchOther)
            {
                if (pInfo->matchSet == 0)
                {
                    c = sqlite3Utf8Read(&zPattern);
                    if (c == 0)
                    {
                        return SQLITE_NOWILDCARDMATCH;
                    }
                }
                else
                {
                    /* "[...]" immediately follows the "*".  We have to do a slow
                    ** recursive search in this case, but it is an unusual case. */
                    mxb_assert(matchOther < 0x80);      /* '[' is a single-byte character */
                    while (*zString)
                    {
                        int bMatch = patternCompare(&zPattern[-1], zString, pInfo, matchOther);
                        if (bMatch != SQLITE_NOMATCH)
                        {
                            return bMatch;
                        }
                        SQLITE_SKIP_UTF8(zString);
                    }
                    return SQLITE_NOWILDCARDMATCH;
                }
            }

            /* At this point variable c contains the first character of the
            ** pattern string past the "*".  Search in the input string for the
            ** first matching character and recursively continue the match from
            ** that point.
            **
            ** For a case-insensitive search, set variable cx to be the same as
            ** c but in the other case and search the input string for either
            ** c or cx.
            */
            if (c <= 0x80)
            {
                char zStop[3];
                int bMatch;
                if (noCase)
                {
                    zStop[0] = sqlite3Toupper(c);
                    zStop[1] = sqlite3Tolower(c);
                    zStop[2] = 0;
                }
                else
                {
                    zStop[0] = c;
                    zStop[1] = 0;
                }
                while (1)
                {
                    zString += strcspn((const char*)zString, zStop);
                    if (zString[0] == 0)
                    {
                        break;
                    }
                    zString++;
                    bMatch = patternCompare(zPattern, zString, pInfo, matchOther);
                    if (bMatch != SQLITE_NOMATCH)
                    {
                        return bMatch;
                    }
                }
            }
            else
            {
                int bMatch;
                while ((c2 = Utf8Read(zString)) != 0)
                {
                    if (c2 != c)
                    {
                        continue;
                    }
                    bMatch = patternCompare(zPattern, zString, pInfo, matchOther);
                    if (bMatch != SQLITE_NOMATCH)
                    {
                        return bMatch;
                    }
                }
            }
            return SQLITE_NOWILDCARDMATCH;
        }
        if (c == matchOther)
        {
            if (pInfo->matchSet == 0)
            {
                c = sqlite3Utf8Read(&zPattern);
                if (c == 0)
                {
                    return SQLITE_NOMATCH;
                }
                zEscaped = zPattern;
            }
            else
            {
                u32 prior_c = 0;
                int seen = 0;
                int invert = 0;
                c = sqlite3Utf8Read(&zString);
                if (c == 0)
                {
                    return SQLITE_NOMATCH;
                }
                c2 = sqlite3Utf8Read(&zPattern);
                if (c2 == '^')
                {
                    invert = 1;
                    c2 = sqlite3Utf8Read(&zPattern);
                }
                if (c2 == ']')
                {
                    if (c == ']')
                    {
                        seen = 1;
                    }
                    c2 = sqlite3Utf8Read(&zPattern);
                }
                while (c2 && c2 != ']')
                {
                    if (c2 == '-' && zPattern[0] != ']' && zPattern[0] != 0 && prior_c > 0)
                    {
                        c2 = sqlite3Utf8Read(&zPattern);
                        if (c >= prior_c && c <= c2)
                        {
                            seen = 1;
                        }
                        prior_c = 0;
                    }
                    else
                    {
                        if (c == c2)
                        {
                            seen = 1;
                        }
                        prior_c = c2;
                    }
                    c2 = sqlite3Utf8Read(&zPattern);
                }
                if (c2 == 0 || (seen ^ invert) == 0)
                {
                    return SQLITE_NOMATCH;
                }
                continue;
            }
        }
        c2 = Utf8Read(zString);
        if (c == c2)
        {
            continue;
        }
        if (noCase && sqlite3Tolower(c) == sqlite3Tolower(c2) && c < 0x80 && c2 < 0x80)
        {
            continue;
        }
        if (c == matchOne && zPattern != zEscaped && c2 != 0)
        {
            continue;
        }
        return SQLITE_NOMATCH;
    }
    return *zString == 0 ? SQLITE_MATCH : SQLITE_NOMATCH;
}
}

int sql_strlike(const char* zPattern, const char* zStr, unsigned int esc)
{
    /* The correct SQL-92 behavior is for the LIKE operator to ignore
    ** case.  Thus  'a' LIKE 'A' would be true. */
    const compareInfo likeInfoNorm = {'%', '_', 0, 1};
    return patternCompare((u8*)zPattern, (u8*)zStr, &likeInfoNorm, esc);
}

int sql_strlike_case(const char* zPattern, const char* zStr, unsigned int esc)
{
    const compareInfo likeInfoCase = {'%', '_', 0, 0};
    return patternCompare((u8*)zPattern, (u8*)zStr, &likeInfoCase, esc);
}
