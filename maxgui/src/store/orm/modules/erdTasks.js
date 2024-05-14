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
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import { t as typy } from 'typy'

export default {
  namespaced: true,
  getters: {
    activeRecordId: () => Worksheet.getters('activeId'),
    activeTmpRecord: (_, getters) => ErdTaskTmp.find(getters.activeRecordId) || {},
    nodesHistory: (_, getters) => typy(getters.activeTmpRecord, 'nodes_history').safeArray,
    activeHistoryIdx: (_, getters) =>
      typy(getters.activeTmpRecord, 'active_history_idx').safeNumber,
  },
}
