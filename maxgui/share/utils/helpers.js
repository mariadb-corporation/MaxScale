/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { t } from 'typy'
import { isCancelled } from '@share/axios/handlers'
import { format } from 'date-fns'
import { logger } from '@share/plugins/logger'
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
    differenceInCalendarDays,
} from 'date-fns'

export const uuidv1 = require('uuid').v1

export const deepDiff = require('deep-diff')

export const lodash = {
    isEmpty: require('lodash/isEmpty'),
    cloneDeep: require('lodash/cloneDeep'),
    cloneDeepWith: require('lodash/cloneDeepWith'),
    isEqual: require('lodash/isEqual'),
    xorWith: require('lodash/xorWith'),
    uniqueId: require('lodash/uniqueId'),
    get: require('lodash/get'),
    set: require('lodash/set'),
    unionBy: require('lodash/unionBy'),
    pick: require('lodash/pick'),
    pickBy: require('lodash/pickBy'),
    uniqBy: require('lodash/uniqBy'),
    merge: require('lodash/merge'),
    mergeWith: require('lodash/mergeWith'),
    differenceWith: require('lodash/differenceWith'),
    countBy: require('lodash/countBy'),
    keyBy: require('lodash/keyBy'),
    values: require('lodash/values'),
    camelCase: require('lodash/camelCase'),
    escapeRegExp: require('lodash/escapeRegExp'),
    objGet: require('lodash/get'),
    groupBy: require('lodash/groupBy'),
    update: require('lodash/update'),
    sortBy: require('lodash/sortBy'),
    debounce: require('lodash/debounce'),
    flatMap: require('lodash/flatMap'),
    uniq: require('lodash/uniq'),
}

export const immutableUpdate = require('immutability-helper')

export function delay(t, v) {
    return new Promise(function(resolve) {
        setTimeout(resolve.bind(null, v), t)
    })
}

export function dynamicColors(dataIndex) {
    const palette = [
        'rgba(171,199,74,1)',
        'rgba(245,157,52,1)',
        'rgba(47,153,163,1)',
        'rgba(150,221,207,1)',
        'rgba(14,155,192,1)',
        'rgba(125,208,18,1)',
        'rgba(14,100,136,1)',
        'rgba(66,79,98,1)',
        'rgba(163,186,192,1)',
        'rgba(0,53,69,1)',
        'rgba(45,156,219,1)',
        'rgba(235,87,87,1)',
        'rgba(48,48,51,1)',
        'rgba(134,205,223,1)',
        'rgba(76,76,79,1)',
    ]
    return palette[dataIndex % palette.length]
}

/**
 * This function replaces a char in payload.str at payload.index with payload.newChar
 * @param {String} payload.str - string to be processed
 * @param {Number} payload.index - index of char that will be replaced
 * @param {String} payload.newChar - new char
 * @returns new string
 */
export function strReplaceAt({ str, index, newChar }) {
    if (index > str.length - 1) return str
    return str.substr(0, index) + newChar + str.substr(index + 1)
}

/**
 * @param {Object|String} error - Error object or string that returns from try catch
 * @return {Array} An array of error string
 */
export function getErrorsArr(error) {
    if (typeof error === 'string') return [error]
    else {
        const errors = lodash.get(error, 'response.data.errors') || []
        // fetch-cmd-result has error messages inside meta object
        const metaErrs = lodash.get(error, 'response.data.meta.errors') || []
        if (errors.length) return errors.map(ele => `${ele.detail}`)
        if (metaErrs.length) return metaErrs.map(ele => `${ele.detail}`)
        return [error]
    }
}

/**
 * Handle format date value
 * @param {String} param.value - String date to be formatted
 * @param {String} param.formatType - format type (default is DATE_RFC2822: E, dd MMM yyyy HH:mm:ss)
 * @return {String} new date format
 */
export function dateFormat({ value, formatType = 'E, dd MMM yyyy HH:mm:ss' }) {
    return format(new Date(value), formatType)
}

