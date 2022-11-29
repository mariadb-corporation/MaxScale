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
import { Model } from '@vuex-orm/core'
import { ORM_PERSISTENT_ENTITIES, EDITOR_MODES } from '@queryEditorSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'

export default class Editor extends Model {
    static entity = ORM_PERSISTENT_ENTITIES.EDITORS

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            query_text: this.string(''),
            curr_ddl_alter_spec: this.string(''),
            blob_file: this.attr(null),
            curr_editor_mode: this.string(EDITOR_MODES.TXT_EDITOR),
            tbl_creation_info: this.attr(null),
            //FK
            query_tab_id: this.attr(null),
        }
    }
}
