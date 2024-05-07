/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@/store/orm/Extender'
import { uuidv1 } from '@/utils/helpers'
import { ORM_PERSISTENT_ENTITIES } from '@/constants/workspace'

export default class Worksheet extends Extender {
  static entity = ORM_PERSISTENT_ENTITIES.WORKSHEETS

  /**
   * @returns {Object} - return fields that are not key, relational fields
   */
  static getNonKeyFields() {
    return { name: this.string('WORKSHEET') }
  }

  static fields() {
    return {
      id: this.uid(() => uuidv1()),
      ...this.getNonKeyFields(),
      /**
       * id field value is used as value for either erd_task_id or query_editor_id
       * as there is no need to keep ErdTask or QueryEditor worksheet after deleting it
       */
      erd_task_id: this.attr(null).nullable(),
      etl_task_id: this.attr(null).nullable(),
      query_editor_id: this.attr(null).nullable(),
    }
  }
}
