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
import { lodash, dynamicColors, strReplaceAt } from '@share/utils/helpers'
import { TIME_REF_POINTS } from '@src/constants'
import {
    getUnixTime,
    subMonths,
    subDays,
    subWeeks,
    startOfDay,
    endOfYesterday,
    parseISO,
    intervalToDuration,
    formatDuration,
} from 'date-fns'
export const stringifyNullOrUndefined = value =>
    typeof value === 'undefined' || value === null ? String(value) : value

/**
 * This export function creates dataset object for stream-line-chart
 * @param {String} payload.label - label for dataset
 * @param {Number} payload.value - value for dataset
 * @param {Number} payload.colorIndex - index of color from color palette of dynamicColors helper
 * @param {Number} [payload.timestamp] - if provided, otherwise using Date.now() (optional)
 * @param {String|Number} [payload.id] - unique id (optional)
 * @param {Array} [payload.data] - data for dataset (optional)
 * @returns {Object} dataset object
 */
export function genLineStreamDataset({ label, value, colorIndex, timestamp, id, data }) {
    const color = dynamicColors(colorIndex)
    const indexOfOpacity = color.lastIndexOf(')') - 1
    const backgroundColor = strReplaceAt({ str: color, index: indexOfOpacity, newChar: '0.1' })
    const lineColor = strReplaceAt({ str: color, index: indexOfOpacity, newChar: '0.4' })
    let time = Date.now()
    if (timestamp) time = timestamp
    let dataset = {
        label: label,
        id: label,
        type: 'line',
        // background of the line
        fill: true,
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        borderWidth: 1,
        data: [{ x: time, y: value }],
    }
    if (id) dataset.resourceId = id
    if (data) dataset.data = data
    return dataset
}

/**
 * @param {Array} payload.arr - Array of objects
 * @param {String} payload.pickBy - property to find the minimum value
 * @returns {Number} - returns min value
 */
export function getMin({ arr, pickBy }) {
    return Math.min(...arr.map(item => item[pickBy]))
}

/**
 * @param {Array} payload.arr - Array of objects
 * @param {String} payload.pickBy - property to find the minimum value
 * @returns {String} - returns the most frequent value
 */
export function getMostFreq({ arr, pickBy }) {
    let countObj = lodash.countBy(arr, pickBy)
    return Object.keys(countObj).reduce((a, b) => (countObj[a] > countObj[b] ? a : b), [])
}

/**
 * Return either socket path or TCP address
 * @param {Object} parameters
 * @returns {String} - either socket path or TCP address
 */
export function getAddress(parameters) {
    const { socket, address, port } = parameters
    return `${socket ? socket : `${address}:${port}`}`
}

export function validateHexColor(color) {
    return Boolean(color.match(/^#[0-9A-F]{6}$/i))
}

/**
 * @param {string} param.v - valid ISO date string or a value in TIME_REF_POINTS
 * @param {boolean} [param.toTimestamp] - returns timestamp if true, otherwise Date object
 * @returns {number|object}
 */
export function parseDateStr({ v, toTimestamp = false }) {
    const {
        NOW,
        START_OF_TODAY,
        END_OF_YESTERDAY,
        START_OF_YESTERDAY,
        NOW_MINUS_2_DAYS,
        NOW_MINUS_LAST_WEEK,
        NOW_MINUS_LAST_2_WEEKS,
        NOW_MINUS_LAST_MONTH,
    } = TIME_REF_POINTS
    let res
    switch (v) {
        case NOW:
            res = new Date()
            break
        case START_OF_TODAY:
            res = startOfDay(new Date())
            break
        case END_OF_YESTERDAY:
            res = endOfYesterday(new Date())
            break
        case START_OF_YESTERDAY:
            res = startOfDay(subDays(new Date(), 1))
            break
        case NOW_MINUS_2_DAYS:
            res = subDays(new Date(), 2)
            break
        case NOW_MINUS_LAST_WEEK:
            res = subWeeks(new Date(), 1)
            break
        case NOW_MINUS_LAST_2_WEEKS:
            res = subWeeks(new Date(), 2)
            break
        case NOW_MINUS_LAST_MONTH:
            res = subMonths(new Date(), 1)
            break
        default:
            res = parseISO(v)
    }
    return toTimestamp ? getUnixTime(res) : res
}

export function flattenTree(tree) {
    return lodash.flatMap(tree, node => {
        if (node.children && node.children.length === 0) return [node]
        return [node, ...flattenTree(node.children)]
    })
}

const padTimeNumber = num => num.toString().padStart(2, '0')
/**
 * @param {Number} sec - seconds
 * @returns Human-readable time, e.g. 1295222 -> 14 Days 23:47:02
 */
export function uptimeHumanize(sec) {
    const duration = intervalToDuration({ start: 0, end: sec * 1000 })
    const formattedDuration = formatDuration(duration, {
        format: ['years', 'months', 'days'].filter(unit => duration[unit] !== 0),
    })
    const formattedTime = [duration.hours, duration.minutes, duration.seconds]
        .map(padTimeNumber)
        .join(':')

    return `${formattedDuration} ${formattedTime}`
}
