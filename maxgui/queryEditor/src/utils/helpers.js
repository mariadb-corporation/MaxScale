/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { lodash } from '@share/utils/helpers'
import sqlFormatter from '@queryEditorSrc/components/SqlEditor/formatter'
import { splitQuery as splitSql, mysqlSplitterOptions } from 'dbgate-query-splitter'

export const deepDiff = require('deep-diff')

export function formatSQL(v) {
    return sqlFormatter(v, {
        indent: '   ',
        uppercase: true,
        linesBetweenQueries: 2,
    })
}

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

/**
 * @param {String} str plain identifier string. e.g. db_name.tbl_name
 * @return {String} Return escaped identifier string. e.g.  \`db_name\`.\`tbl_name\`
 */
export function escapeIdentifiers(str) {
    return str
        .split('.')
        .map(identifier => `\`${identifier}\``)
        .join('.')
}

/**
 * @param {Array} payload.columns table fields
 * @param {Array} payload.rows table rows
 * @return {Array} Return object rows
 */
export function getObjectRows({ columns, rows }) {
    return rows.map(row => {
        const obj = {}
        columns.forEach((c, index) => {
            obj[c] = row[index]
        })
        return obj
    })
}

export function pxToPct({ px, containerPx }) {
    return (px / containerPx) * 100
}

/**
 * This adds number of days to current date
 * @param {Number} days - Number of days
 * @returns {String} - returns date
 */
export function addDaysToNow(days) {
    let curr = new Date()
    return curr.setDate(curr.getDate() + days)
}
/**
 * This returns number of days between target timestamp and current date
 * @param {Object} param.moment - moment
 * @param {String} param.timestamp - target unix timestamp
 * @returns {Number} - days diff
 */
export function daysDiff({ moment, timestamp }) {
    const now = moment().startOf('day')
    const end = moment(timestamp).startOf('day')
    return end.diff(now, 'days')
}

//TODO: objects Re-order in array diff
/**
 * @param {Array} payload.base - initial base array
 * @param {Array} payload.newArr - new array
 * @param {String} payload.idField - key name of unique value in each object in array
 * @returns {Map} - returns  Map { unchanged: [{}], added: [{}], updated:[{}], removed:[{}] }
 */
export function arrOfObjsDiff({ base, newArr, idField }) {
    // stored ids of two arrays to get removed objects
    const baseIds = []
    const newArrIds = []
    const baseMap = new Map()
    base.forEach(o => {
        baseIds.push(o[idField])
        baseMap.set(o[idField], o)
    })

    const resultMap = new Map()
    resultMap.set('unchanged', [])
    resultMap.set('added', [])
    resultMap.set('removed', [])
    resultMap.set('updated', [])

    newArr.forEach(obj2 => {
        newArrIds.push(obj2[idField])
        const obj1 = baseMap.get(obj2[idField])
        if (!obj1) resultMap.set('added', [...resultMap.get('added'), obj2])
        else if (lodash.isEqual(obj1, obj2))
            resultMap.set('unchanged', [...resultMap.get('unchanged'), obj2])
        else {
            const diff = deepDiff(obj1, obj2)
            const objDiff = { oriObj: obj1, newObj: obj2, diff }
            resultMap.set('updated', [...resultMap.get('updated'), objDiff])
        }
    })
    const removedIds = baseIds.filter(id => !newArrIds.includes(id))
    const removed = removedIds.map(id => baseMap.get(id))
    resultMap.set('removed', removed)
    return resultMap
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
