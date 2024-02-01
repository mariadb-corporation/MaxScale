/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @public
 * @returns {Array} - column types
 */
const getColumnTypes = () => [
    {
        header: 'String',
        types: [
            { value: 'CHAR' },
            { value: 'NATIONAL CHAR' },
            { value: 'VARCHAR()' },
            { value: 'NATIONAL VARCHAR()' },
            { value: 'TINYTEXT' },
            { value: 'TEXT' },
            { value: 'MEDIUMTEXT' },
            { value: 'LONGTEXT' },
            { value: 'JSON' },
            { value: 'TINYBLOB' },
            { value: 'BLOB' },
            { value: 'MEDIUMBLOB' },
            { value: 'LONGBLOB' },
            { value: 'BINARY()' },
            { value: 'VARBINARY()' },
            { value: 'UUID' },
        ],
    },
    { header: 'Bit ', types: [{ value: 'BIT' }] },
    { header: 'Set', types: [{ value: 'SET()' }, { value: 'ENUM()' }] },
    { header: 'Binary', types: [{ value: 'INET6' }] },
    {
        header: 'Integer',
        types: [
            { value: 'TINYINT' },
            { value: 'SMALLINT' },
            { value: 'MEDIUMINT' },
            { value: 'INT' },
            { value: 'BIGINT' },
        ],
    },
    {
        header: 'Fixed Num',
        types: [{ value: 'DECIMAL' }],
    },
    {
        header: 'Float',
        types: [{ value: 'FLOAT' }, { value: 'DOUBLE' }],
    },
    // Date and Time Data Type
    { header: 'Time', types: [{ value: 'TIME' }] },
    {
        header: 'Date',
        types: [{ value: 'YEAR' }, { value: 'DATE' }],
    },
    {
        header: 'Date/Time',
        types: [{ value: 'TIMESTAMP' }, { value: 'DATETIME' }],
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
]

const column_types_map = new Map()
getColumnTypes().forEach(category =>
    column_types_map.set(
        category.header,
        //Remove round brackets
        category.types.map(type => type.value.replace(/\([^)]*\)/g, ''))
    )
)
const typesSupportCharset = [
    'CHAR',
    'NATIONAL CHAR',
    'VARCHAR',
    'NATIONAL VARCHAR',
    'TINYTEXT',
    'TEXT',
    'MEDIUMTEXT',
    'LONGTEXT',
    ...column_types_map.get('Set'),
]
const typesSupport_AI = [...column_types_map.get('Integer'), 'SERIAL']

const typesSupport_UN_ZF = [
    ...typesSupport_AI,
    ...column_types_map.get('Float'),
    ...column_types_map.get('Fixed Num'),
]

/**
 * @private
 * @param {String} payload.datatype - column data type to be checked
 * @param {Array} payload.supportedTypes - dist
 * @returns {Boolean} - returns true if provided datatype can be found in supportedTypes
 */
const checkOptSupport = ({ datatype, supportedTypes }) =>
    supportedTypes.some(v => datatype.toUpperCase().includes(v))

/**
 * @public
 * @param {String} datatype - column data type to be checked
 * @returns {Boolean} - returns true if provided data type supports charset/collation
 */
const checkCharsetSupport = datatype =>
    checkOptSupport({ datatype, supportedTypes: typesSupportCharset })

/**
 * @public
 * @param {String} datatype - column data type to be checked
 * @returns {Boolean} - returns true if provided data type supports UNSIGNED|SIGNED and ZEROFILL
 */
const checkUniqueZeroFillSupport = datatype =>
    checkOptSupport({ datatype, supportedTypes: typesSupport_UN_ZF })

/**
 * @public
 * @param {String} datatype - column data type to be checked
 * @returns {Boolean} - returns true if provided data type supports AUTO_INCREMENT
 */
const checkAutoIncrementSupport = datatype =>
    checkOptSupport({ datatype, supportedTypes: typesSupport_AI })

const checkFkSupport = datatype => {
    const str = datatype.toUpperCase()
    return !str.includes('TEXT') && !str.includes('BLOB')
}
const integerTypes = typesSupport_AI

export {
    getColumnTypes,
    checkAutoIncrementSupport,
    checkUniqueZeroFillSupport,
    checkCharsetSupport,
    checkFkSupport,
    integerTypes,
}
