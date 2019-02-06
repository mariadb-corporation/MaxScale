/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mysql_binlog.c - Extracting information from binary logs
 */

#include <maxscale/mysql_binlog.h>
#include <maxscale/mysql_utils.hh>
#include <stdlib.h>
#include <string.h>
#include <maxscale/users.h>
#include <strings.h>
#include <math.h>

#include <maxscale/protocol/mysql.hh>

static uint64_t unpack_bytes(uint8_t* ptr, size_t bytes);

/**
 * @brief Convert a table column type to a string
 *
 * @param type The table column type
 * @return The type of the column in human readable format
 * @see mxs_lestr_consume
 */
const char* column_type_to_string(uint8_t type)
{
    switch (type)
    {
    case TABLE_COL_TYPE_DECIMAL:
        return "DECIMAL";

    case TABLE_COL_TYPE_TINY:
        return "TINY";

    case TABLE_COL_TYPE_SHORT:
        return "SHORT";

    case TABLE_COL_TYPE_LONG:
        return "LONG";

    case TABLE_COL_TYPE_FLOAT:
        return "FLOAT";

    case TABLE_COL_TYPE_DOUBLE:
        return "DOUBLE";

    case TABLE_COL_TYPE_NULL:
        return "NULL";

    case TABLE_COL_TYPE_TIMESTAMP:
        return "TIMESTAMP";

    case TABLE_COL_TYPE_LONGLONG:
        return "LONGLONG";

    case TABLE_COL_TYPE_INT24:
        return "INT24";

    case TABLE_COL_TYPE_DATE:
        return "DATE";

    case TABLE_COL_TYPE_TIME:
        return "TIME";

    case TABLE_COL_TYPE_DATETIME:
        return "DATETIME";

    case TABLE_COL_TYPE_YEAR:
        return "YEAR";

    case TABLE_COL_TYPE_NEWDATE:
        return "NEWDATE";

    case TABLE_COL_TYPE_VARCHAR:
        return "VARCHAR";

    case TABLE_COL_TYPE_BIT:
        return "BIT";

    case TABLE_COL_TYPE_TIMESTAMP2:
        return "TIMESTAMP2";

    case TABLE_COL_TYPE_DATETIME2:
        return "DATETIME2";

    case TABLE_COL_TYPE_TIME2:
        return "TIME2";

    case TABLE_COL_TYPE_NEWDECIMAL:
        return "NEWDECIMAL";

    case TABLE_COL_TYPE_ENUM:
        return "ENUM";

    case TABLE_COL_TYPE_SET:
        return "SET";

    case TABLE_COL_TYPE_TINY_BLOB:
        return "TINY_BLOB";

    case TABLE_COL_TYPE_MEDIUM_BLOB:
        return "MEDIUM_BLOB";

    case TABLE_COL_TYPE_LONG_BLOB:
        return "LONG_BLOB";

    case TABLE_COL_TYPE_BLOB:
        return "BLOB";

    case TABLE_COL_TYPE_VAR_STRING:
        return "VAR_STRING";

    case TABLE_COL_TYPE_STRING:
        return "STRING";

    case TABLE_COL_TYPE_GEOMETRY:
        return "GEOMETRY";

    default:
        mxb_assert(false);
        break;
    }
    return "UNKNOWN";
}

bool column_is_blob(uint8_t type)
{
    switch (type)
    {
    case TABLE_COL_TYPE_TINY_BLOB:
    case TABLE_COL_TYPE_MEDIUM_BLOB:
    case TABLE_COL_TYPE_LONG_BLOB:
    case TABLE_COL_TYPE_BLOB:
        return true;
    }
    return false;
}

/**
 * @brief Check if the column is a string type column
 *
 * @param type Type of the column
 * @return True if the column is a string type column
 * @see mxs_lestr_consume
 */
