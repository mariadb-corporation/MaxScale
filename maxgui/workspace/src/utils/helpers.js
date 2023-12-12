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
import { t } from 'typy'
import { startOfDay, differenceInCalendarDays } from 'date-fns'
import { lodash, capitalizeFirstLetter } from '@share/utils/helpers'
import { formatDialect, mariadb } from 'sql-formatter'
import TableParser from '@wsSrc/utils/TableParser'
import { splitQuery as splitSql, mysqlSplitterOptions } from 'dbgate-query-splitter'

export const tableParser = new TableParser()

export const deepDiff = require('deep-diff')

export function formatSQL(v) {
    return formatDialect(v, { dialect: mariadb, tabWidth: 2, keywordCase: 'upper' })
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
 * @param {String} identifier  identifier name
 * @return {String} Return quoted identifier name. e.g.  `db_name`
 */
export function quotingIdentifier(identifier) {
    if (!t(identifier).isString || !identifier) return identifier
    return `\`${identifier.replace(/`/g, '``')}\``
}

/**
 * @param {String} identifier  quoted identifier name
 * @return {String} Return unquoted identifier name
 */
export function unquoteIdentifier(identifier) {
    if (!t(identifier).isString) return identifier
    const quoteMark = identifier.slice(0, 1)
    return identifier.slice(1, -1).replace(new RegExp(quoteMark + quoteMark, 'g'), quoteMark)
}

export function escapeSingleQuote(str) {
    if (!t(str).isString) return str
    return str.replace(/'/g, "\\'")
}
/**
 * @param {Array} payload.fields - fields
 * @param {Array} payload.rows - 2d array
 * @return {Array} Return object rows
 */
export function map2dArr({ fields, arr }) {
    return arr.map(item => {
        const obj = {}
        fields.forEach((c, index) => {
            obj[c] = item[index]
        })
        return obj
    })
}

export function pxToPct({ px, containerPx }) {
    return (px / containerPx) * 100
}

export function pctToPx({ pct, containerPx }) {
    return (pct * containerPx) / 100
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
 * @param {String} timestamp - target unix timestamp
 * @returns {Number} - days diff
 */
export function daysDiff(timestamp) {
    const now = startOfDay(new Date())
    const end = startOfDay(new Date(timestamp))
    return differenceInCalendarDays(end, now)
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

export function queryResErrToStr(result) {
    return Object.keys(result).reduce((msg, key) => {
        msg += `${capitalizeFirstLetter(key)}: ${result[key]}. `
        return msg
    }, '')
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

function createCanvasFrame(canvas) {
    // create new canvas with white background
    let desCanvas = document.createElement('canvas')
    desCanvas.width = canvas.width
    desCanvas.height = canvas.height
    let destCtx = desCanvas.getContext('2d')
    destCtx.fillStyle = '#FFFFFF'
    destCtx.fillRect(0, 0, desCanvas.width, desCanvas.height)
    //draw the original canvas onto the destination canvas
    destCtx.drawImage(canvas, 0, 0)
    destCtx.scale(2, 2)
    return desCanvas
}
/**
 * @param {object} param
 * @param {HTMLElement} param.canvas - canvas element
 * @param {string} param.fileName
 */
export function exportToJpeg({ canvas, fileName }) {
    const desCanvas = createCanvasFrame(canvas)
    const imageUrl = desCanvas.toDataURL('image/jpeg', 1.0)
    let a = document.createElement('a')
    a.href = imageUrl
    a.download = `${fileName}.jpeg`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
}

export const addComma = () => ', '

/**
 * @param {string} url  from axios response config.url
 * @returns {string} connection id
 */
export function getConnId(url) {
    const matched = /\/sql\/([a-zA-z0-9-]*?)\//g.exec(url) || []
    return matched.length > 1 ? matched[1] : null
}
