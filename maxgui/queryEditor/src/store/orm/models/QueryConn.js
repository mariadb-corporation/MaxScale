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

export default class QueryConn extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_CONNS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            active_db: this.string(''),
            attributes: this.attr({}),
            binding_type: this.string(''),
            name: this.string(''),
            meta: this.attr({}),
            clone_of_conn_id: this.attr(null).nullable(),
        }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK EtlTask
            etl_task_id: this.attr(null).nullable(),
            //FK, one to one inverse
            query_tab_id: this.attr(null).nullable(),
            worksheet_id: this.attr(null).nullable(),
            queryTab: this.belongsTo(ORM_PERSISTENT_ENTITIES.QUERY_TABS, 'query_tab_id'),
            worksheet: this.belongsTo(ORM_PERSISTENT_ENTITIES.WORKSHEETS, 'worksheet_id'),
        }
    }
}
