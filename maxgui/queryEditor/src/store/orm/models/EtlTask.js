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
import { ORM_PERSISTENT_ENTITIES } from '@queryEditorSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'

export default class EtlTask extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.ETL_TASKS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            name: this.string(''),
            status: this.string(''),
            sql_script: this.string(''),
            active_stage_index: this.number(0),
            meta: this.attr({}),
            created: this.string(new Date().toString()),
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
