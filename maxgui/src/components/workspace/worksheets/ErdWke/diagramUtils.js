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
import { globalI18n as i18n } from '@/plugins/i18n'
import erdHelper from '@/utils/erdHelper'
import { MIN_MAX_CARDINALITY } from '@wkeComps/ErdWke/config'
import { LINK_OPT_TYPE_MAP, CREATE_TBL_TOKEN_MAP } from '@/constants/workspace'
import { immutableUpdate, lodash } from '@/utils/helpers'

const {
  SET_ONE_TO_ONE,
  SET_ONE_TO_MANY,
  SET_MANDATORY,
  SET_FK_COL_OPTIONAL,
  SET_REF_COL_MANDATORY,
  SET_REF_COL_OPTIONAL,
} = LINK_OPT_TYPE_MAP
const { ONLY_ONE, ZERO_OR_ONE } = MIN_MAX_CARDINALITY

/**
 * @public
 * @param {object} param.link - erd link
 * @param {function} param.actionCb - callback action
 * @returns {object}
 */
function genCardinalityOpt({ link, actionCb }) {
  const [src = ''] = link.relationshipData.type.split(':')
  const optType = src === ONLY_ONE || src === ZERO_OR_ONE ? SET_ONE_TO_MANY : SET_ONE_TO_ONE
  return {
    title: i18n.t(optType),
    type: optType,
    action: () => actionCb(optType),
  }
}

/**
 * @public
 * @param {object} param.link - erd link
 * @param {function} param.actionCb - callback action
 * @param {boolean} param.isForRefTbl - erd link
 * @returns {object}
 */
function genOptionalityOpt({ link, actionCb, isForRefTbl = false }) {
  const {
    source,
    target,
    relationshipData: { src_attr_id, target_attr_id },
  } = link
  let node = source,
    colId = src_attr_id,
    optType = isForRefTbl ? SET_REF_COL_MANDATORY : SET_MANDATORY

  if (isForRefTbl) {
    node = target
    colId = target_attr_id
  }
  if (erdHelper.isColMandatory({ node, colId }))
    optType = isForRefTbl ? SET_REF_COL_OPTIONAL : SET_FK_COL_OPTIONAL

  return {
    title: i18n.t(optType),
    type: optType,
    action: () => actionCb(optType),
  }
}

/**
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

export default { genCardinalityOpt, genOptionalityOpt, toggleUnique, toggleNotNull }
