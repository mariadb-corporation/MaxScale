/*
 * Copyright (c) 2023 MariaDB plc
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
import { MXS_OBJ_TYPES } from '@share/constants'
import { lodash, stringifyNullOrUndefined } from '@share/utils/helpers'
import * as utils from '@src/utils/dataTableHelpers/utils'

/**
 * Convert an object to tree array.
 * Root id (parentNodeId) is always started at 0
 * @param {Object} payload.obj - Root object to be handled
 * @param {Boolean} payload.keepPrimitiveValue - conditionally call stringifyNullOrUndefined
 * @param {Number} payload.level - depth level for nested object
 * @param {Number} payload.parentNodeId - nodeId of parentNode
 * @return {Array} an array of nodes object
 */
export function objToTree(params) {
    let nodeId = 0 // must be a number, so that hierarchySort can be done
    function recursive(params) {
        const { obj, keepPrimitiveValue, level, parentNodeId = 0 } = params
        let tree = []
        if (utils.isNotEmptyObj(obj)) {
            const targetObj = lodash.cloneDeep(obj)
            Object.keys(targetObj).forEach(key => {
                const value = keepPrimitiveValue
                    ? targetObj[key]
                    : stringifyNullOrUndefined(targetObj[key])

                let node = {
                    nodeId: ++nodeId,
                    parentNodeId,
                    level,
                    id: key,
                    value: value,
                    originalValue: value,
                }

                const hasChild = utils.isNotEmptyArray(value) || utils.isNotEmptyObj(value)
                node.leaf = !hasChild
                if (hasChild) {
                    node.value = ''
                    //  only object has child value will have expanded property
                    node.expanded = false
                }

                if (utils.isNotEmptyObj(value))
                    node.children = recursive({
                        obj: value,
                        keepPrimitiveValue,
                        level: level + 1,
                        parentNodeId: node.nodeId,
                    })
                if (utils.isNotEmptyArray(value))
                    //convert value type array to object then do a recursive call
                    node.children = recursive({
                        obj: { ...value },
                        keepPrimitiveValue,
                        level: level + 1,
                        parentNodeId: node.nodeId,
                    })

                tree.push(node)
            })
        }
        return tree
    }
    return recursive(params)
}

/**
 * This export function takes tree for creating a tree map to
 * lookup for changed nodes and finally return an object
 * with key pairs as changed nodes id and value. This object
 * respects depth level of nested objects.
 * e.g. If a changed nodes are [ { id: 'count', value: 10, ... } ]
 * The result object would be { log_throttling { window: 0, suppress: 0, count: 10 }}
 * @param {Array} payload.arr - Array of objects
 * @param {Array} payload.tree - tree
 * @return {Object} object
 */
export function treeToObj({ changedNodes, tree }) {
    let resultObj = {}
    if (utils.isNotEmptyArray(changedNodes)) {
        let ancestorsHash = {}
        const target = lodash.cloneDeep(changedNodes)
        let treeMap = new Map()
        const flattened = utils.flattenExpandableTree(tree)
        flattened.forEach(node => treeMap.set(node.nodeId, node))

        target.forEach(node => {
            const { parentNodeId } = node
            // if a node changes its value, its ancestor needs to be included in the resultObj
            if (parentNodeId) {
                const ancestorId = utils.findAncestor({ node, treeMap })
                const ancestorNode = treeMap.get(ancestorId)
                if (ancestorNode) {
                    const { originalValue, id: ancestorId } = ancestorNode
                    ancestorsHash[ancestorId] = originalValue
                    utils.updateNode({ obj: ancestorsHash[ancestorId], node: node })
                    resultObj[ancestorId] = ancestorsHash[ancestorId]
                }
            } else if (node.leaf) resultObj[node.id] = node.value
        })
    }
    return resultObj
}

/**
 * @param {Object} param - parameter object must contain string value property
 * @param {Array} suffixes - an array of suffixes name .e.g. ['ms', 's', 'm', 'h']
 * @return {Object} object info {suffix:suffix, indexOfSuffix: indexOfSuffix}
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

export function isServerOrListenerType(type) {
    return type === MXS_OBJ_TYPES.SERVERS || type === MXS_OBJ_TYPES.LISTENERS
}

/**
 * This export function converts to bits or bytes from provided
 * suffix argument when reverse argument is false, otherwise
 * it reverses the conversion from either bits or bytes to provided suffix argument
 * @param {String} payload.suffix - size suffix: Ki, Mi, Gi, Ti or k, M, G, T
 * @param {Number} payload.val - value to be converted
 * @param {Boolean} payload.isIEC - if it is true, it use 1024 for multiples of bytes (B),
 * otherwise 1000 of bits
 * @param {Boolean} payload.reverse - should reverse convert or not
 * @returns {Number} new size value
 */
export function convertSize({ suffix, val, isIEC = false, reverse = false }) {
    let result = val
    let base
    let multiple = isIEC ? 1024 : 1000
    switch (suffix) {
        case 'Ki':
        case 'k':
            base = Math.pow(multiple, 1)
            break
        case 'Mi':
        case 'M':
            base = Math.pow(multiple, 2)
            break
        case 'Gi':
        case 'G':
            base = Math.pow(multiple, 3)
            break
        case 'Ti':
        case 'T':
            base = Math.pow(multiple, 4)
            break
        default:
            base = Math.pow(multiple, 0)
    }
    return reverse ? Math.floor(result / base) : result * base
}

/**
 * This export function converts to milliseconds from provided suffix argument by default.
 * If toMilliseconds is false, it converts milliseconds value to provided suffix argument
 * @param {String} payload.suffix duration suffix: ms,s,m,h
 * @param {Number} payload.val value to be converted. Notice: should be ms value if toMilliseconds is false
 * @param {Boolean} payload.toMilliseconds whether to convert to milliseconds
 * @return {Number} returns converted duration value
 */
export function convertDuration({ suffix, val, toMilliseconds = true }) {
    let result
    switch (suffix) {
        case 's':
            result = toMilliseconds ? val * 1000 : val / 1000
            break
        case 'm':
            result = toMilliseconds ? val * 60 * 1000 : val / (60 * 1000)
            break
        case 'h':
            result = toMilliseconds ? val * 60 * 60 * 1000 : val / (60 * 60 * 1000)
            break
        case 'ms':
        default:
            result = val
    }
    return Math.floor(result)
}

/**
 * @param {array} arr - stringlist value. e.g. ['character_set_client=auto', 'character_set_connection=auto']
 * @returns {string} string with comma separator and line break
 */
export function stringListToStr(arr) {
    return arr.join(',\n').trim()
}