bool column_is_variable_string(uint8_t type)
{
    switch (type)
    {
    case TABLE_COL_TYPE_DECIMAL:
    case TABLE_COL_TYPE_VARCHAR:
    case TABLE_COL_TYPE_BIT:
    case TABLE_COL_TYPE_NEWDECIMAL:
    case TABLE_COL_TYPE_VAR_STRING:
    case TABLE_COL_TYPE_GEOMETRY:
        return true;

    default:
        return false;
    }
}

/**
 * @brief Detect BIT type columns
 * @param type Type of the column
 * @return  True if the column is a BIT
 */
bool column_is_bit(uint8_t type)
{
    return type == TABLE_COL_TYPE_BIT;
}

/**
 * Check if a column is of a temporal type
 * @param type Column type
 * @return True if the type is temporal
 */
bool column_is_temporal(uint8_t type)
{
    switch (type)
    {
    case TABLE_COL_TYPE_YEAR:
    case TABLE_COL_TYPE_DATE:
    case TABLE_COL_TYPE_TIME:
    case TABLE_COL_TYPE_TIME2:
    case TABLE_COL_TYPE_DATETIME:
    case TABLE_COL_TYPE_DATETIME2:
    case TABLE_COL_TYPE_TIMESTAMP:
    case TABLE_COL_TYPE_TIMESTAMP2:
        return true;
    }
    return false;
}

/**
 * @brief Check if the column is a string type column
 *
 * @param type Type of the column
 * @return True if the column is a string type column
 * @see mxs_lestr_consume
 */
bool column_is_fixed_string(uint8_t type)
{
    return type == TABLE_COL_TYPE_STRING;
}

/**
 * Check if a column is a DECIMAL field
 * @param type Column type
 * @return True if column is DECIMAL
 */
bool column_is_decimal(uint8_t type)
{
    return type == TABLE_COL_TYPE_NEWDECIMAL;
}

/**
 * Check if a column is an ENUM or SET
 * @param type Column type
 * @return True if column is either ENUM or SET
 */
bool fixed_string_is_enum(uint8_t type)
{
    return type == TABLE_COL_TYPE_ENUM || type == TABLE_COL_TYPE_SET;
}

/**
 * @brief Unpack a YEAR type
 *
 * The value seems to be stored as an offset from the year 1900
 * @param val Stored value
 * @param dest Destination where unpacked value is stored
 */
static void unpack_year(uint8_t* ptr, struct tm* dest)
{
    memset(dest, 0, sizeof(*dest));
    dest->tm_year = *ptr;
}

/** Base-10 logarithm values */
int64_t log_10_values[] =
{
    1,
    10,
    100,
    1000,
    10000,
    100000,
    1000000,
    10000000,
    100000000
};

/**
 * If the TABLE_COL_TYPE_DATETIME type field is declared as a datetime with
 * extra precision, the packed length is shorter than 8 bytes.
 */
size_t datetime_sizes[] =
{
    5,  // DATETIME(0)
    6,  // DATETIME(1)
    6,  // DATETIME(2)
    7,  // DATETIME(3)
    7,  // DATETIME(4)
    7,  // DATETIME(5)
    8   // DATETIME(6)
};

/**
 * @brief Unpack a DATETIME
 *
 * The DATETIME is stored as a 8 byte value with the values stored as multiples
 * of 100. This means that the stored value is in the format YYYYMMDDHHMMSS.
 * @param val Value read from the binary log
 * @param dest Pointer where the unpacked value is stored
 */
static void unpack_datetime(uint8_t* ptr, int length, struct tm* dest)
{
    uint64_t val = 0;
    uint32_t second, minute, hour, day, month, year;

    val = gw_mysql_get_byte8(ptr);
    second = val - ((val / 100) * 100);
    val /= 100;
    minute = val - ((val / 100) * 100);
    val /= 100;
    hour = val - ((val / 100) * 100);
    val /= 100;
    day = val - ((val / 100) * 100);
    val /= 100;
    month = val - ((val / 100) * 100);
    val /= 100;
    year = val;

    memset(dest, 0, sizeof(struct tm));
    dest->tm_year = year - 1900;
    dest->tm_mon = month - 1;
    dest->tm_mday = day;
    dest->tm_hour = hour;
    dest->tm_min = minute;
    dest->tm_sec = second;
}

