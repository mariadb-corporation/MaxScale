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
import { ORM_PERSISTENT_ENTITIES, EDITOR_MODES } from '@queryEditorSrc/store/config'

export default class Editor extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.EDITORS
    //Persistence
    static state() {
        return {
            /**
             * vuex-orm serialized fields defined in the static fields(),
             * so FileSystemFileHandle can't be stored there.
             * This state stores fileHandle with key is the QueryTabId
             * key is query_tab_id or editor id as they are the same, value is blob_file data
             */
            blob_file_map: {},
        }
    }

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            query_txt: this.string(''),
            curr_ddl_alter_spec: this.string(''),
            curr_editor_mode: this.string(EDITOR_MODES.TXT_EDITOR),
            tbl_creation_info: this.attr({}),
        }
    }

    static fields() {
        return {
            id: this.attr(null), // use QueryTab Id as PK for this table
            ...this.getNonKeyFields(),
        }
    }
}
