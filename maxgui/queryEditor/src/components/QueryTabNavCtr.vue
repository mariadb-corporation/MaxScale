<template>
    <div class="d-flex flex-row">
        <v-tabs
            v-model="activeQueryTabId"
            show-arrows
            hide-slider
            :height="height"
            class="v-tabs--query-editor-style query-tab-nav v-tabs--custom-small-pagination-btn flex-grow-0"
            :style="{ maxWidth: `calc(100% - ${queryTabNavToolbarWidth + 1}px)` }"
            center-active
        >
            <v-tab
                v-for="queryTab in queryTabsOfActiveWke"
                :key="`${queryTab.id}`"
                :href="`#${queryTab.id}`"
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active"
            >
                <div
                    style="min-width:160px"
                    class="fill-height d-flex align-center justify-space-between px-3"
                >
                    <div class="d-inline-flex align-center">
                        <mxs-truncate-str
                            :tooltipItem="{ txt: `${queryTab.name}`, nudgeLeft: 36 }"
                            :maxWidth="112"
                        />
                        <span
                            v-if="getIsFileUnsavedByQueryTabId(queryTab.id)"
                            class="unsaved-changes-indicator"
                        />
                        <v-progress-circular
                            v-if="getLoadingQueryResultByQueryTabId(queryTab.id)"
                            class="ml-2"
                            size="16"
                            width="2"
                            color="primary"
                            indeterminate
                        />
                    </div>
                    <v-btn
                        class="ml-1 del-tab-btn"
                        icon
                        x-small
                        :disabled="getIsConnBusyByQueryTabId(queryTab.id)"
                        @click.stop.prevent="
                            getIsFileUnsavedByQueryTabId(queryTab.id)
                                ? openFileDlg(queryTab)
                                : handleDeleteTab(queryTab)
                        "
                    >
                        <v-icon
                            size="8"
                            :color="getIsConnBusyByQueryTabId(queryTab.id) ? '' : 'error'"
                        >
                            $vuetify.icons.mxs_close
                        </v-icon>
                    </v-btn>
                </div>
            </v-tab>
        </v-tabs>
        <query-tab-nav-toolbar-ctr @get-total-btn-width="queryTabNavToolbarWidth = $event" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapGetters } from 'vuex'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTabNavToolbarCtr from './QueryTabNavToolbarCtr.vue'
import saveFile from '@queryEditorSrc/mixins/saveFile'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'

export default {
    name: 'query-tab-nav-ctr',
    components: { QueryTabNavToolbarCtr },
    mixins: [saveFile],
    props: { height: { type: Number, required: true } },
    data() {
        return {
            queryTabNavToolbarWidth: 0,
        }
    },
    computed: {
        ...mapGetters({
            getIsConnBusyByQueryTabId: 'queryConns/getIsConnBusyByQueryTabId',
            getLoadingQueryResultByQueryTabId: 'queryResult/getLoadingQueryResultByQueryTabId',
            getIsFileUnsavedByQueryTabId: 'editors/getIsFileUnsavedByQueryTabId',
        }),
        activeQueryTabId: {
            get() {
                return QueryTab.getters('getActiveQueryTabId')
            },
            set(v) {
                if (v)
                    QueryTab.commit(
                        state =>
                            (state.active_query_tab_map[Worksheet.getters('getActiveWkeId')] = v)
                    )
            },
        },
        queryTabsOfActiveWke() {
            return QueryTab.getters('getQueryTabsOfActiveWke')
        },
    },
    methods: {
        ...mapMutations({
            SET_FILE_DLG_DATA: 'editors/SET_FILE_DLG_DATA',
        }),
        /**
         * @param {Object} queryTab - queryTab object
         */
        openFileDlg(queryTab) {
            this.SET_FILE_DLG_DATA({
                is_opened: true,
                title: this.$mxs_t('deleteQueryTab'),
                confirm_msg: this.$mxs_t('confirmations.deleteQueryTab', {
                    targetId: queryTab.name,
                }),
                on_save: async () => {
                    await this.handleSaveFile(queryTab)
                    await this.handleDeleteTab(queryTab)
                },
                dont_save: async () => await this.handleDeleteTab(queryTab),
            })
        },
        async handleDeleteTab(queryTab) {
            if (this.queryTabsOfActiveWke.length === 1)
                QueryTab.dispatch('refreshLastQueryTab', queryTab.id)
            else await QueryTab.dispatch('handleDeleteQueryTab', queryTab.id)
        },
    },
}
</script>
<style lang="scss" scoped>
.unsaved-changes-indicator::after {
    content: ' *';
    color: $primary;
    padding-left: 4px;
    font-size: 0.875rem;
    position: relative;
    font-weight: 500;
    font-family: $heading-font-family;
}
</style>
