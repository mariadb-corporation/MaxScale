<template>
    <div
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="d-flex flex-column fill-height worksheet-wrapper"
        @shortkey="isTxtEditor ? wkeShortKeyHandler($event) : null"
    >
        <div class="d-flex flex-row">
            <v-tabs
                v-model="activeWkeID"
                show-arrows
                hide-slider
                :height="wkeNavHeight"
                class="tab-navigation--btn-style wke-navigation flex-grow-0"
                :style="{
                    maxWidth: `calc(100% - ${pageToolbarBtnWidth + 1}px)`,
                    height: `${wkeNavHeight}px`,
                }"
            >
                <v-tab
                    v-for="wke in worksheets_arr"
                    :key="wke.id"
                    :href="`#${wke.id}`"
                    class="pa-0 tab-btn text-none"
                    active-class="tab-btn--active"
                >
                    <v-tooltip
                        :disabled="!$typy(getWkeLastSessConnByWkeId(wke.id), 'name').safeString"
                        top
                        transition="slide-x-transition"
                        content-class="shadow-drop"
                    >
                        <template v-slot:activator="{ on }">
                            <div
                                style="min-width:160px"
                                class="fill-height d-flex align-center justify-space-between px-3"
                                v-on="on"
                            >
                                <div class="d-inline-flex align-center">
                                    <span
                                        class="tab-name d-inline-block text-truncate"
                                        style="max-width:88px"
                                    >
                                        {{ wke.name }}
                                    </span>
                                    <v-progress-circular
                                        v-if="isWkeLoadingQueryResult(wke.id)"
                                        class="ml-2"
                                        size="16"
                                        width="2"
                                        color="primary"
                                        indeterminate
                                    />
                                </div>
                                <v-btn
                                    v-if="worksheets_arr.length > 1"
                                    class="ml-1 del-wke-btn"
                                    icon
                                    x-small
                                    :disabled="
                                        $typy(is_conn_busy_map[getActiveSessionId], 'value')
                                            .safeBoolean
                                    "
                                    @click.stop.prevent="handleDeleteWke(wke.id)"
                                >
                                    <v-icon
                                        size="8"
                                        :color="
                                            $typy(is_conn_busy_map[getActiveSessionId], 'value')
                                                .safeBoolean
                                                ? ''
                                                : 'error'
                                        "
                                    >
                                        $vuetify.icons.close
                                    </v-icon>
                                </v-btn>
                            </div>
                        </template>
                        <span class="color text-text py-2 px-4">
                            {{ $t('connectedTo') }}
                            {{ $typy(getWkeLastSessConnByWkeId(wke.id), 'name').safeString }}
                        </span>
                    </v-tooltip>
                </v-tab>
            </v-tabs>
            <page-toolbar @get-total-btn-width="pageToolbarBtnWidth = $event" />
        </div>
        <keep-alive>
            <worksheet
                v-if="activeWkeID"
                ref="wke"
                :key="activeWkeID"
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
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import Worksheet from './Worksheet'
import PageToolbar from './PageToolbar.vue'

export default {
    name: 'worksheets',
    components: {
        Worksheet,
        PageToolbar,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            wkeNavHeight: 32,
            pageToolbarBtnWidth: 128,
        }
    },
    computed: {
        ...mapState({
            QUERY_SHORTCUT_KEYS: state => state.app_config.QUERY_SHORTCUT_KEYS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            worksheets_arr: state => state.wke.worksheets_arr,
            active_wke_id: state => state.wke.active_wke_id,
            is_conn_busy_map: state => state.queryConn.is_conn_busy_map,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getWkeLastSessConnByWkeId: 'queryConn/getWkeLastSessConnByWkeId',
            isWkeLoadingQueryResult: 'queryResult/isWkeLoadingQueryResult',
            getCurrEditorMode: 'editor/getCurrEditorMode',
        }),
        activeWkeID: {
            get() {
                return this.active_wke_id
            },
            set(v) {
                if (v) this.SET_ACTIVE_WKE_ID(v)
            },
        },
        isTxtEditor() {
            return this.getCurrEditorMode === this.SQL_EDITOR_MODES.TXT_EDITOR
        },
    },
    methods: {
        ...mapMutations({ SET_ACTIVE_WKE_ID: 'wke/SET_ACTIVE_WKE_ID' }),
        ...mapActions({
            handleDeleteWke: 'wke/handleDeleteWke',
        }),
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
        getSessionToolbar() {
            return this.$typy(this.$refs, `wke.$refs.sessionToolbar`).safeObject
        },
        getLoadSql() {
            return this.getSessionToolbar().$refs.loadSql
        },
        async onCtrlEnter() {
            await this.getSessionToolbar().handleRun('selected')
        },
        async onCtrlShiftEnter() {
            await this.getSessionToolbar().handleRun('all')
        },
        onCtrlD() {
            this.getSessionToolbar().openSnippetDlg()
        },
        onCtrlO() {
            this.getSessionToolbar().$refs.loadSql.handleFileOpen()
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
.wke-navigation {
    .tab-btn {
        &:first-of-type {
            border-left: none !important;
        }
        border-bottom: none !important;
        border-top: none !important;
        .del-wke-btn {
            visibility: hidden;
        }
        &:hover {
            .del-wke-btn {
                visibility: visible;
            }
        }
    }
}
</style>
