/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { NODE_TYPE_MAP } from '@/constants/workspace'
import { formatSQL } from '@/utils/queryUtils'
import { t as typy } from 'typy'

/**
 * @param {object} param
 * @param {NODE_TYPE_MAP} param.type
 * @param {object} param.resultSet - an object contains the result of SHOW CREATE statement
 * @returns {string} ddl string
 */
function getDdl({ type, resultSet }) {
  let value = ''
  switch (type) {
    case NODE_TYPE_MAP.SP:
    case NODE_TYPE_MAP.FN:
    case NODE_TYPE_MAP.TRIGGER:
      value = typy(resultSet, `data[0][2]`).safeString
      break
    case NODE_TYPE_MAP.VIEW:
    default:
      value = typy(resultSet, `data[0][1]`).safeString
  }
  return formatSQL(value)
}

export default { getDdl }
