<template>
    <div
        ref="queryViewCtr"
        v-resize.quiet="setDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="query-editor fill-height"
        @shortkey="eventBus.$emit('shortkey', $event.srcKey)"
    >
        <div
            class="fill-height d-flex flex-column"
            :class="{ 'query-editor--fullscreen': is_fullscreen }"
        >
            <div v-if="$slots['query-editor-top']" ref="queryEditorTopSlot">
                <slot name="query-editor-top" />
            </div>
            <v-progress-linear v-if="is_validating_conn" indeterminate color="primary" />
            <template v-else>
                <wke-nav-ctr
                    v-if="!hidden_comp.includes('wke-nav-ctr')"
                    :height="wkeNavCtrHeight"
                />
                <keep-alive v-for="wke in worksheets_arr" :key="wke.id" max="15">
                    <wke-ctr
                        v-if="active_wke_id === wke.id && ctrDim.height"
                        ref="wke"
                        :ctrDim="ctrDim"
                    >
                        <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                    </wke-ctr>
                </keep-alive>
            </template>
        </div>
        <mxs-conf-dlg
            v-model="isConfDlgOpened"
            :title="$mxs_t('confirmations.leavePage')"
            saveText="thatsRight"
            minBodyWidth="624px"
            :onSave="onLeave"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        >
            <template v-slot:confirm-text>
                <p>{{ $mxs_t('info.disconnectAll') }}</p>
            </template>
            <template v-slot:body-append>
                <v-checkbox
                    v-model="confirmDelAll"
                    class="v-checkbox--custom-label"
                    :label="$mxs_t('disconnectAll')"
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
 * Change Date: 2026-10-04
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
import WkeCtr from '@queryEditorSrc/components/WkeCtr.vue'
import WkeNavCtr from '@queryEditorSrc/components/WkeNavCtr.vue'
import { EventBus } from '@queryEditorSrc/components/EventBus'

export default {
    name: 'query-editor',
    components: {
        WkeCtr,
        WkeNavCtr,
    },
    data() {
        return {
            confirmDelAll: true,
            isConfDlgOpened: false,
            dim: {},
            queryEditorTopSlotHeight: 0,
        }
    },
    computed: {
        ...mapState({
            active_wke_id: state => state.wke.active_wke_id,
            sql_conns: state => state.queryConn.sql_conns,
            is_fullscreen: state => state.queryPersisted.is_fullscreen,
            is_validating_conn: state => state.queryConn.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.queryEditorConfig.config.QUERY_SHORTCUT_KEYS,
            worksheets_arr: state => state.wke.worksheets_arr,
            hidden_comp: state => state.queryEditorConfig.hidden_comp,
        }),
        ...mapGetters({
            getActiveWke: 'wke/getActiveWke',
            getIsTxtEditor: 'editor/getIsTxtEditor',
        }),
        wkeNavCtrHeight() {
            return this.hidden_comp.includes('wke-nav-ctr') ? 0 : 32
        },
        ctrDim() {
            return {
                width: this.dim.width,
                height: this.dim.height - this.wkeNavCtrHeight - this.queryEditorTopSlotHeight,
            }
        },
        eventBus() {
            return EventBus
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
        this.$nextTick(() => this.setDim(), this.setQueryEditorTopSlotHeight())
    },

    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
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
                                text: [this.$mxs_t('info.notFoundConn')],
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
        setQueryEditorTopSlotHeight() {
            if (this.$refs.queryEditorTopSlot) {
                const { height } = this.$refs.queryEditorTopSlot.getBoundingClientRect()
                this.queryEditorTopSlotHeight = height
            }
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
