/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { t as typy } from 'typy'
import erdHelper from '@/utils/erdHelper'
import { CREATE_TBL_TOKEN_MAP, LINK_OPT_TYPE_MAP } from '@/constants/workspace'
import { immutableUpdate, lodash, dynamicColors } from '@/utils/helpers'
import TableParser from '@/utils/TableParser'
import ddlTemplate from '@/utils/ddlTemplate'

const {
  SET_ONE_TO_ONE,
  SET_ONE_TO_MANY,
  SET_MANDATORY,
  SET_FK_COL_OPTIONAL,
  SET_REF_COL_MANDATORY,
  SET_REF_COL_OPTIONAL,
} = LINK_OPT_TYPE_MAP

/**
 * @private
 * @param {object} param
 * @param {object} param.node - entity-diagram node
 * @param {string} param.colId - column id
 * @param {boolean} param.value - if it's true, add UQ key if not exists, otherwise remove UQ
 * @return {object} updated node
 */
function toggleUnique({ node, colId, value }) {
  const category = CREATE_TBL_TOKEN_MAP.uniqueKey
  // check if column is already unique
  const isUnique = erdHelper.areUniqueCols({ node, colIds: [colId] })
  if (value && isUnique) return node
  let keyMap = node.data.defs.key_category_map[category] || {}
  // add UQ key
  if (value) {
    const newKey = erdHelper.genKey({ defs: node.data.defs, category, colId })
    keyMap = immutableUpdate(keyMap, { $merge: { [newKey.id]: newKey } })
  }
  // remove UQ key
  else
    keyMap = immutableUpdate(keyMap, {
      $unset: Object.values(keyMap).reduce((ids, k) => {
        if (
          lodash.isEqual(
            k.cols.map((c) => c.id),
            [colId]
          )
        )
          ids.push(k.id)
        return ids
      }, []),
    })

  return immutableUpdate(node, {
    data: {
      defs: {
        key_category_map: Object.keys(keyMap).length
          ? { $merge: { [category]: keyMap } }
          : { $unset: [category] },
      },
    },
  })
}

/**
 * @private
 * @param {object} param
 * @param {object} param.node - entity-diagram node
 * @param {string} param.colId - column id
 * @param {boolean} param.value - if it's true, turns on NOT NULL.
 * @return {object} updated node
 */
function toggleNotNull({ node, colId, value }) {
  return immutableUpdate(node, {
    data: { defs: { col_map: { [colId]: { nn: { $set: value } } } } },
  })
}

/**
 * @private
 * Adds a PLAIN index for provided colId to provided node
 * @param {object} param
 * @param {string} param.colId
 * @param {object} param.node
 * @returns {object} updated node
 */
function addPlainIndex({ colId, node }) {
  const refTblDef = node.data.defs
  const plainKeyMap = typy(
    refTblDef,
    `key_category_map[${CREATE_TBL_TOKEN_MAP.key}]`
  ).safeObjectOrEmpty
  const newKey = erdHelper.genKey({ defs: refTblDef, category: CREATE_TBL_TOKEN_MAP.key, colId })
  return immutableUpdate(node, {
    data: {
      defs: {
        key_category_map: {
          $merge: { [CREATE_TBL_TOKEN_MAP.key]: { ...plainKeyMap, [newKey.id]: newKey } },
        },
      },
    },
  })
}

/**
 * Assign new coord attributes from nodeMap to provided nodes
 * @param {object} param
 * @param {array<object>} param.nodes
 * @param {object} param.nodeMap
 * @returns {array} updated nodes
 */
function assignCoord({ nodes, nodeMap }) {
  return nodes.reduce((map, n) => {
    if (!nodeMap[n.id]) map[n.id] = n
    else {
      const { x, y, vx, vy, size } = nodeMap[n.id]
      map[n.id] = {
        ...n,
        x,
        y,
        vx,
        vy,
        size,
      }
    }
    return map
  }, {})
}

function immutableUpdateConfig(obj, path, value) {
  const updatedObj = lodash.cloneDeep(obj)
  lodash.update(updatedObj, path, () => value)
  return updatedObj
}

/**
 * Remove the table node and the FK of other table nodes if they
 * are referencing to the table node being removed.
 * @param {object} param
 * @param {object} param.id - table node id to be removed
 * @param {array<object>} param.nodes - all table nodes
 * @returns {object} node map object
 */
function rmTblNode({ id, nodes }) {
  return nodes.reduce((map, n) => {
    if (n.id !== id) {
      const fkMap = n.data.defs.key_category_map[CREATE_TBL_TOKEN_MAP.foreignKey]
      if (!fkMap) map[n.id] = n
      else {
        const updatedFkMap = Object.values(fkMap).reduce((res, key) => {
          if (key.ref_tbl_id !== id) res[key.id] = key
          return res
        }, {})
        map[n.id] = immutableUpdate(n, {
          data: {
            defs: {
              key_category_map: Object.keys(updatedFkMap).length
                ? { $merge: { [CREATE_TBL_TOKEN_MAP.foreignKey]: updatedFkMap } }
                : { $unset: [CREATE_TBL_TOKEN_MAP.foreignKey] },
            },
          },
        })
      }
    }
    return map
  }, {})
}