/**
 * Unpack a 5 byte reverse byte order value
 * @param data pointer to data
 * @return Unpacked value
 */
static inline uint64_t unpack5(uint8_t* data)
{
    uint64_t rval = data[4];
    rval += ((uint64_t)data[3]) << 8;
    rval += ((uint64_t)data[2]) << 16;
    rval += ((uint64_t)data[1]) << 24;
    rval += ((uint64_t)data[0]) << 32;
    return rval;
}

/** The DATETIME values are stored in the binary logs with an offset */
#define DATETIME2_OFFSET 0x8000000000LL

/**
 * @brief Unpack a DATETIME2
 *
 * The DATETIME2 is only used by row based replication in newer MariaDB servers.
 * @param val Value read from the binary log
 * @param dest Pointer where the unpacked value is stored
 */
static void unpack_datetime2(uint8_t* ptr, uint8_t decimals, struct tm* dest)
{
    int64_t unpacked = unpack5(ptr) - DATETIME2_OFFSET;
    if (unpacked < 0)
    {
        unpacked = -unpacked;
    }

    uint64_t date = unpacked >> 17;
    uint64_t yearmonth = date >> 5;
    uint64_t time = unpacked % (1 << 17);

    memset(dest, 0, sizeof(*dest));
    dest->tm_sec = time % (1 << 6);
    dest->tm_min = (time >> 6) % (1 << 6);
    dest->tm_hour = time >> 12;
    dest->tm_mday = date % (1 << 5);
    dest->tm_mon = (yearmonth % 13) - 1;

    /** struct tm stores the year as: Year - 1900 */
    dest->tm_year = (yearmonth / 13) - 1900;
}

/** Unpack a "reverse" byte order value */
#define unpack4(data) (data[3] + (data[2] << 8) + (data[1] << 16) + (data[0] << 24))

/**
 * @brief Unpack a TIMESTAMP
 *
 * The timestamps are stored with the high bytes first
 * @param val The stored value
 * @param dest Destination where the result is stored
 */
static void unpack_timestamp(uint8_t* ptr, uint8_t decimals, struct tm* dest)
{
    time_t t = unpack4(ptr);

    if (t == 0)
    {
        // Use GMT date to detect zero date timestamps
        gmtime_r(&t, dest);
    }
    else
    {
        localtime_r(&t, dest);
    }
}

#define unpack3(data) (data[2] + (data[1] << 8) + (data[0] << 16))

/**
 * @brief Unpack a TIME
 *
 * The TIME is stored as a 3 byte value with the values stored as multiples
 * of 100. This means that the stored value is in the format HHMMSS.
 * @param val Value read from the binary log
 * @param dest Pointer where the unpacked value is stored
 */
static void unpack_time(uint8_t* ptr, struct tm* dest)
{
    uint64_t val = unpack3(ptr);
    uint32_t second = val - ((val / 100) * 100);
    val /= 100;
    uint32_t minute = val - ((val / 100) * 100);
    val /= 100;
    uint32_t hour = val;

    memset(dest, 0, sizeof(struct tm));
    dest->tm_hour = hour;
    dest->tm_min = minute;
    dest->tm_sec = second;
}

/**
 * @brief Unpack a TIME2
 *
 * The TIME2 is stored as a 3 byte value containing the integer parts plus
 * the additional fractional parts as a trailing value. The
 * integer parts of the time are extracted with the following algorithm:
 *
 * hours   = (value >> 12) % (1 << 10)
 * minutes = (value >> 6) % (1 << 6)
 * seconds = value % (1 << 6)
 *
 * As the `struct tm` doesn't support fractional seconds, the fractional part
 * is ignored.
 *
 * @param val  Value read from the binary log
 * @param dest Pointer where the unpacked value is stored
 */
