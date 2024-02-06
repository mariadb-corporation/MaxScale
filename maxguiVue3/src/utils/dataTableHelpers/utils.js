/*
 * Copyright (c) 2023 MariaDB plc
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
import { lodash } from '@/utils/helpers'
import { t as typy } from 'typy'

export const isNotEmptyObj = (v) => typy(v).isObject && !typy(v).isEmptyObject
export const isNotEmptyArray = (v) => typy(v).isArray && v.length > 0

/**
 * This function flattens tree array
 * @param {Array} tree - tree array to be flatten
 * @returns {Array} flattened array
 */
export function flattenExpandableTree(tree) {
  let flattened = []
  let target = lodash.cloneDeep(tree)
  //Traversal
  target.forEach((o) => {
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
