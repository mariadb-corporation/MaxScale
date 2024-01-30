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
import { isCancelled } from '@share/axios/handlers'
import { format, intervalToDuration, formatDuration } from 'date-fns'
import { logger } from '@share/plugins/logger'
export const uuidv1 = require('uuid').v1

export const immutableUpdate = require('immutability-helper')

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

export function flattenTree(tree) {
    return lodash.flatMap(tree, node => {
        if (node.children && node.children.length === 0) return [node]
        return [node, ...flattenTree(node.children)]
    })
}

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
 * @private
 * This copies inherit styles from srcNode to dstNode
 * @param {Object} payload.srcNode - html node to be copied
 * @param {Object} payload.dstNode - target html node to pasted
 */
function copyNodeStyle({ srcNode, dstNode }) {
    const computedStyle = window.getComputedStyle(srcNode)
    Array.from(computedStyle).forEach(key =>
        dstNode.style.setProperty(
            key,
            computedStyle.getPropertyValue(key),
            computedStyle.getPropertyPriority(key)
        )
    )
}
export function removeTargetDragEle(dragTargetId) {
    let elem = document.getElementById(dragTargetId)
    if (elem) elem.parentNode.removeChild(elem)
}
export function addDragTargetEle({ e, dragTarget, dragTargetId }) {
    let cloneNode = dragTarget.cloneNode(true)
    cloneNode.setAttribute('id', dragTargetId)
    cloneNode.textContent = dragTarget.textContent
    copyNodeStyle({ srcNode: dragTarget, dstNode: cloneNode })
    cloneNode.style.position = 'absolute'
    cloneNode.style.top = e.clientY + 'px'
    cloneNode.style.left = e.clientX + 'px'
    cloneNode.style.zIndex = 9999
    document.getElementById('app').appendChild(cloneNode)
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
 * This is faster than lodash cloneDeep.
 * But it comes with pitfalls, so it's only suitable for json data
 * @param {Object} data - json data
 * @returns {Object}
 */
export function stringifyClone(data) {
    return JSON.parse(JSON.stringify(data))
}

/**
 * An async await wrapper
 * @param {Promise} promise
 * @returns { Promise }
 */
export async function to(promise) {
    return promise
        .then(data => [null, data])
        .catch(err => {
            if (!isCancelled(err)) logger.error(getErrorsArr(err).toString())
            return [err, undefined]
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
