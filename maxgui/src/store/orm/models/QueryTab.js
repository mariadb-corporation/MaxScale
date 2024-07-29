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
import {
  PERSISTENT_ORM_ENTITY_MAP,
  TMP_ORM_ENTITY_MAP,
  QUERY_TAB_TYPE_MAP,
} from '@/constants/workspace'
import { uuidv1 } from '@/utils/helpers'

export default class QueryTab extends Extender {
  static entity = PERSISTENT_ORM_ENTITY_MAP.QUERY_TABS

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return {
      name: this.string('Query Tab 1'),
      count: this.number(1),
      type: this.string(QUERY_TAB_TYPE_MAP.SQL_EDITOR),
    }
  }

  /**
   * This function refreshes the name field to its default name
   * @param {String|Function} payload - either an id or a callback function that return Boolean (filter)
   */
  static refreshName(payload) {
    const models = this.filterEntity(this, payload)
    models.forEach((model) => {
      const target = this.query().withAll().whereId(model.id).first()
      if (target) this.update({ where: model.id, data: { name: `Query Tab ${target.count}` } })
    })
  }

  static fields() {
    return {
      id: this.uid(() => uuidv1()),
      ...this.getNonKeyFields(),
      //FK
      query_editor_id: this.attr(null),
      // relationship fields
      alterEditor: this.hasOne(PERSISTENT_ORM_ENTITY_MAP.ALTER_EDITORS, 'id'),
      insightViewer: this.hasOne(PERSISTENT_ORM_ENTITY_MAP.INSIGHT_VIEWERS, 'id'),
      txtEditor: this.hasOne(PERSISTENT_ORM_ENTITY_MAP.TXT_EDITORS, 'id'),
      queryResult: this.hasOne(PERSISTENT_ORM_ENTITY_MAP.QUERY_RESULTS, 'id'),
      queryTabTmp: this.hasOne(TMP_ORM_ENTITY_MAP.QUERY_TABS_TMP, 'id'),
    }
  }
}
