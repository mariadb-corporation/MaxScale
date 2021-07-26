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
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.query.is_fullscreen,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_wke_id: state => state.query.active_wke_id,
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
    async beforeDestroy() {
        if (process.env.NODE_ENV !== 'development' && this.curr_cnct_resource)
            await this.disconnect()
    },
    methods: {
        ...mapMutations({ UPDATE_SA_WKE_STATES: 'query/UPDATE_SA_WKE_STATES' }),
        ...mapActions({
            disconnect: 'query/disconnect',
            checkActiveConn: 'query/checkActiveConn',
        }),
        setCtrDim() {
            const { width, height } = this.$refs.paneContainer.getBoundingClientRect()
            this.ctrDim = { width, height }
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
