<template>
    <div
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="d-flex flex-column fill-height worksheet-wrapper"
        @shortkey="getIsTxtEditor ? wkeShortKeyHandler($event) : null"
    >
        <wke-nav-ctr />
        <keep-alive>
            <wke-ctr
                v-if="active_wke_id"
                ref="wke"
                :key="active_wke_id"
                :ctrDim="ctrDim"
                @onCtrlEnter="onCtrlEnter"
                @onCtrlShiftEnter="onCtrlShiftEnter"
                @onCtrlD="onCtrlD"
                @onCtrlO="onCtrlO"
                @onCtrlS="onCtrlS"
                @onCtrlShiftS="onCtrlShiftS"
            />
        </keep-alive>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapGetters, mapState } from 'vuex'
import Wke from './Wke.container.vue'
import WkeNav from './WkeNav.container.vue'

export default {
    name: 'worksheets',
    components: {
        'wke-ctr': Wke,
        'wke-nav-ctr': WkeNav,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            QUERY_SHORTCUT_KEYS: state => state.app_config.QUERY_SHORTCUT_KEYS,
            active_wke_id: state => state.wke.active_wke_id,
        }),
        ...mapGetters({ getIsTxtEditor: 'editor/getIsTxtEditor' }),
    },
    methods: {
        async wkeShortKeyHandler(e) {
            switch (e.srcKey) {
                case 'win-ctrl-d':
                case 'mac-cmd-d':
                    this.onCtrlD()
                    break
                case 'win-ctrl-enter':
                case 'mac-cmd-enter':
                    await this.onCtrlEnter()
                    break
                case 'win-ctrl-shift-enter':
                case 'mac-cmd-shift-enter':
                    await this.onCtrlShiftEnter()
                    break
                case 'win-ctrl-o':
                case 'mac-cmd-o':
                    this.onCtrlO()
                    break
                case 'win-ctrl-s':
                case 'mac-cmd-s':
                    await this.onCtrlS()
                    break
                case 'win-ctrl-shift-s':
                case 'mac-cmd-shift-s':
                    await this.onCtrlShiftS()
                    break
            }
        },
        getSessionToolbar() {
            return this.$typy(this.$refs, `wke.$refs.sessionToolbar`).safeObject
        },
        getLoadSql() {
            return this.getSessionToolbar().$refs.loadSql
        },
        async onCtrlEnter() {
            await this.getSessionToolbar().handleRun('selected')
        },
        async onCtrlShiftEnter() {
            await this.getSessionToolbar().handleRun('all')
        },
        onCtrlD() {
            this.getSessionToolbar().openSnippetDlg()
        },
        onCtrlO() {
            this.getSessionToolbar().$refs.loadSql.handleFileOpen()
        },
        async onCtrlS() {
            const loadSql = this.getLoadSql()
            if (loadSql.getIsFileUnsaved && loadSql.hasFullSupport && loadSql.hasFileHandle)
                await loadSql.saveFile()
        },
        async onCtrlShiftS() {
            const loadSql = this.getLoadSql()
            if (loadSql.getIsFileUnsaved)
                if (loadSql.hasFullSupport) await loadSql.saveFileAs()
                else loadSql.saveFileLegacy()
        },
    },
}
</script>
