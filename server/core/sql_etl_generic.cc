/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/sql_etl_generic.hh"

#include <maxbase/string.hh>

#include <sql.h>
#include <sqlext.h>

namespace
{
int to_int(const std::optional<std::string>& val)
{
    // Returning a value of -1 helps avoid conflicting with any of the ODBC constants that usually seem to
    // start from 0 in unixODBC.
    return atoi(val.value_or("-1").c_str());
}

std::string to_str(const std::optional<std::string>& val)
{
    return val.value_or("");
}

std::string to_mariadb_type(const mxq::TextResult::Row& row)
{
    // https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlcolumns-function?view=sql-server-ver16#comments
    // The values are offset by one.
    const std::string COLUMN_NAME = to_str(row[3]);
    const int DATA_TYPE = to_int(row[4]);
    const std::string TYPE_NAME = to_str(row[5]);
    const int COLUMN_SIZE = to_int(row[6]);
    const int DECIMAL_DIGITS = to_int(row[8]);
    const std::string COLUMN_DEF = to_str(row[12]);
    const int CHAR_OCTET_LENGTH = to_int(row[15]);
    const std::string IS_NULLABLE = to_str(row[17]);
    std::ostringstream ss;

    ss << "`" << COLUMN_NAME << "` ";

    switch (DATA_TYPE)
    {
    case SQL_TINYINT:
        ss << "TINYINT";
        break;

    case SQL_SMALLINT:
        ss << "SMALLINT";
        break;

    case SQL_INTEGER:
        ss << "INT";
        break;

    case SQL_BIGINT:
        ss << "BIGINT";
        break;

    case SQL_FLOAT:
    case SQL_REAL:
        ss << "FLOAT";
        break;

    case SQL_DOUBLE:
        ss << "DOUBLE";
        break;

    case SQL_BIT:
        ss << "BIT";
        break;

    case SQL_WCHAR:
    case SQL_CHAR:
        ss << "CHAR(" << COLUMN_SIZE << ")";
        break;

    case SQL_GUID:
        ss << "UUID";
        break;

    case SQL_BINARY:
        ss << "BINARY(" << COLUMN_SIZE << ")";
        break;

    case SQL_DECIMAL:
    case SQL_NUMERIC:
        ss << "DECIMAL" << "(" << COLUMN_SIZE << "," << DECIMAL_DIGITS << ")";
        break;

    case SQL_WVARCHAR:
    case SQL_VARCHAR:
    case SQL_WLONGVARCHAR:
    case SQL_LONGVARCHAR:
        if (CHAR_OCTET_LENGTH < 16384)
        {
            ss << "VARCHAR(" << COLUMN_SIZE << ")";
        }
        else if (CHAR_OCTET_LENGTH < 65535)
        {
            ss << "TEXT";
        }
        else if (CHAR_OCTET_LENGTH < 16777215)
        {
            ss << "MEDIUMTEXT";
        }
        else
        {
            ss << "LONGTEXT";
        }
        break;

    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
        if (CHAR_OCTET_LENGTH < 16384)
        {
            ss << "VARBINARY(" << CHAR_OCTET_LENGTH << ")";
        }
        else if (CHAR_OCTET_LENGTH < 65535)
        {
            ss << "BLOB";
        }
        else if (CHAR_OCTET_LENGTH < 16777215)
        {
            ss << "MEDIUMBLOB";
        }
        else
        {
            ss << "LONGBLOB";
        }
        break;

    case SQL_TYPE_DATE:
        ss << "DATE";
        break;

#ifdef SQL_TYPE_UTCTIME
    case SQL_TYPE_UTCTIME:
#endif
    case SQL_TYPE_TIME:
        ss << "TIME";
        break;

    case SQL_TYPE_TIMESTAMP:
        ss << "TIMESTAMP";
        break;

#ifdef SQL_TYPE_UTCDATETIME
    case SQL_TYPE_UTCDATETIME:
#endif
    case SQL_INTERVAL_MONTH:
    case SQL_INTERVAL_YEAR:
    case SQL_INTERVAL_YEAR_TO_MONTH:
    case SQL_INTERVAL_DAY:
    case SQL_INTERVAL_HOUR:
    case SQL_INTERVAL_MINUTE:
    case SQL_INTERVAL_SECOND:
    case SQL_INTERVAL_DAY_TO_HOUR:
    case SQL_INTERVAL_DAY_TO_MINUTE:
    case SQL_INTERVAL_DAY_TO_SECOND:
    case SQL_INTERVAL_HOUR_TO_MINUTE:
    case SQL_INTERVAL_HOUR_TO_SECOND:
    case SQL_INTERVAL_MINUTE_TO_SECOND:
        ss << "DATETIME";
        break;

    default:
        ss << "UNKNOWN";
        break;
    }

    ss << (IS_NULLABLE == "NO" ? " NOT NULL" : " NULL");

    if (!COLUMN_DEF.empty())
    {
        ss << " DEFAULT " << COLUMN_DEF;
    }

    // Storing the native type as a comment hopefully helps the user figure out if the type deduced from the
    // SQL type is the appropriate MariaDB type.
    ss << " /* Type: " << TYPE_NAME << " */ ";

    return ss.str();
}

std::string fk_ref_option(int rule, std::string_view operation)
{
    std::ostringstream ss;

    switch (rule)
    {
    case SQL_CASCADE:
        ss << " ON " << operation << " CASCADE";
        break;

    case SQL_NO_ACTION:
        ss << " ON " << operation << " NO ACTION";
        break;

    case SQL_SET_NULL:
        ss << " ON " << operation << " SET NULL";
        break;

    case SQL_SET_DEFAULT:
        ss << " ON " << operation << " SET DEFAULT";
        break;

    default:
        break;
    }

    return ss.str();
}
}

