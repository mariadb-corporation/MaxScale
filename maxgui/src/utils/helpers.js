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
import { MXS_OBJ_TYPES } from '@share/constants'
import { TIME_REF_POINTS } from '@rootSrc/constants'
import {
    getUnixTime,
    subMonths,
    subDays,
    subWeeks,
    startOfDay,
    endOfYesterday,
    parseISO,
} from 'date-fns'

export function isNotEmptyObj(v) {
    return v !== null && !Array.isArray(v) && typeof v === 'object' && !lodash.isEmpty(v)
}

export function isNotEmptyArray(v) {
    return v !== null && Array.isArray(v) && v.length > 0
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

export function repStateIcon(state) {
    if (state) {
        // error icon
        if (state === 'Stopped') return 0
        // healthy icon
        else if (state === 'Running') return 1
        // warning icon
        else return 2
    }
}

export function monitorStateIcon(monitorState) {
    if (monitorState) {
        if (monitorState.includes('Running')) return 1
        if (monitorState.includes('Stopped')) return 0
        else return ''
    } else return ''
}

export function listenerStateIcon(state) {
    if (state) {
        if (state === 'Running') return 1
        else if (state === 'Stopped') return 2
        else if (state === 'Failed') return 0
        else return ''
    } else return ''
}

/**
 * This export function takes array of objects and object path to create a hash map
 * using provided argument path. Key value will be always an array of objects.
 * Meaning that if there are duplicated key values at provided argument path, the value will be
 * pushed to the array. This makes this export function different compare to built in Map object
 * @param {Array} payload.arr - Array of objects to be hashed by provided keyName
 * @param {String} payload.path - path of object to be hashed by. e.g. 'attributes.module_type' or 'parentNodeId'
 * @return {Object} hashMap
 */
export function hashMapByPath({ arr, path }) {
    let hashMap = {}
    arr.forEach(obj => {
        const keyValue = lodash.get(obj, path)
        if (hashMap[keyValue] === undefined) hashMap[keyValue] = []
        hashMap[keyValue].push(obj)
    })
    return hashMap
}

/**
 * This export function converts type null and undefined to type string
 * with value as 'undefined' and 'null' respectively
 * @param {Any} value - Any types that needs to be handled
 * @return {Any} value
 */
export function convertType(value) {
    const typeOfValue = typeof value
    let newVal = value
    if (typeOfValue === 'undefined') {
        newVal = typeOfValue
    }
    // handle typeof null object and empty string
    if (value === null) newVal = 'null'
    return newVal
}

/**
 * Convert an object to tree array.
 * Root id (parentNodeId) is always started at 0
 * @param {Object} payload.obj - Root object to be handled
 * @param {Boolean} payload.keepPrimitiveValue - keepPrimitiveValue to whether call convertType export function or not
 * @param {Number} payload.level - depth level for nested object
 * @param {Number} payload.parentNodeId - nodeId of parentNode
 * @return {Array} an array of nodes object
 */
export function objToTree(params) {
    let nodeId = 0 // must be a number, so that hierarchySort can be done
    function recursive(params) {
        const { obj, keepPrimitiveValue, level, parentNodeId = 0 } = params
        let tree = []
        if (isNotEmptyObj(obj)) {
            const targetObj = lodash.cloneDeep(obj)
            Object.keys(targetObj).forEach(key => {
                const value = keepPrimitiveValue ? targetObj[key] : convertType(targetObj[key])

                let node = {
                    nodeId: ++nodeId,
                    parentNodeId,
                    level,
                    id: key,
                    value: value,
                    originalValue: value,
                }

                const hasChild = isNotEmptyArray(value) || isNotEmptyObj(value)
                node.leaf = !hasChild
                if (hasChild) {
                    node.value = ''
                    //  only object has child value will have expanded property
                    node.expanded = false
                }

                if (isNotEmptyObj(value))
                    node.children = recursive({
                        obj: value,
                        keepPrimitiveValue,
                        level: level + 1,
                        parentNodeId: node.nodeId,
                    })
                if (isNotEmptyArray(value))
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
 * This function flattens tree array
 * @param {Array} tree - tree array to be flatten
 * @returns {Array} flattened array
 */
export function flattenExpandableTree(tree) {
    let flattened = []
    let target = lodash.cloneDeep(tree)
    //Traversal
    target.forEach(o => {
        if (o.children && o.children.length > 0) {
            o.expanded = true
            flattened.push(o)
            flattened = [...flattened, ...flattenExpandableTree(o.children)]
        } else flattened.push(o)
    })
    return flattened
}

/**
 * This function finds the ancestor node id of provided argument node
 * @param {Number} payload.node - node to be used for finding its ancestor
 * @param {Map} payload.treeMap - map for find specific node using nodeId
 * @returns {Number} ancestor node id
 */
export function findAncestor({ node, treeMap }) {
    const { nodeId } = node
    let ancestors = []
    let parentId = treeMap.get(nodeId) && treeMap.get(nodeId).parentNodeId
    while (parentId) {
        ancestors.push(parentId)
        parentId = treeMap.get(parentId) && treeMap.get(parentId).parentNodeId
    }
    // since nodeId is an incremental number, the ancestor nodeId should be the smallest number
    if (ancestors.length) return Math.min(...ancestors)
    // root parentNodeId is always 0
    else return 0
}

/**
 * This function mutates nested property of obj (ancestor object)
 * using id and value of node obj. The id of node obj
 * is the key of ancestor object at unknown level while the value is
 * the new value for that key.
 * @param {Object} payload.obj - ancestor object
 * @param {Object} payload.node - node that contains id and value.
 */
export function updateNode({ obj, node }) {
    const { id: key, value } = node
    if (obj[key] !== undefined) obj[key] = value
    else
        for (const prop in obj) {
            if (obj[prop] && typeof obj[prop] === 'object') updateNode({ obj: obj[prop], node })
        }
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
    if (isNotEmptyArray(changedNodes)) {
        let ancestorsHash = {}
        const target = lodash.cloneDeep(changedNodes)
        let treeMap = new Map()
        const flattened = flattenExpandableTree(tree)
        flattened.forEach(node => treeMap.set(node.nodeId, node))

        target.forEach(node => {
            const { parentNodeId } = node
            // if a node changes its value, its ancestor needs to be included in the resultObj
            if (parentNodeId) {
                const ancestorId = findAncestor({ node, treeMap })
                const ancestorNode = treeMap.get(ancestorId)
                if (ancestorNode) {
                    const { originalValue, id: ancestorId } = ancestorNode
                    ancestorsHash[ancestorId] = originalValue
                    updateNode({ obj: ancestorsHash[ancestorId], node: node })
                    resultObj[ancestorId] = ancestorsHash[ancestorId]
                }
            } else if (node.leaf) resultObj[node.id] = node.value
        })
    }
    return resultObj
}

/**
 * This export function converts to bits or bytes from provided
 * suffix argument when reverse argument is false, otherwise
 * it reverses the conversion from either bits or bytes to provided suffix argument
 * @param {String} payload.suffix - size suffix: Ki, Mi, Gi, Ti or k, M, G, T
 * @param {Number} payload.val - value to be converted
 * @param {Boolean} payload.isIEC - if it is true, it use 1024 for multiples of bytes (B), otherwise 1000 of bits
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

/**
 * This export function creates dataset object for mxs-line-chart-stream
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
 * This allows to enter minus or hyphen minus and numbers
 * @param {Event} e - input evt
 */
export function preventNonInteger(e) {
    if (!e.key.match(/^[-]?\d*$/g)) e.preventDefault()
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
 * Get slave replication status
 * @param {Object} serverInfo
 * @returns {Array}- replication status
 */
export function getRepStats(serverInfo) {
    if (!serverInfo || !serverInfo.slave_connections.length) return []
    const repStats = []
    serverInfo.slave_connections.forEach(slave_conn => {
        const { seconds_behind_master, slave_io_running, slave_sql_running } = slave_conn
        let replication_state = 'Lagging'
        // Determine replication_state (Stopped||Running||Lagging)
        if (slave_io_running === 'No' || slave_sql_running === 'No') replication_state = 'Stopped'
        else if (seconds_behind_master === 0) {
            if (slave_sql_running === 'Yes' && slave_io_running === 'Yes')
                replication_state = 'Running'
            else
                replication_state =
                    slave_io_running !== 'Yes' ? slave_io_running : slave_sql_running
        }
        repStats.push({ name: serverInfo.name, replication_state, ...slave_conn })
    })

    return repStats
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

export function scrollToFirstErrMsgInput() {
    let invalidEles = document.getElementsByClassName('v-messages__message')
    return invalidEles[0].scrollIntoView({
        behavior: 'smooth',
        block: 'center',
        inline: 'start',
    })
}

export function isServerOrListenerType(type) {
    return type === MXS_OBJ_TYPES.SERVERS || type === MXS_OBJ_TYPES.LISTENERS
}

/**
 * @param {array} arr - stringlist value. e.g. ['character_set_client=auto', 'character_set_connection=auto']
 * @returns {string} string with comma separator and line break
 */
export function stringListToStr(arr) {
    return arr.join(',\n').trim()
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
