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
import Extender from '@/store/orm/Extender'
import {
  PERSISTENT_ORM_ENTITY_MAP,
  TMP_ORM_ENTITY_MAP,
  ETL_STATUS_MAP,
  ETL_STAGE_INDEX_MAP,
} from '@/constants/workspace'
import { uuidv1 } from '@/utils/helpers'

export default class EtlTask extends Extender {
  static entity = PERSISTENT_ORM_ENTITY_MAP.ETL_TASKS

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return {
      name: this.string(''),
      status: this.string(ETL_STATUS_MAP.INITIALIZING),
      active_stage_index: this.number(ETL_STAGE_INDEX_MAP.OVERVIEW),
      // help to differentiate stage of migration in etl-migration-stage
      is_prepare_etl: this.boolean(false),
      /**
       * @property {string} src_type  - mariadb||postgresql||generic
       * @property {string} dest_name - server name in MaxScale
       * @property {string} async_query_id - query_id of async query
       */
      meta: this.attr({}),
      res: this.attr({}), // only store migration results; scripts are not stored
      /**
       * @property {number} timestamp
       * @property {string} name
       */
      logs: this.attr({
        [ETL_STAGE_INDEX_MAP.CONN]: [],
        [ETL_STAGE_INDEX_MAP.SRC_OBJ]: [],
        [ETL_STAGE_INDEX_MAP.DATA_MIGR]: [],
      }),
      created: this.number(Date.now()),
    }
  }

  static fields() {
    return {
      id: this.uid(() => uuidv1()),
      ...this.getNonKeyFields(),
      connections: this.hasMany(PERSISTENT_ORM_ENTITY_MAP.QUERY_CONNS, 'etl_task_id'),
      etlTaskTmp: this.hasOne(TMP_ORM_ENTITY_MAP.ETL_TASKS_TMP, 'id'),
    }
  }
}
