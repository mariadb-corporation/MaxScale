<template>
    <div v-resize.quiet="setCtrDim" class="fill-height query-view-wrapper">
        <div
            ref="paneContainer"
            class="query-view d-flex flex-column fill-height"
            :class="{ 'query-view--fullscreen': is_fullscreen }"
        >
            <v-card v-if="is_validating_conn" class="fill-height" loading />
            <worksheets v-else :ctrDim="ctrDim" />
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
import { mapState } from 'vuex'
import Worksheets from './Worksheets.vue'
export default {
    name: 'query-view',
    components: {
        Worksheets,
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
        }),
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
