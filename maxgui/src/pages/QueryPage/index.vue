<template>
    <div v-resize.quiet="setPanelsPct" class="fill-height">
        <div
            ref="paneContainer"
            class="query-page d-flex flex-column fill-height"
            :class="{ 'query-page--fullscreen': is_fullscreen }"
        >
            <toolbar-container ref="toolbarContainer" />
            <split-pane
                v-if="minSidebarPct"
                v-model="sidebarPct"
                class="query-page__content"
                :minPercent="minSidebarPct"
                split="vert"
                :disable="is_sidebar_collapsed"
            >
                <template slot="pane-left">
                    <!-- TODO: move sidebar-container to <worksheet/> -->
                    <sidebar-container
                        @place-to-editor="wkeRef ? wkeRef.placeToEditor($event) : () => null"
                        @dragging-schema="wkeRef ? wkeRef.draggingSchema($event) : () => null"
                        @drop-schema-to-editor="
                            wkeRef ? wkeRef.dropSchemaToEditor($event) : () => null
                        "
                    />
                </template>
                <template slot="pane-right">
                    <worksheets
                        ref="worksheets"
                        :containerHeight="containerHeight"
                        @mounted="isWkeMounted = $event"
                        @onCtrlEnter="() => $refs.toolbarContainer.handleRun('all')"
                        @onCtrlShiftEnter="() => $refs.toolbarContainer.handleRun('selected')"
                    />
                </template>
            </split-pane>
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
import SidebarContainer from './SidebarContainer'
import { mapActions, mapState, mapGetters, mapMutations } from 'vuex'
import ToolbarContainer from './ToolbarContainer'
import Worksheets from './Worksheets.vue'
export default {
    name: 'query-view',
    components: {
        SidebarContainer,
        ToolbarContainer,
        Worksheets,
    },
    data() {
        return {
            containerHeight: 0,
            minSidebarPct: 0,
            sidebarPct: 0,
            isWkeMounted: false,
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.query.is_fullscreen,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_wke_id: state => state.query.active_wke_id,
            is_sidebar_collapsed: state => state.query.is_sidebar_collapsed,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
            getActiveWke: 'query/getActiveWke',
        }),
        wkeRef() {
            // wke ref is only available when it's fully mounted
            if (this.isWkeMounted) return this.$typy(this.$refs, 'worksheets.$refs.wke').safeObject
            return null
        },
    },
    watch: {
        is_fullscreen() {
            this.$help.doubleRAF(() => {
                // recalculate panes percent
                this.setPanelsPct()
            })
        },
        is_sidebar_collapsed() {
            this.$help.doubleRAF(() => this.handleSetSidebarPct())
        },
        sidebarPct() {
            this.$help.doubleRAF(() => {
                if (this.wkeRef) this.wkeRef.setResultPaneDim()
            })
        },
        active_wke_id(v) {
            if (v) this.UPDATE_SA_WKE_STATES(this.getActiveWke)
        },
    },
    async created() {
        await this.checkActiveConn()
    },
    async beforeDestroy() {
        if (process.env.NODE_ENV !== 'development' && this.curr_cnct_resource)
            await this.disconnect()
    },
    mounted() {
        this.$help.doubleRAF(() => this.setPanelsPct())
    },
    methods: {
        ...mapMutations({ UPDATE_SA_WKE_STATES: 'query/UPDATE_SA_WKE_STATES' }),
        ...mapActions({
            disconnect: 'query/disconnect',
            checkActiveConn: 'query/checkActiveConn',
        }),
        setPanelsPct() {
            this.handleSetSidebarPct()
            /**
             * get pane container height then pass it via props to worksheets
             * to calculate min percent of worksheets child panes
             */
            this.containerHeight = this.$refs.paneContainer.clientHeight
        },
        handleSetSidebarPct() {
            const containerWidth = this.$refs.paneContainer.clientWidth
            if (this.is_sidebar_collapsed) {
                this.minSidebarPct = this.$help.pxToPct({ px: 40, containerPx: containerWidth })
                this.sidebarPct = this.minSidebarPct
            } else {
                this.minSidebarPct = this.$help.pxToPct({ px: 200, containerPx: containerWidth })
                this.sidebarPct = this.$help.pxToPct({ px: 240, containerPx: containerWidth })
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
