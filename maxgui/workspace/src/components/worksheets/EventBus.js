/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * Reason for using EventBus: mxs-sql-editor component emits individual events for
 * certain shortcut keys but it works only when the mouse cursor is focused on the
 * editor. So `workspace-ctr` component uses v-shortkey to listen on the same
 * shortcut keys and emit "workspace-shortkey" event. This event bus will "carry"
 * that event to the components handle the event.
 *
 * This event bus currently carries the following events:
 * workspace-shortkey: v:string. shortcut keys defined in app_config.QUERY_SHORTCUT_KEYS
 * entity-editor-ctr-update-node-data. Emits after successfully executed queries in `entity-editor-ctr`
 * component.
 */
import Vue from 'vue'

export const EventBus = new Vue()