static void unpack_time2(uint8_t* ptr, uint8_t decimals, struct tm* dest)
{
    uint64_t val = unpack3(ptr) - DATETIME2_OFFSET;
    memset(dest, 0, sizeof(struct tm));
    dest->tm_hour = (val >> 12) % (1 << 10);
    dest->tm_min = (val >> 6) % (1 << 6);
    dest->tm_sec = val % (1 << 6);
}

/**
 * @brief Unpack a DATE value
 * @param ptr Pointer to packed value
 * @param dest Pointer where the unpacked value is stored
 */
static void unpack_date(uint8_t* ptr, struct tm* dest)
{
    uint64_t val = ptr[0] + (ptr[1] << 8) + (ptr[2] << 16);
    memset(dest, 0, sizeof(struct tm));
    dest->tm_mday = val & 31;
    dest->tm_mon = ((val >> 5) & 15) - 1;
    dest->tm_year = (val >> 9) - 1900;
}

/**
 * @brief Unpack an ENUM or SET field
 * @param ptr Pointer to packed value
 * @param metadata Pointer to field metadata
 * @return Length of the processed field in bytes
 */
size_t unpack_enum(uint8_t* ptr, uint8_t* metadata, uint8_t* dest)
{
    memcpy(dest, ptr, metadata[1]);
    return metadata[1];
}

/**
 * @brief Unpack a BIT
 *
 * A part of the BIT values are stored in the NULL value bitmask of the row event.
 * This makes extracting them a bit more complicated since the other fields
 * in the table could have an effect on the location of the stored values.
 *
 * It is possible that the BIT value is fully stored in the NULL value bitmask
 * which means that the actual row data is zero bytes for this field.
 * @param ptr Pointer to packed value
 * @param null_mask NULL field mask
 * @param col_count Number of columns in the row event
 * @param curr_col_index Current position of the field in the row event (zero indexed)
 * @param metadata Field metadata
 * @param dest Destination where the value is stored
 * @return Length of the processed field in bytes
 */
size_t unpack_bit(uint8_t* ptr,
                  uint8_t* null_mask,
                  uint32_t col_count,
                  uint32_t curr_col_index,
                  uint8_t* metadata,
                  uint64_t* dest)
{
    if (metadata[1])
    {
        memcpy(ptr, dest, metadata[1]);
    }
    return metadata[1];
}

/**
 * @brief Get the length of a temporal field
 * @param type Field type
 * @param decimals How many decimals the field has
 * @return Number of bytes the temporal value takes
 */
static size_t temporal_field_size(uint8_t type, uint8_t* decimals, int length)
{
    switch (type)
    {
    case TABLE_COL_TYPE_YEAR:
        return 1;

    case TABLE_COL_TYPE_TIME:
    case TABLE_COL_TYPE_DATE:
        return 3;

    case TABLE_COL_TYPE_TIME2:
        return 3 + ((*decimals + 1) / 2);

    case TABLE_COL_TYPE_DATETIME:
        return 8;

    case TABLE_COL_TYPE_TIMESTAMP:
        return 4;

    case TABLE_COL_TYPE_TIMESTAMP2:
        return 4 + ((*decimals + 1) / 2);

    case TABLE_COL_TYPE_DATETIME2:
        return 5 + ((*decimals + 1) / 2);

    default:
        MXS_ERROR("Unknown field type: %x %s", type, column_type_to_string(type));
        break;
    }

    return 0;
}

/**
 * @brief Unpack a temporal value
 *
 * MariaDB and MySQL both store temporal values in a special format. This function
 * unpacks them from the storage format and into a common, usable format.
 * @param type Column type
 * @param val Extracted packed value
 * @param tm Pointer where the unpacked temporal value is stored
 */
