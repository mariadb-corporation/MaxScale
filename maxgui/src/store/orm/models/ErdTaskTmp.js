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
import Extender from '@/store/orm/Extender'
import { TMP_ORM_ENTITY_MAP, TABLE_STRUCTURE_SPEC_MAP } from '@/constants/workspace'
import { uuidv1 } from '@/utils/helpers'

export default class ErdTaskTmp extends Extender {
  static entity = TMP_ORM_ENTITY_MAP.ERD_TASKS_TMP

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return {
      graph_height_pct: this.number(100),
      active_entity_id: this.string(''),
      active_spec: this.string(TABLE_STRUCTURE_SPEC_MAP.COLUMNS),
      key: this.string(uuidv1()), // key for rerender purpose
      nodes_history: this.attr([]),
      active_history_idx: this.number(0),
    }
  }

  static fields() {
    return {
      id: this.attr(null), // use Erd task Id as PK for this table
      ...this.getNonKeyFields(),
    }
  }
}
