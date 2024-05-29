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
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import Worksheet from '@wsModels/Worksheet'

export default {
  namespaced: true,
  getters: {
    activeId: () => Worksheet.getters('activeId'),
    activeRecord: (state, getters) => QueryEditor.find(getters.activeId) || {},
    activeQueryTabId: (state, getters) => getters.activeRecord.active_query_tab_id,
    activeTmpRecord: (state, getters) => QueryEditorTmp.find(getters.activeId) || {},
  },
}
