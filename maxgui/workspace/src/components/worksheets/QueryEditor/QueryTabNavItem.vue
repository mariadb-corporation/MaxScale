<template>
    <div style="min-width:162px" class="fill-height d-flex align-center justify-space-between px-3">
        <div class="d-inline-flex align-center">
            <mxs-truncate-str
                :tooltipItem="{ txt: `${queryTab.name}`, nudgeLeft: 36 }"
                :maxWidth="112"
            />
            <span v-if="isQueryTabUnsaved" class="unsaved-changes-indicator" />
            <v-progress-circular
                v-if="isLoadingQueryResult"
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
            :disabled="isQueryTabConnBusy"
            @click.stop.prevent="isQueryTabUnsaved ? openFileDlg() : handleDeleteTab()"
        >
            <v-icon size="8" :color="isQueryTabConnBusy ? '' : 'error'">
                $vuetify.icons.mxs_close
            </v-icon>
        </v-btn>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapGetters } from 'vuex'
import QueryTab from '@wsModels/QueryTab'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import saveFile from '@wsSrc/mixins/saveFile'

export default {
    name: 'query-tab-nav-item',
    mixins: [saveFile],
    props: { queryTab: { type: Object, required: true } },
    data() {
        return {
            queryTabNavToolbarWidth: 0,
        }
    },
    computed: {
        ...mapGetters({ getIsQueryTabUnsaved: 'fileSysAccess/getIsQueryTabUnsaved' }),
        isQueryTabUnsaved() {
            return this.getIsQueryTabUnsaved(this.queryTab.id)
        },
        queryTabsOfActiveWke() {
            return QueryTab.getters('queryTabsOfActiveWke')
        },
        isLoadingQueryResult() {
            return QueryResult.getters('findIsLoading')(this.queryTab.id)
        },
        isQueryTabConnBusy() {
            return QueryConn.getters('isConnBusyByQueryTabId')(this.queryTab.id)
        },
    },
    methods: {
        ...mapMutations({
            SET_FILE_DLG_DATA: 'editorsMem/SET_FILE_DLG_DATA',
        }),
        openFileDlg() {
            this.SET_FILE_DLG_DATA({
                is_opened: true,
                title: this.$mxs_t('deleteQueryTab'),
                confirm_msg: this.$mxs_t('confirmations.deleteQueryTab', {
                    targetId: this.queryTab.name,
                }),
                on_save: async () => {
                    await this.handleSaveFile(this.queryTab)
                    await this.handleDeleteTab()
                },
                dont_save: async () => await this.handleDeleteTab(),
            })
        },
        async handleDeleteTab() {
            if (this.queryTabsOfActiveWke.length === 1)
                QueryTab.dispatch('refreshLastQueryTab', this.queryTab.id)
            else await QueryTab.dispatch('handleDeleteQueryTab', this.queryTab.id)
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
