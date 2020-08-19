/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'

export const isEmpty = require('lodash/isEmpty')
export const cloneDeep = require('lodash/cloneDeep')
export const isEqual = require('lodash/isEqual')
export const xorWith = require('lodash/xorWith')
export const uniqueId = require('lodash/uniqueId')

export const lodash = {
    isEmpty: isEmpty,
    cloneDeep: cloneDeep,
    isEqual: isEqual,
    xorWith: xorWith,
    uniqueId: uniqueId,
}

export function isNull(v) {
    return v === null
}

export function isUndefined(v) {
    return v === undefined
}

export function isFunction(v) {
    return typeof v === 'function'
}

export function isNotEmptyObj(v) {
    return !isNull(v) && !Array.isArray(v) && typeof v === 'object' && !isEmpty(v)
}
export function isNotEmptyArray(v) {
    return !isNull(v) && Array.isArray(v) && v.length > 0
}

export function getCookie(name) {
    let value = '; ' + document.cookie
    let parts = value.split('; ' + name + '=')
    if (parts.length == 2)
        return parts
            .pop()
            .split(';')
            .shift()
}

export function deleteCookie(name) {
    document.cookie = name + '=; expires=Thu, 01 Jan 1970 00:00:01 GMT;'
}

export function range(start, end) {
    if (isNaN(start) || isNaN(end)) return
    return Math.floor(Math.random() * (end - start + 1)) + start
}

//------------------------- Helper functions to display icon -------------------------------
export function serviceStateIcon(serviceState) {
    if (serviceState) {
        if (serviceState.includes('Started')) return 1
        if (serviceState.includes('Stopped')) return 2
        if (serviceState.includes('Allocated') || serviceState.includes('Failed')) return 0
        else return ''
    } else return ''
}
export function serverStateIcon(serverState) {
    let result = 2 // warning icon, warning text
    if (serverState) {
        // error icon, unhealthy text
        if (serverState === 'Running' || serverState.includes('Down')) result = 0
        // healthy icon, healthy text
        else if (serverState.includes('Running')) result = 1
        // warning icon
        if (serverState.includes('Maintenance')) result = 2
    }
    return result
}
export function monitorStateIcon(monitorState) {
    if (monitorState) {
        if (monitorState.includes('Running')) return 1
        if (monitorState.includes('Stopped')) return 2
        else return ''
    } else return ''
}
export function listenerStateIcon(state) {
    if (state) {
        if (state === 'Running') return 1
        else if (state === 'Stopped') return 2
        else if (state === 'Failed') return 0
    } else return ''
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
        'rgba(125,208,18,1)',
        'rgba(14,100,136,1)',
        'rgba(66,79,98,1)',
        'rgba(0,53,69,1)',
        'rgba(45,156,219,1)',
    ]
    return palette[dataIndex % palette.length]
}

export function strReplaceAt(str, index, chr) {
    if (index > str.length - 1) return str
    return str.substr(0, index) + chr + str.substr(index + 1)
}

/**
 * @param {Object} error Error object that returns from try catch
 * @return {Array} An array of error string
 */
export function getErrorsArr(error) {
    let errorsArr = [error]

    if (!isUndefined(error.response) && !isUndefined(error.response.data.errors)) {
        errorsArr = error.response.data.errors.map(ele => `${ele.detail}`)
    }

    return errorsArr
}

/**
 * @param {Array} OurArray Array of objects to be grouped by specified property name of object
 * @param {String} property Property to be grouped by
 * @return {Array} Return an object group by specified property name
 */
export function groupBy(array, property) {
    return array.reduce(function(accumulator, object) {
        // get the value of our object based on provided property to use for group the array as the array key
        const key = object[property]
        /*  if the current value is similar to the key don't accumulate the transformed array and leave it empty */
        if (!accumulator[key]) {
            accumulator[key] = []
        }
        // add the value to the array
        accumulator[key].push(object)
        // return the transformed array
        return accumulator
        // Also we also set the initial value of reduce() to an empty object
    }, {})
}

