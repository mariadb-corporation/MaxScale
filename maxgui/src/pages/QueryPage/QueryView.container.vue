<template>
    <v-card
        ref="queryViewCtr"
        v-resize.quiet="setDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="query-view fill-height d-flex flex-column"
        :class="{ 'query-view--fullscreen': is_fullscreen }"
        tile
        :loading="is_validating_conn"
        @shortkey="getIsTxtEditor ? wkeShortKeyHandler($event) : null"
    >
        <template v-if="!is_validating_conn">
            <wke-nav-ctr :height="wkeNavCtrHeight" />
            <keep-alive v-for="wke in worksheets_arr" :key="wke.id" max="15">
                <wke-ctr
                    v-if="active_wke_id === wke.id && ctrDim.height"
                    ref="wke"
                    :ctrDim="ctrDim"
                    @onCtrlEnter="onCtrlEnter"
                    @onCtrlShiftEnter="onCtrlShiftEnter"
                    @onCtrlD="onCtrlD"
                    @onCtrlO="onCtrlO"
                    @onCtrlS="onCtrlS"
                    @onCtrlShiftS="onCtrlShiftS"
                />
            </keep-alive>
        </template>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'
import Wke from './Wke.container.vue'
import WkeNav from './WkeNav.container.vue'

export default {
    name: 'query-view-ctr',
    components: {
        'wke-ctr': Wke,
        'wke-nav-ctr': WkeNav,
    },
    data() {
        return {
            wkeNavCtrHeight: 32,
            dim: {},
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.wke.is_fullscreen,
            is_validating_conn: state => state.queryConn.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.app_config.QUERY_SHORTCUT_KEYS,
            active_wke_id: state => state.wke.active_wke_id,
            worksheets_arr: state => state.wke.worksheets_arr,
        }),
        ...mapGetters({ getIsTxtEditor: 'editor/getIsTxtEditor' }),
        ctrDim() {
            return { width: this.dim.width, height: this.dim.height - this.wkeNavCtrHeight }
        },
    },
    watch: {
        is_fullscreen(v) {
            if (v)
                this.dim = {
                    width: document.body.clientWidth,
                    height: document.body.clientHeight,
                }
            else this.$help.doubleRAF(() => this.setDim())
        },
    },
    mounted() {
        this.$nextTick(() => this.setDim())
    },

    methods: {
        setDim() {
            const { width, height } = this.$refs.queryViewCtr.$el.getBoundingClientRect()
            this.dim = { width, height }
        },
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
        //TODO: Refactor to use event bus instead of accessing the target component via $refs
        getTxtEditorToolbar() {
            return this.$typy(this.$refs, `wke[0].$refs.editor[0].$refs.txtEditorToolbar`)
                .safeObject
        },
        getLoadSqlCtr() {
            return this.getTxtEditorToolbar().$refs.loadSqlCtr
        },
        async onCtrlEnter() {
            await this.getTxtEditorToolbar().handleRun('selected')
        },
        async onCtrlShiftEnter() {
            await this.getTxtEditorToolbar().handleRun('all')
        },
        onCtrlD() {
            this.getTxtEditorToolbar().openSnippetDlg()
        },
        onCtrlO() {
            this.getLoadSqlCtr().handleFileOpen()
        },
        async onCtrlS() {
            const loadSqlCtr = this.getLoadSqlCtr()
            if (
                loadSqlCtr.getIsFileUnsaved &&
                loadSqlCtr.hasFullSupport &&
                loadSqlCtr.hasFileHandle
            )
                await loadSqlCtr.saveFile()
        },
        async onCtrlShiftS() {
            const loadSqlCtr = this.getLoadSqlCtr()
            if (loadSqlCtr.getIsFileUnsaved)
                if (loadSqlCtr.hasFullSupport) await loadSqlCtr.saveFileAs()
                else loadSqlCtr.saveFileLegacy()
        },
    },
}
</script>

<style lang="scss" scoped>
.query-view {
    box-shadow: none !important;
    &--fullscreen {
        z-index: 7;
        position: fixed;
        top: 0px;
        right: 0px;
        bottom: 0px;
        left: 0px;
    }
}
</style>
