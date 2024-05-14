/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import store from '@/store'
import { t as typy } from 'typy'
import { queryEngines, queryCharsetCollationMap, queryDefDbCharsetMap } from '@/store/queryHelper'

/**
 * @param {string} param.connId - connection id
 * @param {object} param.config - axios config
 */
async function querySuppData(param) {
  if (param.connId && param.config) {
    if (typy(store.state.ddlEditor.engines).isEmptyArray)
      store.commit('ddlEditor/SET_ENGINES', await queryEngines(param))
    if (typy(store.state.ddlEditor.charset_collation_map).isEmptyObject)
      store.commit('ddlEditor/SET_CHARSET_COLLATION_MAP', await queryCharsetCollationMap(param))
    if (typy(store.state.ddlEditor.def_db_charset_map).isEmptyObject)
      store.commit('ddlEditor/SET_DEF_DB_CHARSET_MAP', await queryDefDbCharsetMap(param))
  }
}

export default { querySuppData }
