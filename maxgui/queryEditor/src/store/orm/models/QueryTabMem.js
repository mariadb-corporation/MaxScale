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

export default class QueryTabMem extends Extender {
    static entity = ORM_MEM_ENTITIES.QUERY_TABS_MEM

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            // fields for QueryConn
            is_conn_busy: this.boolean(false),
            lost_cnn_err_msg_obj: this.attr({}),
            // fields for QueryResult
            has_kill_flag: this.boolean(false),
            /**
             * Below object fields have these properties
             * @property {number} request_sent_time
             * @property {number} total_duration
             * @property {boolean} is_loading
             * @property {object} data
             * @property {object} abort_controller // only query_results has it
             */
            prvw_data: this.attr({}),
            prvw_data_details: this.attr({}),
            query_results: this.attr({}),
        }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK
            query_tab_id: this.attr(null),
        }
    }
}