size_t unpack_temporal_value(uint8_t type, uint8_t* ptr, uint8_t* metadata, int length, struct tm* tm)
{
    switch (type)
    {
    case TABLE_COL_TYPE_YEAR:
        unpack_year(ptr, tm);
        break;

    case TABLE_COL_TYPE_DATETIME:
        unpack_datetime(ptr, length, tm);
        break;

    case TABLE_COL_TYPE_DATETIME2:
        unpack_datetime2(ptr, *metadata, tm);
        break;

    case TABLE_COL_TYPE_TIME:
        unpack_time(ptr, tm);
        break;

    case TABLE_COL_TYPE_TIME2:
        unpack_time2(ptr, *metadata, tm);
        break;

    case TABLE_COL_TYPE_DATE:
        unpack_date(ptr, tm);
        break;

    case TABLE_COL_TYPE_TIMESTAMP:
    case TABLE_COL_TYPE_TIMESTAMP2:
        unpack_timestamp(ptr, *metadata, tm);
        break;

    default:
        mxb_assert(false);
        break;
    }
    return temporal_field_size(type, metadata, length);
}

static bool is_zero_date(struct tm* tm)
{
    // Detects 1970-01-01 00:00:00
    return tm->tm_sec == 0 && tm->tm_min == 0 && tm->tm_hour == 0
        && tm->tm_mday == 1 && tm->tm_mon == 0 && tm->tm_year == 70;
}

void format_temporal_value(char* str, size_t size, uint8_t type, struct tm* tm)
{
    const char* format = "";

    switch (type)
    {
    case TABLE_COL_TYPE_DATETIME:
    case TABLE_COL_TYPE_DATETIME2:
    case TABLE_COL_TYPE_TIMESTAMP:
    case TABLE_COL_TYPE_TIMESTAMP2:
        format = "%Y-%m-%d %H:%M:%S";
        break;

    case TABLE_COL_TYPE_TIME:
    case TABLE_COL_TYPE_TIME2:
        format = "%H:%M:%S";
        break;

    case TABLE_COL_TYPE_DATE:
        format = "%Y-%m-%d";
        break;

    case TABLE_COL_TYPE_YEAR:
        format = "%Y";
        break;

    default:
        MXS_ERROR("Unexpected temporal type: %x %s", type, column_type_to_string(type));
        mxb_assert(false);
        break;
    }

    if ((type == TABLE_COL_TYPE_TIMESTAMP || type == TABLE_COL_TYPE_TIMESTAMP2) && is_zero_date(tm))
    {
        strcpy(str, "0-00-00 00:00:00");
    }
    else
    {
        strftime(str, size, format, tm);
    }
}

/**
 * @brief Extract a value from a row event
 *
 * This function extracts a single value from a row event and stores it for
 * further processing. Integer values are usable immediately but temporal
 * values need to be unpacked from the compact format they are stored in.
 * @param ptr Pointer to the start of the field value
 * @param type Column type of the field
 * @param metadata Pointer to the field metadata
 * @param val Destination where the extracted value is stored
 * @return Number of bytes copied
 * @see extract_temporal_value
 */
size_t unpack_numeric_field(uint8_t* src, uint8_t type, uint8_t* metadata, uint8_t* dest)
{
    size_t size = 0;
    switch (type)
    {
    case TABLE_COL_TYPE_LONG:
    case TABLE_COL_TYPE_FLOAT:
        size = 4;
        break;

    case TABLE_COL_TYPE_INT24:
        size = 3;
        break;

    case TABLE_COL_TYPE_LONGLONG:
    case TABLE_COL_TYPE_DOUBLE:
        size = 8;
        break;

    case TABLE_COL_TYPE_SHORT:
        size = 2;
        break;

    case TABLE_COL_TYPE_TINY:
        size = 1;
        break;

    default:
        MXS_ERROR("Bad column type: %x %s", type, column_type_to_string(type));
        break;
    }

    mxb_assert(size > 0);
    memcpy(dest, src, size);
    return size;
}

