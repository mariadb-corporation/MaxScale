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
import { PERSISTENT_ORM_ENTITY_MAP, TMP_ORM_ENTITY_MAP } from '@/constants/workspace'

export default class QueryEditor extends Extender {
  static entity = PERSISTENT_ORM_ENTITY_MAP.QUERY_EDITORS

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return { active_query_tab_id: this.attr(null) }
  }

  static fields() {
    return {
      id: this.attr(null), // use Worksheet id as PK for this table
      ...this.getNonKeyFields(),
      queryTabs: this.hasMany(PERSISTENT_ORM_ENTITY_MAP.QUERY_TABS, 'query_editor_id'),
      schemaSidebar: this.hasOne(PERSISTENT_ORM_ENTITY_MAP.SCHEMA_SIDEBARS, 'id'),
      queryEditorTmp: this.hasOne(TMP_ORM_ENTITY_MAP.QUERY_EDITORS_TMP, 'id'),
    }
  }
}
