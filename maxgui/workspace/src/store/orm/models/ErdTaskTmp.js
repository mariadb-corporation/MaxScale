/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_TMP_ENTITIES, DDL_EDITOR_SPECS } from '@wsSrc/constants'
import { uuidv1 } from '@share/utils/helpers'

export default class ErdTaskTmp extends Extender {
    static entity = ORM_TMP_ENTITIES.ERD_TASKS_TMP

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            graph_height_pct: this.number(100),
            active_entity_id: this.string(''),
            active_spec: this.string(DDL_EDITOR_SPECS.COLUMNS),
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