namespace sql_etl
{

std::string GenericExtractor::create_table(mxq::ODBC& source, const Table& table)
{
    auto cols = source.columns(m_catalog, table.schema(), table.table());

    if (!cols)
    {
        throw problem("Failed to fetch column information: ", source.error());
    }

    // The ODBC API doesn't guarantee that the catalog functions return the results in any specific order.
    // In practice the results seem to be sorted by the ordinal positions of the fields but this can't
    // really be relied upon.
    std::sort(cols->begin(), cols->end(), [](const auto& lhs, const auto& rhs){
        return to_int(lhs[16]) < to_int(rhs[16]);
    });

    std::vector<std::string> parts;

    for (const auto& row : *cols)
    {
        parts.push_back(to_mariadb_type(row));
    }

    auto pk = source.primary_keys(m_catalog, table.schema(), table.table());

    if (!pk)
    {
        throw problem("Failed to fetch primary key information: ", source.error());
    }
    if (!pk->empty())
    {
        std::sort(pk->begin(), pk->end(), [](const auto& lhs, const auto& rhs){
            return to_int(lhs[4]) < to_int(rhs[4]);
        });

        auto fields = mxb::transform_join(*pk, [](const auto& row){
            return to_str(row[3]);
        }, ", ", "`");

        parts.push_back("PRIMARY KEY(" + fields + ")");
    }

    auto idx = source.statistics(m_catalog, table.schema(), table.table());

    if (!idx)
    {
        throw problem("Failed to fetch index information: ", source.error());
    }

    if (!idx->empty())
    {
        struct Index
        {
            bool                                     unique = false;
            std::vector<std::pair<std::string, int>> columns;
        };

        std::map<std::string, Index> indexes;

        for (const auto& row : *idx)
        {
            const std::string INDEX_NAME = to_str(row[5]);
            const int NON_UNIQUE = to_int(row[3]);

            if (!INDEX_NAME.empty())
            {
                auto& i = indexes[INDEX_NAME];
                const std::string COLUMN_NAME = to_str(row[8]);
                const std::string ASC_OR_DESC = to_str(row[9]);
                std::string idx_field = "`" + COLUMN_NAME + "`";

                if (ASC_OR_DESC == "A")
                {
                    idx_field += " ASC";
                }
                else if (ASC_OR_DESC == "D")
                {
                    idx_field += " DESC";
                }

                i.columns.emplace_back(std::move(idx_field), to_int(row[7]));
                i.unique = NON_UNIQUE == SQL_FALSE;
            }
        }

        for (auto& [name, i] : indexes)
        {
            std::sort(i.columns.begin(), i.columns.end(), [](const auto& lhs, const auto& rhs){
                return lhs.second < rhs.second;
            });

            auto fields = mxb::transform_join(i.columns, [](const auto& kv){
                return kv.first;
            }, ", ");

            parts.push_back("KEY `" + name + "`(" + fields + ")");
        }
    }

    auto fks = source.foreign_keys(m_catalog, table.schema(), table.table());

    if (!fks)
    {
        throw problem("Failed to fetch foreign key information: ", source.error());
    }

    if (!fks->empty())
    {
        struct ForeignKey
        {
            std::string on_update;
            std::string on_delete;
            std::string pk_schema;
            std::string pk_table;

            std::vector<std::tuple<std::string, std::string, int>> columns;
        };

        std::map<std::string, ForeignKey> keys;

        for (const auto& row : *fks)
        {
            const std::string PKTABLE_SCHEM = to_str(row[1]);
            const std::string PKTABLE_NAME = to_str(row[2]);
            const std::string PKCOLUMN_NAME = to_str(row[3]);
            const std::string FKTABLE_SCHEM = to_str(row[5]);
            const std::string FKTABLE_NAME = to_str(row[6]);
            const std::string FKCOLUMN_NAME = to_str(row[7]);
            const int KEY_SEQ = to_int(row[8]);
            const int UPDATE_RULE = to_int(row[9]);
            const int DELETE_RULE = to_int(row[10]);
            const std::string FK_NAME = to_str(row[11]);

            auto& fk = keys[FK_NAME];
            mxb_assert(fk.pk_schema.empty() || fk.pk_schema == PKTABLE_SCHEM);
            mxb_assert(fk.pk_table.empty() || fk.pk_table == PKTABLE_NAME);
            fk.pk_schema = PKTABLE_SCHEM;
            fk.pk_table = PKTABLE_NAME;
            fk.on_update = fk_ref_option(UPDATE_RULE, "UPDATE");
            fk.on_delete = fk_ref_option(DELETE_RULE, "DELETE");
            fk.columns.emplace_back(FKCOLUMN_NAME, PKCOLUMN_NAME, KEY_SEQ);
        }

        for (auto& [name, key] : keys)
        {
            std::sort(key.columns.begin(), key.columns.end(), [](const auto& lhs, const auto& rhs){
                return std::get<2>(lhs) < std::get<2>(rhs);
            });

            auto fk_fields = mxb::transform_join(key.columns, [](const auto& val){
                return std::get<0>(val);
            }, ", ", "`");

            auto pk_fields = mxb::transform_join(key.columns, [](const auto& val){
                return std::get<1>(val);
            }, ", ", "`");

            std::ostringstream ss;
            ss << "FOREIGN KEY `" << name << "` (" << fk_fields << ") "
               << "REFERENCES `" << key.pk_schema << "`.`" << key.pk_table << "`(" << pk_fields << ")"
               << key.on_update << key.on_delete;

            parts.push_back(ss.str());
        }
    }

    std::ostringstream ss;
    std::string_view create_type = to_create_table(table.create_mode());

    ss << create_type << " `" << table.schema() << "`.`" << table.table() << "` (\n"
       << mxb::transform_join(parts, [](const auto& val){
        return "  " + val;
    }, ",\n") << "\n)";

    return ss.str();
}

std::string GenericExtractor::select(mxq::ODBC& source, const Table& table)
{
    auto cols = source.columns(m_catalog, table.schema(), table.table());

    if (!cols)
    {
        throw problem("Failed to fetch table information: ", source.error());
    }

    std::vector<std::string> names;
    for (const auto& row : *cols)
    {
        names.push_back(to_str(row[3]));
    }

    std::ostringstream ss;
    ss << "SELECT " << mxb::join(names, ", ", "\"")
       << " FROM \"" << table.schema() << "\".\"" << table.table() << "\"";
    return ss.str();
}

std::string GenericExtractor::insert(mxq::ODBC& source, const Table& table)
{
    auto cols = source.columns(m_catalog, table.schema(), table.table());

    if (!cols)
    {
        throw problem("Failed to fetch table information: ", source.error());
    }

    std::vector<std::string> names;
    std::vector<std::string> placeholders;

    for (const auto& row : *cols)
    {
        names.push_back(to_str(row[3]));
        placeholders.push_back("?");
    }

    std::ostringstream ss;
    ss << "INSERT INTO `" << table.schema() << "`.`" << table.table() << "`"
       << "(" << mxb::join(names, ",", "`") << ") VALUES (" << mxb::join(placeholders, ",") << ")";
    return ss.str();
}
}