/**
 * Generate a new parsed table node
 * @param {object} param
 * @param {array<object>} param.nodes
 * @param {array<string>} param.schemas - The first schema is used otherwise 'test' is used as default schema
 * @param {object} param.charsetCollationMap
 * @param {object} param.panAndZoom
 * @param {number} param.panAndZoom.x
 * @param {number} param.panAndZoom.y
 * @param {number} param.panAndZoom.k
 * @returns {object} Updated node map with the new table node added.
 */
function genTblNode({ nodes, schemas, charsetCollationMap, panAndZoom }) {
  const length = nodes.length
  const { genTblStructureData, genErdNode } = erdHelper
  const schema = typy(schemas, '[0]').safeString || 'test'
  const tableParser = new TableParser()
  const nodeData = genTblStructureData({
    parsedTable: tableParser.parse({
      ddl: ddlTemplate.createTbl(`table_${length + 1}`),
      schema,
      autoGenId: true,
    }),
    charsetCollationMap,
  })
  const { x, y, k } = panAndZoom
  return {
    ...genErdNode({ nodeData, highlightColor: dynamicColors(length) }),
    // plus extra padding
    x: (0 - x) / k + 65,
    y: (0 - y) / k + 42,
  }
}

/**
 * Add a new FK to nodeMap
 * @param {object} param
 * @param {object} param.nodeMap
 * @param {object} param.node - the target node to have the FK
 * @param {object} param.colKeyCategoryMap - col key category map of all nodes
 * @param {object} param.currentFkMap - current FK map of the target node
 * @param {object} param.newKey - new fk
 * @param {object} param.refNode - referenced node
 * @returns {object} updated nodeMap
 */
function addFk({ nodeMap, node, colKeyCategoryMap, currentFkMap, newKey, refNode }) {
  let newMap = nodeMap
  // entity-diagram doesn't generate composite FK,so both cols and ref_cols always have one item
  const colId = newKey.cols[0].id
  const refColId = newKey.ref_cols[0].id

  if (!erdHelper.validateFkColTypes({ src: node, target: refNode, colId, targetColId: refColId }))
    return null

  // Auto adds a PLAIN index for referenced col if there is none.
  const nonIndexedColId = colKeyCategoryMap[refColId] ? null : refColId

  if (nonIndexedColId)
    newMap = immutableUpdate(nodeMap, {
      [refNode.id]: { $set: addPlainIndex({ node: nodeMap[refNode.id], colId: nonIndexedColId }) },
    })

  return immutableUpdate(newMap, {
    [node.id]: {
      data: {
        defs: {
          key_category_map: {
            $merge: { [CREATE_TBL_TOKEN_MAP.foreignKey]: { ...currentFkMap, [newKey.id]: newKey } },
          },
        },
      },
    },
  })
}

/**
 * Remove the FK from the nodeMap
 * @param {object} param
 * @param {object} param.nodeMap
 * @param {object} param.link - fk link object to be removed
 * @returns {object} updated nodeMap
 */
function rmFk({ nodeMap, link }) {
  let fkMap = typy(
    nodeMap[link.source.id],
    `data.defs.key_category_map[${CREATE_TBL_TOKEN_MAP.foreignKey}]`
  ).safeObjectOrEmpty

  fkMap = immutableUpdate(fkMap, { $unset: [link.id] })

  return immutableUpdate(nodeMap, {
    [link.source.id]: {
      data: {
        defs: {
          key_category_map: Object.keys(fkMap).length
            ? { $merge: { [CREATE_TBL_TOKEN_MAP.foreignKey]: fkMap } }
            : { $unset: [CREATE_TBL_TOKEN_MAP.foreignKey] },
        },
      },
    },
  })
}

function updateCardinality({ nodeMap, type, link }) {
  const { src_attr_id, target_attr_id } = link.relationshipData
  let newMap = nodeMap,
    nodeId = link.source.id,
    node = newMap[nodeId],
    colId = src_attr_id,
    value = false,
    method

  switch (type) {
    case SET_ONE_TO_MANY:
    case SET_ONE_TO_ONE: {
      method = toggleUnique
      /**
       * In an one to many relationship, FK is placed on the "many" side,
       * and the FK col can't be unique. On the other hand, one to one
       * relationship, fk col and ref col must be both unique
       */
      if (type === SET_ONE_TO_ONE) {
        value = true
        // update also ref col of target node
        newMap = immutableUpdate(newMap, {
          [link.target.id]: {
            $set: method({ node: newMap[link.target.id], colId: target_attr_id, value }),
          },
        })
      }
      break
    }
    case SET_MANDATORY:
    case SET_FK_COL_OPTIONAL:
    case SET_REF_COL_OPTIONAL:
    case SET_REF_COL_MANDATORY: {
      method = toggleNotNull
      if (type === SET_REF_COL_OPTIONAL || type === SET_REF_COL_MANDATORY) {
        nodeId = link.target.id
        colId = target_attr_id
      }
      value = type === SET_MANDATORY || type === SET_REF_COL_MANDATORY
      break
    }
  }
  return immutableUpdate(newMap, { [nodeId]: { $set: method({ node, colId, value }) } })
}

export default {
  assignCoord,
  immutableUpdateConfig,
  genTblNode,
  rmTblNode,
  addFk,
  rmFk,
  updateCardinality,
}
