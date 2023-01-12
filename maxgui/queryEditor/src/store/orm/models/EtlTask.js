/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@queryEditorSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES, ETL_STATUS, ETL_STAGE_INDEX } from '@queryEditorSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'

export default class EtlTask extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.ETL_TASKS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            name: this.string(''),
            status: this.string(ETL_STATUS.INITIALIZING),
            sql_script: this.string(''),
            active_stage_index: this.number(ETL_STAGE_INDEX.CONN),
            meta: this.attr({}),
            logs: this.attr([]),
            created: this.number(Date.now()),
        }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            connections: this.hasMany(ORM_PERSISTENT_ENTITIES.QUERY_CONNS, 'etl_task_id'),
        }
    }
}