/**
 * Handle format date value
 * @param {String} value String date to be formatted
 * @param {String} formatType format type
 * @return {String} new String with HH:mm:ss MM/DD/YYYY format
 */
export function formatValue(value, formatType) {
    let date = new Date(value)
    const DATE_RFC2822 = 'ddd, DD MMM YYYY HH:mm:ss'
    const default_format = 'HH:mm:ss MM.DD.YYYY'
    let format
    switch (formatType) {
        case 'DATE_RFC2822':
            format = DATE_RFC2822
            break
        case 'MM.DD.YYYY HH:mm:ss':
            format = formatType
            break
        default:
            format = default_format
    }

    return Vue.moment(date).format(format)
}

let nodeId = 0 // must be a number, so that hierarchySort can be done
/**
 * Flatten a tree into nodes list
 * Convert an object to array of nodes object with tree data properties.
 * If key value is an object, it will be flatten. If key value is an array,
 * it will be converted to object then flatten.
 * @param {Object} payload - Payload object
 * @param {Object} payload.obj - Root Object to be handle
 * @param {Boolean} payload.keepPrimitiveValue - keepPrimitiveValue to whether call handleValue function or not
 * @param {Number} payload.level - depth level for nested object
 * @param {Object} payload.parentNodeInfo - This contains id and original value, it's null in the first level (0)
 * @param {Number} payload.parentNodeId - nodeId of parentNode
 * @param {String} payload.keyName - keyName
 * @param {String} payload.keyValue - keyValue
 * @return {Array} an array of nodes object
 */
export function objToArrOfNodes({
    obj,
    keepPrimitiveValue,
    level,
    parentNodeInfo = null,
    parentNodeId = 0,
    keyName = 'id',
    keyValue = 'value',
}) {
    if (isNotEmptyObj(obj)) {
        let nodes = []
        const targetObj = cloneDeep(obj)

        Object.keys(targetObj).map(key => {
            let value = keepPrimitiveValue ? targetObj[key] : handleValue(targetObj[key])

            let node = {
                nodeId: ++nodeId,
                parentNodeId: parentNodeId,
                level: level,
                parentNodeInfo: parentNodeInfo,
                [keyName]: key,
                [keyValue]: value,
                originalValue: value,
            }

            const hasChild = isNotEmptyArray(value) || isNotEmptyObj(value)

            if (hasChild) {
                node[keyValue] = ''
                //  only object has child value will have expanded property
                node.expanded = false
            }

            if (isNotEmptyObj(value))
                node.children = objToArrOfNodes({
                    obj: value,
                    keepPrimitiveValue,
                    level: level + 1,
                    parentNodeInfo: { [keyName]: key, originalValue: value },
                    parentNodeId: node.nodeId,
                })
            if (isNotEmptyArray(value))
                //convert value type array to object then do a recursive call
                node.children = objToArrOfNodes({
                    obj: { ...value },
                    keepPrimitiveValue,
                    level: level + 1,
                    parentNodeInfo: { [keyName]: key, originalValue: value },
                    parentNodeId: node.nodeId,
                })

            node.leaf = !hasChild
            nodes.push(node)
        })
        return nodes
    }
    return []
}

/**
 * Convert an array of nodes object to an object has property name as the value of keyName,
 * key value as the value of keyValue.
 * @param {Object} payload - Payload object
 * @param {Array} payload.arr - Array of objects
 * @param {String} payload.keyName - keyName of the object in the array
 * @param {String} payload.keyValue - keyValue of the object in the array
 * @return {Object} return an object
 */
