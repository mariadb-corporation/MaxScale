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
import { t as typy } from 'typy'

export const isNotEmptyObj = (v) => typy(v).isObject && !typy(v).isEmptyObject
export const isNotEmptyArray = (v) => typy(v).isArray && v.length > 0

/**
 * This function finds the ancestor node id of provided argument node
 * @param {object} payload.nodeId - node to be used for finding its ancestor
 * @param {object} payload.nodeMap - map for find specific node using id
 * @returns {string} ancestor node id
 */
export function findAncestor({ id, nodeMap }) {
  let ancestors = []
  let parentId = typy(nodeMap[id], 'parentId').safeString
  while (parentId) {
    ancestors.push(parentId)
    parentId = typy(nodeMap[parentId], 'parentId').safeString
  }
  if (ancestors.length) return ancestors.at(-1)
  return null
}

/**
 * This function mutates nested property of obj (ancestor object)
 * using key and value of node obj. The key of node obj
 * is the key of ancestor object at unknown level while the value is
 * the new value for that key.
 * @param {Object} payload.obj - ancestor object
 * @param {Object} payload.node - node that contains key and value.
 */
export function updateNode({ obj, node }) {
  const { key, value } = node
  if (obj[key] !== undefined) obj[key] = value
  else
    for (const prop in obj) {
      if (obj[prop] && typeof obj[prop] === 'object') updateNode({ obj: obj[prop], node })
    }
}
