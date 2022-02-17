<template>
    <div
        v-resize.quiet="setCtrDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="fill-height"
        @shortkey="isTxtEditor ? handleShortkey($event) : null"
    >
        <div
            ref="paneContainer"
            class="query-view d-flex flex-column fill-height"
            :class="{ 'query-view--fullscreen': is_fullscreen }"
        >
            <v-card v-if="is_validating_conn" class="fill-height" loading />
            <worksheets v-else ref="wkesRef" :ctrDim="ctrDim" />
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
import { mapState, mapGetters } from 'vuex'
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
            QUERY_SHORTCUT_KEYS: state => state.app_config.QUERY_SHORTCUT_KEYS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            is_fullscreen: state => state.query.is_fullscreen,
            is_validating_conn: state => state.query.is_validating_conn,
        }),
        ...mapGetters({
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
    },
    created() {
        this.$help.doubleRAF(() => this.setCtrDim())
    },

    methods: {
        setCtrDim() {
            const { width, height } = this.$refs.paneContainer.getBoundingClientRect()
            this.ctrDim = { width, height }
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
.query-view {
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
