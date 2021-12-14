/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default [
    {
        header: 'String',
        types: [
            { value: 'CHAR()' },
            { value: 'NATIONAL CHAR()' }, // set charset to utf8
            { value: 'VARCHAR()' },
            { value: 'NATIONAL VARCHAR()' }, // set charset to utf8
            { value: 'TINYTEXT' },
            { value: 'TEXT()' },
            { value: 'MEDIUMTEXT' },
            { value: 'LONGTEXT' },
            { value: 'JSON' }, // LONGTEXT alias with a default json_valid() CHECK
            { value: 'TINYBLOB' },
            { value: 'BLOB()' },
            { value: 'MEDIUMBLOB' },
            { value: 'LONGBLOB' },
            { value: 'BINARY()' },
            { value: 'VARBINARY()' },
            { value: 'UUID' },
        ],
    },
    { header: 'Bit ', types: [{ value: 'BIT()' }] },
    { header: 'Set', types: [{ value: 'SET()' }, { value: 'ENUM()' }] },
    { header: 'Binary', types: [{ value: 'INET6' }] },
    {
        header: 'Integer',
        types: [
            { value: 'TINYINT()' },
            { value: 'SMALLINT()' },
            { value: 'MEDIUMINT()' },
            { value: 'INT()' },
            { value: 'BIGINT()' },
        ],
    },
    {
        header: 'Fixed Num',
        types: [{ value: 'DECIMAL()' }],
    },
    {
        header: 'Float',
        types: [{ value: 'FLOAT()' }, { value: 'DOUBLE()' }],
    },
    // Date and Time Data Type
    { header: 'Time', types: [{ value: 'TIME()' }] },
    {
        header: 'Date',
        types: [{ value: 'YEAR()' }, { value: 'DATE' }],
    },
    {
        header: 'Date/Time',
        types: [{ value: 'TIMESTAMP()' }, { value: 'DATETIME()' }],
    },
    {
        header: 'Geometry',
        types: [
            { value: 'POINT' },
            { value: 'LINESTRING' },
            { value: 'MULTIPOINT' },
            { value: 'MULTILINESTRING' },
            { value: 'MULTIPOLYGON' },
            { value: 'POLYGON' },
            { value: 'GEOMETRYCOLLECTION' },
            { value: 'GEOMETRY' },
        ],
    },
    {
        header: 'Alias',
        types: [
            { value: 'SERIAL' }, //alias for BIGINT UNSIGNED NOT NULL AUTO_INCREMENT UNIQUE.
        ],
    },
    {
        header: 'SQL/PL',
        types: [
            { value: 'NUMBER()' },
            { value: 'RAW()' },
            { value: 'VARCHAR2()' },
            { value: 'CLOB' },
        ],
    },
]
