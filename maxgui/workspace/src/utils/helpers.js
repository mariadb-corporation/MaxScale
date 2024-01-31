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

export const immutableUpdate = require('immutability-helper')

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