static uint64_t unpack_bytes(uint8_t* ptr, size_t bytes)
{
    uint64_t val = 0;

    switch (bytes)
    {
    case 1:
        val = ptr[0];
        break;

    case 2:
        val = ptr[1] | ((uint64_t)(ptr[0]) << 8);
        break;

    case 3:
        val = (uint64_t)ptr[2] | ((uint64_t)ptr[1] << 8)
            | ((uint64_t)ptr[0] << 16);
        break;

    case 4:
        val = (uint64_t)ptr[3] | ((uint64_t)ptr[2] << 8)
            | ((uint64_t)ptr[1] << 16) | ((uint64_t)ptr[0] << 24);
        break;

    case 5:
        val = (uint64_t)ptr[4] | ((uint64_t)ptr[3] << 8)
            | ((uint64_t)ptr[2] << 16) | ((uint64_t)ptr[1] << 24)
            | ((uint64_t)ptr[0] << 32);
        break;

    case 6:
        val = (uint64_t)ptr[5] | ((uint64_t)ptr[4] << 8)
            | ((uint64_t)ptr[3] << 16) | ((uint64_t)ptr[2] << 24)
            | ((uint64_t)ptr[1] << 32) | ((uint64_t)ptr[0] << 40);
        break;

    case 7:
        val = (uint64_t)ptr[6] | ((uint64_t)ptr[5] << 8)
            | ((uint64_t)ptr[4] << 16) | ((uint64_t)ptr[3] << 24)
            | ((uint64_t)ptr[2] << 32) | ((uint64_t)ptr[1] << 40)
            | ((uint64_t)ptr[0] << 48);
        break;

    case 8:
        val = (uint64_t)ptr[7] | ((uint64_t)ptr[6] << 8)
            | ((uint64_t)ptr[5] << 16) | ((uint64_t)ptr[4] << 24)
            | ((uint64_t)ptr[3] << 32) | ((uint64_t)ptr[2] << 40)
            | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[0] << 56);
        break;

    default:
        mxb_assert(false);
        break;
    }

    return val;
}

size_t unpack_decimal_field(uint8_t* ptr, uint8_t* metadata, double* val_float)
{
    const int dec_dig = 9;
    int precision = metadata[0];
    int decimals = metadata[1];
    int dig_bytes[] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
    int ipart = precision - decimals;
    int ipart1 = ipart / dec_dig;
    int fpart1 = decimals / dec_dig;
    int ipart2 = ipart - ipart1 * dec_dig;
    int fpart2 = decimals - fpart1 * dec_dig;
    int ibytes = ipart1 * 4 + dig_bytes[ipart2];
    int fbytes = fpart1 * 4 + dig_bytes[fpart2];
    int field_size = ibytes + fbytes;

    /** Remove the sign bit and store it locally */
    bool negative = (ptr[0] & 0x80) == 0;
    ptr[0] ^= 0x80;

    if (negative)
    {
        for (int i = 0; i < ibytes; i++)
        {
            ptr[i] = ~ptr[i];
        }

        for (int i = 0; i < fbytes; i++)
        {
            ptr[i + ibytes] = ~ptr[i + ibytes];
        }
    }

    int64_t val_i = 0;

    if (ibytes > 8)
    {
        int extra = ibytes - 8;
        ptr += extra;
        ibytes -= extra;
        mxb_assert(ibytes == 8);
    }

    val_i = unpack_bytes(ptr, ibytes);
    int64_t val_f = fbytes ? unpack_bytes(ptr + ibytes, fbytes) : 0;

    if (negative)
    {
        val_i = -val_i;
        val_f = -val_f;
    }

    *val_float = (double)val_i + ((double)val_f / (pow(10.0, decimals)));

    return field_size;
}
