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
import column_types from './column_types'

const column_types_map = new Map()
column_types.forEach(category =>
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
 *
 * @param {String} payload.datatype - column data type to be checked
 * @param {Array} payload.supportedTypes - dist
 * @returns {Boolean} - returns true if provided datatype can be found in supportedTypes
 */
const checkOptSupport = ({ datatype, supportedTypes }) =>
    supportedTypes.some(v => datatype.toUpperCase().includes(v))

/**
 * @param {String} datatype - column data type to be checked
 * @returns {Boolean} - returns true if provided data type supports charset/collation
 */
export const check_charset_support = datatype =>
    checkOptSupport({ datatype, supportedTypes: typesSupportCharset })

/**
 * @param {String} datatype - column data type to be checked
 * @returns {Boolean} - returns true if provided data type supports UNSIGNED|SIGNED and ZEROFILL
 */
export const check_UN_ZF_support = datatype =>
    checkOptSupport({ datatype, supportedTypes: typesSupport_UN_ZF })

/**
 * @param {String} datatype - column data type to be checked
 * @returns {Boolean} - returns true if provided data type supports AUTO_INCREMENT
 */
export const check_AI_support = datatype =>
    checkOptSupport({ datatype, supportedTypes: typesSupport_AI })