export function arrToObject({ arr, keyName = 'id', keyValue = 'value' }) {
    if (isNotEmptyArray(arr)) {
        let targetArr = cloneDeep(arr)
        let resultObj = {}
        /*
            if value of keyValue is an object,
            there is linked nodes, this linkedNodesHash
            holds key value and linkedNodeKeyName
            of linked nodes
        */
        let linkedNodeKeyName = null
        let linkedNodesHash = {}
        targetArr.forEach(node => {
            const { parentNodeInfo = null } = node

            /*
                objToArrOfNodes reverse, get parentNodeInfo then check if
                current node is a linked node. Then assign key value of
                current node to linkedNodesHash object, finally assign
                to resultObj with parentId as key name and linkedNodesHash as
                key value
            */
            if (parentNodeInfo) {
                const { originalValue, id: parentId } = parentNodeInfo
                if (linkedNodesHash[parentId] == undefined) linkedNodesHash[parentId] = {}

                if (isLinkedNode({ parentNodeInfo, linkedNodesHash, linkedNodeKeyName })) {
                    linkedNodesHash[parentId] = originalValue
                    linkedNodeKeyName = parentId
                }

                linkedNodesHash[parentId] = {
                    ...linkedNodesHash[parentId], //includes unmodified key/value pair as well
                    [node[keyName]]: node[keyValue], //set new value to key
                }

                resultObj[parentId] = linkedNodesHash[parentId]
            } else if (node.leaf || node.leaf === undefined)
                /*
                    leaf is undefined when the array wasn't created by objToArrOfNodes.
                    e.g. in parameters-collapse component.
                */
                resultObj[node[keyName]] = node[keyValue]
        })

        return resultObj
    }
    return {}
}
/**
 * This function compares original key names of parent's original value with
 * key names of linkedNodes object. LinkedNodes object is getting from linkedNodesHash
 * with key name as parentId
 * @private
 * @param {Object} payload - Payload object
 * @param {Object} payload.parentNodeInfo - node info contains id of the parent and original value before flattening
 * @param {Object} payload.parentNodeInfo.originalValue - original value before flattening
 * @param {String} payload.parentNodeInfo.parentId - parent id of this node
 * @param {Object} payload.linkedNodesHash - linked nodes hash accumulated
 * @param {String} payload.linkedNodeKeyName - linked node key name
 * @returns {Boolean} return true if current node is a linked node
 */
export function isLinkedNode({ parentNodeInfo, linkedNodesHash, linkedNodeKeyName }) {
    const { originalValue, id: parentId } = parentNodeInfo
    const linkedNodes = linkedNodesHash[parentId]
    if (isEmpty(linkedNodes)) return true
    else {
        const linkedNodesKeys = Object.keys(linkedNodes).sort()
        const parentObjKeys = Object.keys(originalValue).sort()
        return !isEqual(linkedNodesKeys, parentObjKeys) && linkedNodeKeyName === parentId
    }
}

/**
 * Handle displaying undefined and null as 'undefined' and 'null' string respectively
 * @param {Any} value Any types that needs to be handled
 * @return {Any} return valid value for rendering, null becomes 'null', otherwise return 'undefined'
 */
export function handleValue(value) {
    const typeOfValue = typeof value
    let newVal

    if (
        Array.isArray(value) ||
        typeOfValue === 'object' ||
        typeOfValue === 'string' ||
        typeOfValue === 'number' ||
        typeOfValue === 'boolean'
    ) {
        newVal = value
    } else {
        newVal = 'undefined'
    }
    // handle typeof null object and empty string
    if (value === null) newVal = 'null'

    return newVal
}

export function capitalizeFirstLetter(string) {
    return string.charAt(0).toUpperCase() + string.slice(1)
}

export function isArrayEqual(x, y) {
    return isEmpty(xorWith(x, y, isEqual))
}

/**
 * @param {Object} bytes byte be processed
 * @return {String} returns converted value
 */
export function byteConverter(bytes) {
    let val
    const base = 1024
    const i = Math.floor(Math.log(bytes) / Math.log(base))

    if (i === 0) return { value: bytes, suffix: '' }

    val = bytes / Math.pow(base, i)
    let result = { value: Math.floor(val), suffix: ['', 'Ki', 'Mi', 'Gi', 'Ti'][i] }
    return result
}

