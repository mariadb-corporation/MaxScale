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
            @click.stop.prevent="onClickDelete"
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 Emits:
 - delete(id:string)
*/
import { mapState, mapMutations, mapGetters } from 'vuex'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import QueryConn from '@wsModels/QueryConn'
import AlterEditor from '@wsModels/AlterEditor'
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
        ...mapState({ confirm_dlg: state => state.mxsWorkspace.confirm_dlg }),
        ...mapGetters({ getIsQueryTabUnsaved: 'fileSysAccess/getIsQueryTabUnsaved' }),
        tabId() {
            return this.queryTab.id
        },
        queryTabTmp() {
            return QueryTabTmp.find(this.tabId) || {}
        },
        isQueryTabUnsaved() {
            return this.getIsQueryTabUnsaved(this.tabId)
        },
        initialAlterEditorData() {
            return this.$typy(AlterEditor.find(this.tabId), 'data').safeObjectOrEmpty
        },
        alterEditorStagingData() {
            return this.$typy(this.queryTabTmp, 'alter_editor_staging_data').safeObjectOrEmpty
        },
        hasAlterEditorDataChanged() {
            if (this.$typy(this.alterEditorStagingData).isEmptyObject) return false
            return !this.$helpers.lodash.isEqual(
                this.initialAlterEditorData,
                this.alterEditorStagingData
            )
        },
        isLoadingQueryResult() {
            return this.$typy(this.queryTabTmp, 'query_results.is_loading').safeBoolean
        },
        isQueryTabConnBusy() {
            return this.$typy(QueryConn.getters('findQueryTabConn')(this.tabId), 'is_busy')
                .safeBoolean
        },
    },
    methods: {
        ...mapMutations({ SET_CONFIRM_DLG: 'mxsWorkspace/SET_CONFIRM_DLG' }),
        onClickDelete() {
            if (this.isQueryTabUnsaved || this.hasAlterEditorDataChanged) {
                let confirm_msg = this.$mxs_t('confirmations.deleteQueryTab', {
                        targetId: this.queryTab.name,
                    }),
                    on_save = async () => {
                        await this.handleSaveFile(this.queryTab)
                        this.$emit('delete', this.tabId)
                    },
                    after_cancel = () => this.$emit('delete', this.tabId),
                    save_text = 'save',
                    cancel_text = 'dontSave'

                if (this.hasAlterEditorDataChanged) {
                    confirm_msg = this.$mxs_t('confirmations.deleteAlterTab', {
                        targetId: this.queryTab.name,
                    })
                    on_save = () => this.$emit('delete', this.tabId)
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
            } else this.$emit('delete', this.tabId)
        },
    },
}
</script>
