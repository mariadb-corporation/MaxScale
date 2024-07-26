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
import { ORM_PERSISTENT_ENTITIES } from '@/constants/workspace'

export default class DdlEditor extends Extender {
  static entity = ORM_PERSISTENT_ENTITIES.DDL_EDITORS

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return {
      data: this.attr({}),
      is_fetching: this.boolean(true),
      is_altering: this.boolean(false),
    }
  }

  static fields() {
    return {
      id: this.attr(null),
      ...this.getNonKeyFields(),
    }
  }
}
