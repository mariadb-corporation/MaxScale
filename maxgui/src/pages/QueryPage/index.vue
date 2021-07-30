<template>
    <div v-resize.quiet="setCtrDim" class="fill-height">
        <div
            ref="paneContainer"
            class="query-page d-flex flex-column fill-height"
            :class="{ 'query-page--fullscreen': is_fullscreen }"
        >
            <toolbar-container ref="toolbarContainer" />
            <worksheets
                :ctrDim="ctrDim"
                @onCtrlEnter="() => $refs.toolbarContainer.handleRun('all')"
                @onCtrlShiftEnter="() => $refs.toolbarContainer.handleRun('selected')"
            />
            <confirm-dialog
                ref="confirmDialog"
                :title="$t('confirmations.leavePage')"
                type="thatsRight"
                minBodyWidth="624px"
                :onSave="onLeave"
                :onClose="cancelLeave"
                :onCancel="cancelLeave"
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
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapGetters, mapMutations } from 'vuex'
import ToolbarContainer from './ToolbarContainer'
import Worksheets from './Worksheets.vue'
export default {
    name: 'query-view',
    components: {
        ToolbarContainer,
        Worksheets,
    },
    data() {
        return {
            ctrDim: {},
            confirmDelAll: true,
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.query.is_fullscreen,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_wke_id: state => state.query.active_wke_id,
            cnct_resources: state => state.query.cnct_resources,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
            getActiveWke: 'query/getActiveWke',
        }),
    },
    watch: {
        is_fullscreen() {
            this.$help.doubleRAF(() => this.setCtrDim())
        },
        active_wke_id(v) {
            if (v) this.UPDATE_SA_WKE_STATES(this.getActiveWke)
        },
    },
    async created() {
        this.$help.doubleRAF(() => this.setCtrDim())
        await this.checkActiveConn()
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
                            text: ['Connection is not found, please reconnect'],
                            type: 'error',
                        })
                        await this.checkActiveConn()
                        break
                    default:
                        this.confirmDelAll = true
                        this.$refs.confirmDialog.open()
                }
        }
    },
    methods: {
        ...mapMutations({
            UPDATE_SA_WKE_STATES: 'query/UPDATE_SA_WKE_STATES',
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
        }),
        ...mapActions({
            checkActiveConn: 'query/checkActiveConn',
            disconnectAll: 'query/disconnectAll',
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
