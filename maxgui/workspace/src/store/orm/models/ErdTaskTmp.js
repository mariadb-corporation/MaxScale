/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_TMP_ENTITIES } from '@wsSrc/store/config'

export default class ErdTaskTmp extends Extender {
    static entity = ORM_TMP_ENTITIES.ERD_TASKS_TMP

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            graph_height_pct: this.number(100),
            active_entity_id: this.string(''),
        }
    }

    static fields() {
        return {
            id: this.attr(null), // use Erd task Id as PK for this table
            ...this.getNonKeyFields(),
        }
    }
}
