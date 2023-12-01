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
 * The sql-editor emits individual events for certain shortcut keys but it works
 * only when the mouse cursor is focused on the editor. So the MxsWorkspace uses
 * v-shortkey to listen on the same shortcut keys then emit "shortkey" event.
 * This event bus will "carry" this `shortkey` event to the components handle corresponding
 * shortcut key
 * This bus emits
 * @shortkey: v:string. shortcut keys defined in app_config.QUERY_SHORTCUT_KEYS
 */
import Vue from 'vue'

export const EventBus = new Vue()