export function capitalizeFirstLetter(string) {
    return string.charAt(0).toUpperCase() + string.slice(1)
}

/**
 * Case insensitive check if substring is included in source string
 * @param {String} str source string
 * @param {String} subStr sub string to be searched
 * @return {Boolean} Return Boolean
 */
export function ciStrIncludes(str, subStr) {
    return str.toLowerCase().includes(subStr.toLowerCase())
}

/**
 * Vue.nextTick is not enough for rendering large DOM.
 * This function uses double RAF technique to wait for a browser repaint
 * @param {Function} callback callback function
 */
export function doubleRAF(callback) {
    requestAnimationFrame(() => {
        requestAnimationFrame(callback)
    })
}

/**
 * @private
 * @param {String} text
 */
function fallbackCopyTextToClipboard(text) {
    let txtArea = document.createElement('textarea')
    txtArea.value = text
    txtArea.style = { ...txtArea.style, top: 0, left: 0, position: 'fixed' }
    document.body.appendChild(txtArea)
    txtArea.focus()
    txtArea.select()
    document.execCommand('copy')
    document.body.removeChild(txtArea)
}

/**
 * @param {String} text
 */
export function copyTextToClipboard(text) {
    if (navigator.clipboard) {
        navigator.clipboard.writeText(text)
    } else fallbackCopyTextToClipboard(text)
}

/**
 * Prevents non-integer input by cancelling the event if the pressed
 * key does not represent an integer.
 * @param {KeyboardEvent} e - The keyboard event object.
 */
export function preventNonInteger(e) {
    if (!e.key.match(/^[-]?\d*$/g)) e.preventDefault()
}

/**
 * This allows user to enter only number
 * @param {Event} e - input evt
 */
export function preventNonNumericalVal(e) {
    if (!e.key.match(/^\d*$/g)) e.preventDefault()
}

/**
 * An async await wrapper
 * @param {Promise} promise
 * @returns { Promise }
 */
export async function tryAsync(promise) {
    return promise
        .then(data => [null, data])
        .catch(err => {
            if (!isCancelled(err)) logger.error(getErrorsArr(err).toString())
            return [err, undefined]
        })
}

/**
 * Creates a set of Vuex mutations based on the provided states
 * @param {Object} states - An object representing the states for which mutations are to be created.
 * @returns {Object} - An object containing Vuex mutations.
 */
export function genSetMutations(states) {
    return Object.keys(states).reduce(
        (mutations, name) => ({
            ...mutations,
            [`SET_${name.toUpperCase()}`]: (state, payload) => (state[name] = payload),
        }),
        {}
    )
}

export function scrollToFirstErrMsgInput() {
    let invalidEles = document.getElementsByClassName('v-messages__message')
    return invalidEles[0].scrollIntoView({
        behavior: 'smooth',
        block: 'center',
        inline: 'start',
    })
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
    return socket || `${address}:${port}`
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

/**
 * @param {String|Number} value value to be handled
 * @returns {String} Returns px unit string
 */
export function handleAddPxUnit(value) {
    if (typeof value === 'number') return `${value}px`
    return value
}

/**
 * This function is not working on macOs as the scrollbar is only showed when scrolling.
 * However, on Macos, scrollbar is placed above the content (overlay) instead of taking up space
 * of the content. So in macOs, this returns 0.
 * @returns {Number} scrollbar width
 */
export function getScrollbarWidth() {
    // Creating invisible container
    const outer = document.createElement('div')
    outer.style.visibility = 'hidden'
    outer.style.overflow = 'scroll' // forcing scrollbar to appear
    outer.style.msOverflowStyle = 'scrollbar' // needed for WinJS apps
    document.body.appendChild(outer)

    // Creating inner element and placing it in the container
    const inner = document.createElement('div')
    outer.appendChild(inner)

    // Calculating difference between container's full width and the child width
    const scrollbarWidth = outer.offsetWidth - inner.offsetWidth

    // Removing temporary elements from the DOM
    outer.parentNode.removeChild(outer)

    return scrollbarWidth
}
