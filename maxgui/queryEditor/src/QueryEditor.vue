<template>
    <div
        ref="queryViewCtr"
        v-resize.quiet="setDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="query-editor mxs-color-helper all-border-table-border fill-height"
        @shortkey="getIsTxtEditor ? wkeShortKeyHandler($event) : null"
    >
        <v-progress-linear v-if="is_validating_conn" indeterminate color="primary" />
        <div
            v-else
            class="fill-height d-flex flex-column"
            :class="{ 'query-editor--fullscreen': is_fullscreen }"
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
        </div>
        <mxs-conf-dlg
            v-model="isConfDlgOpened"
            :title="$t('confirmations.leavePage')"
            type="thatsRight"
            minBodyWidth="624px"
            :onSave="onLeave"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        >
            <template v-slot:confirm-text>
                <p>{{ $t('info.disconnectAll') }}</p>
            </template>
            <template v-slot:body-append>
                <v-checkbox
                    v-model="confirmDelAll"
                    class="v-checkbox--custom-label"
                    :label="$t('disconnectAll')"
                    color="primary"
                    dense
                    hide-details
                />
            </template>
        </mxs-conf-dlg>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/* Emits
 * @leave-page: v:Object. `to` object of beforeRouteLeave hook
 */
import { mapActions, mapState, mapMutations, mapGetters } from 'vuex'
import '@queryEditorSrc/styles/queryEditor.scss'
import Wke from '@queryEditorSrc/components/Wke.container.vue'
import WkeNav from '@queryEditorSrc/components/WkeNav.container.vue'

export default {
    name: 'query-editor',
    components: {
        'wke-ctr': Wke,
        'wke-nav-ctr': WkeNav,
    },
    data() {
        return {
            confirmDelAll: true,
            isConfDlgOpened: false,
            wkeNavCtrHeight: 32,
            dim: {},
        }
    },
    computed: {
        ...mapState({
            active_wke_id: state => state.wke.active_wke_id,
            sql_conns: state => state.queryConn.sql_conns,
            is_fullscreen: state => state.wke.is_fullscreen,
            is_validating_conn: state => state.queryConn.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.queryEditorConfig.config.QUERY_SHORTCUT_KEYS,
            worksheets_arr: state => state.wke.worksheets_arr,
        }),
        ...mapGetters({
            getActiveWke: 'wke/getActiveWke',
            getIsTxtEditor: 'editor/getIsTxtEditor',
        }),
        ctrDim() {
            return { width: this.dim.width, height: this.dim.height - this.wkeNavCtrHeight }
        },
    },
    watch: {
        active_wke_id: {
            immediate: true,
            handler(v) {
                if (v) this.handleSyncWke(this.getActiveWke)
            },
        },
        is_fullscreen(v) {
            if (v)
                this.dim = {
                    width: document.body.clientWidth,
                    height: document.body.clientHeight,
                }
            else this.$helpers.doubleRAF(() => this.setDim())
        },
    },
    async created() {
        this.handleAutoClearQueryHistory()
        await this.validatingConn()
    },
    mounted() {
        this.$nextTick(() => this.setDim())
    },

    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'appNotifier/SET_SNACK_BAR_MESSAGE' }),
        ...mapActions({
            validatingConn: 'queryConn/validatingConn',
            disconnectAll: 'queryConn/disconnectAll',
            clearConn: 'queryConn/clearConn',
            handleSyncWke: 'wke/handleSyncWke',
            handleAutoClearQueryHistory: 'queryPersisted/handleAutoClearQueryHistory',
        }),
        /**
         * sub-component doesn't have beforeRouteLeave hook, so the hook should be
         * placed in the parent component and call this beforeRouteLeave function
         * via ref
         * @param {Object} to - `to` object of beforeRouteLeave hook
         * @param {Object} from - `from` object of beforeRouteLeave hook
         * @param {Function} next - `next` function of beforeRouteLeave hook
         */
        beforeRouteLeaveHandler(to, from, next) {
            if (this.to) {
                next()
            } else {
                this.to = to
                /**
                 * Allow to leave page immediately if next path is to login page (user logouts)
                 * or if there is no active connections
                 */
                if (Object.keys(this.sql_conns).length === 0) this.leavePage()
                else
                    switch (to.path) {
                        case '/login':
                            this.leavePage()
                            break
                        case '/404':
                            this.SET_SNACK_BAR_MESSAGE({
                                text: [this.$t('info.notFoundConn')],
                                type: 'error',
                            })
                            this.cancelLeave()
                            this.clearConn()
                            this.validatingConn()
                            break
                        default:
                            this.confirmDelAll = true
                            this.isConfDlgOpened = true
                    }
            }
        },
        async onLeave() {
            if (this.confirmDelAll) await this.disconnectAll()
            this.leavePage()
        },
        leavePage() {
            this.$emit('leave-page', this.to)
        },
        cancelLeave() {
            this.to = null
        },
        setDim() {
            const { width, height } = this.$refs.queryViewCtr.getBoundingClientRect()
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
.query-editor {
    &--fullscreen {
        background: white;
        z-index: 7;
        position: fixed;
        top: 0px;
        right: 0px;
        bottom: 0px;
        left: 0px;
    }
}
</style>