export function toBitsOrBytes(suffix, val, reverse = false) {
    let result = val
    let base
    switch (suffix) {
        case 'Ki':
        case 'k':
            base = Math.pow(1024, 1)
            break
        case 'Mi':
        case 'M':
            base = Math.pow(1024, 2)
            break
        case 'Gi':
        case 'G':
            base = Math.pow(1024, 3)
            break
        case 'Ti':
        case 'T':
            base = Math.pow(1024, 4)
            break
        default:
            base = Math.pow(1024, 0)
    }
    return reverse ? Math.floor(result / base) : result * base
}

/**
 * @param {String} suffix duration suffix: s,m,h,ms
 * @param {Object} val mode be processed. Default is null
 * @param {Boolean} reverse
 * @return {Number} returns converted value
 */
export function toBaseMiliOrReverse(suffix, val, reverse) {
    let result
    switch (suffix) {
        case 's':
            result = reverse ? val / 1000 : val * 1000
            break
        case 'm':
            result = reverse ? val / (60 * 1000) : val * 60 * 1000
            break
        case 'h':
            result = reverse ? val / (60 * 60 * 1000) : val * 60 * 60 * 1000
            break
        case 'ms':
        default:
            result = val
    }
    return Math.floor(result)
}

/**
 * @param {Object} param parameter object must contain string value property
 * @param {Array} suffixes an array of suffixes name eg: ['ms', 's', 'm', 'h']
 * @return {Object} returns object info {suffix:suffix, indexOfSuffix: indexOfSuffix}
 * suffix as suffix name, indexOfSuffix as the begin index of that suffix in param.value
 */
export function getSuffixFromValue(param, suffixes) {
    let suffix = null
    let indexOfSuffix = null
    // get suffix from param.value string
    for (let i = 0; i < suffixes.length; ++i) {
        if (param.value.includes(suffixes[i])) {
            suffix = suffixes[i]
            indexOfSuffix = param.value.indexOf(suffix)
            break
        }
    }
    return { suffix: suffix, indexOfSuffix: indexOfSuffix }
}
/**
 *
 *
 * @export
 * @param {String} label - label for dataset
 * @param {Number} value - value for dataset
 * @param {Number} colorIndex - index of color from color palette of dynamicColors helper
 * @param {Number} [timestamp] - if provided, otherwise using Date.now()
 * @returns {Object} returns dataset object
 */
export function genLineDataSet(label, value, colorIndex, timestamp) {
    const lineColor = dynamicColors(colorIndex)
    const indexOfOpacity = lineColor.lastIndexOf(')') - 1
    const backgroundColor = strReplaceAt(lineColor, indexOfOpacity, '0.1')
    let time = Date.now()
    if (timestamp) time = timestamp
    return {
        label: label,
        id: label,
        type: 'line',
        // background of the line
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        borderWidth: 1,
        lineTension: 0,
        data: [{ x: time, y: value }],
    }
}

Object.defineProperties(Vue.prototype, {
    $help: {
        get() {
            return {
                getCookie,
                deleteCookie,
                range,
                serviceStateIcon,
                serverStateIcon,
                monitorStateIcon,
                listenerStateIcon,
                isNotEmptyObj,
                isNotEmptyArray,
                delay,
                dynamicColors,
                strReplaceAt,
                getErrorsArr,
                groupBy,
                formatValue,
                objToArrOfNodes,
                arrToObject,
                handleValue,
                capitalizeFirstLetter,
                isArrayEqual,
                getSuffixFromValue,
                byteConverter,
                toBaseMiliOrReverse,
                toBitsOrBytes,
                genLineDataSet,
                isNull,
                isFunction,
                isUndefined,
                lodash,
            }
        },
    },
})
