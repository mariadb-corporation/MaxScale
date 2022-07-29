<template>
    <div v-resize.quiet="setCtrDim" class="fill-height query-view-wrapper">
        <div
            ref="paneContainer"
            class="query-view d-flex flex-column fill-height"
            :class="{ 'query-view--fullscreen': is_fullscreen }"
        >
            <v-card
                v-shortkey="QUERY_SHORTCUT_KEYS"
                class="fill-height d-flex flex-column fill-height worksheet-wrapper"
                :loading="is_validating_conn"
                @shortkey="getIsTxtEditor ? wkeShortKeyHandler($event) : null"
            >
                <div class="d-flex flex-column fill-height worksheet-wrapper">
                    <wke-nav-ctr />
                    <keep-alive v-for="wke in worksheets_arr" :key="wke.id" max="15">
                        <wke-ctr
                            v-if="active_wke_id === wke.id"
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
                </div>
            </v-card>
        </div>
    </div>
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
            ctrDim: {},
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
    },
    watch: {
        is_fullscreen() {
            this.$help.doubleRAF(() => this.setCtrDim())
        },
    },
    created() {
        this.$help.doubleRAF(() => this.setCtrDim())
    },

    methods: {
        setCtrDim() {
            const { width, height } = this.$refs.paneContainer.getBoundingClientRect()
            this.ctrDim = { width, height }
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
        getTxtEditorToolbar() {
            return this.$typy(this.$refs, `wke[0].$refs.txtEditor[0].$refs.txtEditorToolbar`)
                .safeObject
        },
        getLoadSql() {
            return this.getTxtEditorToolbar().$refs.loadSql
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
            this.getTxtEditorToolbar().$refs.loadSql.handleFileOpen()
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

<style lang="scss" scoped>
$header-height: 50px;
$app-sidebar-width: 50px;
.query-view-wrapper {
    // ignore root padding
    margin-left: -36px;
    margin-top: -24px;
    width: calc(100% + 72px);
    height: calc(100% + 48px);
    .query-view {
        background: #ffffff;
        &--fullscreen {
            padding: 0px !important;
            width: 100%;
            height: calc(100% + #{$header-height});
            margin-left: -#{$app-sidebar-width};
            margin-top: -#{$header-height};
            z-index: 7;
            position: fixed;
            overflow: hidden;
        }
    }
}
</style>
