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
import { lodash } from '@/utils/helpers'
import * as utils from '@/utils/treeTableHelpers/utils'
import { t as typy } from 'typy'

/**
 * Convert an object to tree array.
 * @param {Object} param.obj - Root object to be handled
 * @param {Number} param.level - depth level for nested object
 * @param {Number} param.parentId - id of parentId
 * @param {boolean} param.arrayTransform
 * @return {Array} an array of nodes object
 */
export function objToTree(params) {
  let id = 0 // must be a number, so that hierarchySort can be done
  function recursive(params) {
    const { obj, keepPrimitiveValue, level, parentId = 0, arrayTransform } = params
    let tree = []
    if (utils.isNotEmptyObj(obj)) {
      const targetObj = lodash.cloneDeep(obj)
      Object.keys(targetObj).forEach((key) => {
        const value = targetObj[key]

        let node = {
          id: ++id,
          parentId,
          level,
          key,
          value,
        }

        const hasChild = arrayTransform
          ? utils.isNotEmptyArray(value) || utils.isNotEmptyObj(value)
          : utils.isNotEmptyObj(value)

        node.leaf = !hasChild
        if (hasChild) {
          //  only object has child value will have expanded property
          node.expanded = false
        }

        if (utils.isNotEmptyObj(value))
          node.children = recursive({
            obj: value,
            keepPrimitiveValue,
            level: level + 1,
            parentId: node.id,
            arrayTransform,
          })
        if (arrayTransform && utils.isNotEmptyArray(value))
          //convert value type array to object then do a recursive call
          node.children = recursive({
            obj: { ...value },
            keepPrimitiveValue,
            level: level + 1,
            parentId: node.id,
            arrayTransform,
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
export function treeToObj({ changedNodes, nodeMap }) {
  let resultObj = {}
  if (typy(changedNodes).safeArray.length) {
    let ancestorsHash = {}
    const target = lodash.cloneDeep(changedNodes)

    target.forEach((node) => {
      const { parentId } = node
      // if a node changes its value, its ancestor needs to be included in the resultObj
      if (parentId) {
        const ancestorId = utils.findAncestor({ id: node.id, nodeMap })
        const ancestorNode = nodeMap[ancestorId]
        if (ancestorNode) {
          const { value, key: ancestorKey } = ancestorNode
          ancestorsHash[ancestorKey] = value
          utils.updateNode({ obj: ancestorsHash[ancestorKey], node })
          resultObj[ancestorKey] = ancestorsHash[ancestorKey]
        }
      } else if (node.leaf) resultObj[node.key] = node.value
    })
  }
  return resultObj
}
