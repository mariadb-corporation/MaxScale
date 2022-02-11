<template>
    <div
        v-resize.quiet="setCtrDim"
        v-shortkey="{
            'win-ctrl-s': ['ctrl', 's'],
            'mac-cmd-s': ['meta', 's'],
            'win-ctrl-enter': ['ctrl', 'enter'],
            'mac-cmd-enter': ['meta', 'enter'],
            'win-ctrl-shift-enter': ['ctrl', 'shift', 'enter'],
            'mac-cmd-shift-enter': ['meta', 'shift', 'enter'],
        }"
        class="fill-height"
        @shortkey="isTxtEditor ? handleShortkey($event) : null"
    >
        <div
            ref="paneContainer"
            class="query-page d-flex flex-column fill-height"
            :class="{ 'query-page--fullscreen': is_fullscreen }"
        >
            <worksheets ref="wkesRef" :ctrDim="ctrDim" />
            <confirm-dialog
                v-model="isConfDlgOpened"
                :title="$t('confirmations.leavePage')"
                type="thatsRight"
                minBodyWidth="624px"
                :onSave="onLeave"
                @on-close="cancelLeave"
                @on-cancel="cancelLeave"
            >
                <template v-slot:body-append>
                    <v-checkbox
                        v-model="confirmDelAll"
                        class="small"
                        :label="$t('disconnectAll')"
                        color="primary"
                        hide-details
                    />
                </template>
                <template v-slot:body-prepend>
                    <p>{{ $t('info.disconnectAll') }}</p>
                </template>
            </confirm-dialog>
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
import { mapActions, mapState, mapGetters, mapMutations } from 'vuex'
import Worksheets from './Worksheets.vue'
export default {
    name: 'query-view',
    components: {
        Worksheets,
    },
    data() {
        return {
            ctrDim: {},
            confirmDelAll: true,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            is_fullscreen: state => state.query.is_fullscreen,
            active_wke_id: state => state.query.active_wke_id,
            cnct_resources: state => state.query.cnct_resources,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
            getActiveWke: 'query/getActiveWke',
            getCurrEditorMode: 'query/getCurrEditorMode',
        }),
        isTxtEditor() {
            return this.getCurrEditorMode === this.SQL_EDITOR_MODES.TXT_EDITOR
        },
    },
    watch: {
        is_fullscreen() {
            this.$help.doubleRAF(() => this.setCtrDim())
        },
        async active_wke_id(v) {
            if (v) this.UPDATE_SA_WKE_STATES(this.getActiveWke)
        },
    },
    async created() {
        this.handleAutoClearQueryHistory()
        this.$help.doubleRAF(() => this.setCtrDim())
        await this.validatingConn()
    },

    async beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            // If next path is to login page (user logouts) or there is no active connections, don't need to show dialog
            if (this.cnct_resources.length === 0) this.leavePage()
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
                        await this.validatingConn()
                        break
                    default:
                        this.confirmDelAll = true
                        this.isConfDlgOpened = true
                }
        }
    },
    methods: {
        ...mapMutations({
            UPDATE_SA_WKE_STATES: 'query/UPDATE_SA_WKE_STATES',
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
        }),
        ...mapActions({
            validatingConn: 'query/validatingConn',
            disconnectAll: 'query/disconnectAll',
            clearConn: 'query/clearConn',
            handleAutoClearQueryHistory: 'persisted/handleAutoClearQueryHistory',
        }),
        setCtrDim() {
            const { width, height } = this.$refs.paneContainer.getBoundingClientRect()
            this.ctrDim = { width, height }
        },
        async onLeave() {
            if (this.confirmDelAll) await this.disconnectAll()
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        handleShortkey(e) {
            const wkes = this.$refs.wkesRef.$refs
            switch (e.srcKey) {
                case 'win-ctrl-s':
                case 'mac-cmd-s':
                    wkes.pageToolbar.openFavoriteDialog()
                    break
                case 'win-ctrl-enter':
                case 'mac-cmd-enter':
                    wkes.wkeToolbar.handleRun('selected')
                    break
                case 'win-ctrl-shift-enter':
                case 'mac-cmd-shift-enter':
                    wkes.wkeToolbar.handleRun('all')
                    break
            }
        },
    },
}
</script>

<style lang="scss" scoped>
$header-height: 50px;
.query-page {
    background: #ffffff;
    &--fullscreen {
        padding: 0px !important;
        width: 100%;
        height: calc(100% - #{$header-height});
        margin-left: -90px;
        margin-top: -24px;
        z-index: 7;
        position: fixed;
        overflow: hidden;
    }
}
</style>
