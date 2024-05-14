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
import Extender from '@/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES } from '@/constants/workspace'

export default class SchemaSidebar extends Extender {
  static entity = ORM_PERSISTENT_ENTITIES.SCHEMA_SIDEBARS

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return {
      filter_txt: this.string(''),
      expanded_nodes: this.attr([]),
    }
  }

  static fields() {
    return {
      id: this.attr(null), // use Worksheet Id as PK for this table
      ...this.getNonKeyFields(),
    }
  }
}
