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
import { uuidv1 } from '@share/utils/helpers'
import { ORM_MEM_ENTITIES } from '@queryEditorSrc/store/config'

export default class WorksheetMem extends Extender {
    static entity = ORM_MEM_ENTITIES.WORKSHEETS_MEM

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            // fields for SchemaSidebar
            /**
             * TODO: Make active_prvw_node an object hash with queryTab id as key
             * @property {boolean} loading_db_tree
             * @property {array} db_completion_list
             * @property {array} data - Contains schemas array
             * @property {object} active_prvw_node - Contains active node in the schemas array
             * @property {string} data_of_conn - Name of the connection using to fetch data
             */
            db_tree: this.attr({}),
            /**
             * @property {object} data - Contains res.data.data.attributes of a query
             * @property {object} stmt_err_msg_obj
             * @property {array} result - error msg array.
             */
            exe_stmt_result: this.attr({}),
        }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK
            worksheet_id: this.attr(null),
        }
    }
}
