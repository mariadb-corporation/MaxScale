/*
 * Copyright (c) 2023 MariaDB plc
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
import { splitQuery as splitSql, mysqlSplitterOptions } from 'dbgate-query-splitter'
import { formatDialect, mariadb } from 'sql-formatter'
import { capitalizeFirstLetter } from '@share/utils/helpers'

/**
 * This function splits the query into statements accurately for most cases
 * except compound statements. It requires the presence of DELIMITER to split
 * correctly.
 * For example: below sql will be splitted accurately into 1 statement.
 * DELIMITER //
 * IF (1>0) THEN BEGIN NOT ATOMIC SELECT 1; END ; END IF;
 * DELIMITER ;
 * This function should be now only used for counting the number of statements.
 * @param {string} sql
 * @returns {string[]}
 */
export function splitQuery(sql) {
    return splitSql(sql, mysqlSplitterOptions)
}

export function formatSQL(v) {
    return formatDialect(v, { dialect: mariadb, tabWidth: 2, keywordCase: 'upper' })
}

const IDENTIFIED_PATTERN = /IDENTIFIED\s*/gim
const PWD_PATTERN = /['"][^'"]*['"]/

const IDENTIFIED_BY_PATTERN = new RegExp(
    '(\\b' + IDENTIFIED_PATTERN.source + 'BY(?:\\s*PASSWORD)?\\s*)' + PWD_PATTERN.source,
    'gim'
)
const IDENTIFIED_PLUGIN_PATTERN = new RegExp(
    '(\\b' +
        IDENTIFIED_PATTERN.source +
        '(VIA|WITH)\\s*\\w+\\s*(USING|AS)\\s*)' +
        PWD_PATTERN.source,
    'gim'
)
const PLUGIN_PWD_PATTERN = new RegExp(`PASSWORD\\s*\\(${PWD_PATTERN.source}`, 'gim')

export function maskQueryPwd(query) {
    if (query.match(IDENTIFIED_PATTERN) || query.match(PLUGIN_PWD_PATTERN))
        return query
            .replace(IDENTIFIED_BY_PATTERN, "$1'***'")
            .replace(PLUGIN_PWD_PATTERN, "PASSWORD('***'")
            .replace(IDENTIFIED_PLUGIN_PATTERN, `$1'***'`)
    return query
}

export function queryResErrToStr(result) {
    return Object.keys(result).reduce((msg, key) => {
        msg += `${capitalizeFirstLetter(key)}: ${result[key]}. `
        return msg
    }, '')
}
