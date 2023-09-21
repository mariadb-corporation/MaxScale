<template>
    <div style="min-width:162px" class="fill-height d-flex align-center justify-space-between px-3">
        <div class="d-inline-flex align-center">
            <mxs-truncate-str
                :tooltipItem="{ txt: `${queryTab.name}`, nudgeLeft: 36 }"
                :maxWidth="112"
            />
            <span v-if="isQueryTabUnsaved || hasAlterEditorDataChanged" class="changes-indicator" />
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
            @click.stop.prevent="onClickClose"
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
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations, mapGetters } from 'vuex'
import QueryTab from '@wsModels/QueryTab'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import saveFile from '@wsSrc/mixins/saveFile'
import AlterEditor from '@wsModels/AlterEditor'

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
        ...mapState({ confirm_dlg: state => state.mxsWorkspace.confirm_dlg }),
        ...mapGetters({ getIsQueryTabUnsaved: 'fileSysAccess/getIsQueryTabUnsaved' }),
        tabId() {
            return this.queryTab.id
        },
        isQueryTabUnsaved() {
            return this.getIsQueryTabUnsaved(this.tabId)
        },
        initialDdlEditorData() {
            return this.$typy(AlterEditor.find(this.tabId), 'data').safeObjectOrEmpty
        },
        alterEditorStagingData() {
            return QueryTab.getters('findAlterEditorStagingData')(this.tabId)
        },
        hasAlterEditorDataChanged() {
            if (this.$typy(this.alterEditorStagingData).isEmptyObject) return false
            return !this.$helpers.lodash.isEqual(
                this.initialDdlEditorData,
                this.alterEditorStagingData
            )
        },
        queryTabsOfActiveWke() {
            return QueryTab.getters('queryTabsOfActiveWke')
        },
        isLoadingQueryResult() {
            return QueryResult.getters('findIsLoading')(this.tabId)
        },
        isQueryTabConnBusy() {
            return QueryConn.getters('isConnBusyByQueryTabId')(this.tabId)
        },
    },
    methods: {
        ...mapMutations({ SET_CONFIRM_DLG: 'mxsWorkspace/SET_CONFIRM_DLG' }),
        onClickClose() {
            if (this.isQueryTabUnsaved || this.hasAlterEditorDataChanged) {
                let confirm_msg = this.$mxs_t('confirmations.deleteQueryTab', {
                        targetId: this.queryTab.name,
                    }),
                    on_save = async () => {
                        await this.handleSaveFile(this.queryTab)
                        await this.handleDeleteTab()
                    },
                    after_cancel = async () => await this.handleDeleteTab(),
                    save_text = 'save',
                    cancel_text = 'dontSave'

                if (this.hasAlterEditorDataChanged) {
                    confirm_msg = this.$mxs_t('confirmations.deleteAlterTab', {
                        targetId: this.queryTab.name,
                    })
                    on_save = async () => await this.handleDeleteTab()
                    after_cancel = () => null
                    save_text = 'confirm'
                    cancel_text = 'cancel'
                }
                this.SET_CONFIRM_DLG({
                    ...this.confirm_dlg,
                    is_opened: true,
                    save_text,
                    cancel_text,
                    title: this.$mxs_t('deleteTab'),
                    confirm_msg,
                    on_save,
                    after_cancel,
                })
            } else this.handleDeleteTab()
        },
        async handleDeleteTab() {
            if (this.queryTabsOfActiveWke.length === 1)
                QueryTab.dispatch('refreshLastQueryTab', this.tabId)
            else await QueryTab.dispatch('handleDeleteQueryTab', this.tabId)
        },
    },
}
</script>
