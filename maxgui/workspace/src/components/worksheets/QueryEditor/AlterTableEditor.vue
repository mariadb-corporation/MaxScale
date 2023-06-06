<template>
    <v-card class="alter-table-editor fill-height" :loading="isLoading" tile>
        <mxs-ddl-editor
            v-if="stagingData"
            ref="editor"
            v-model="stagingData"
            :dim="dim"
            :data="data"
            @on-revert="revertChanges"
            @on-apply="applyChanges"
        />
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { mapState, mapGetters, mapMutations, mapActions } from 'vuex'
import Editor from '@wsModels/Editor'
import QueryEditor from '@wsModels/QueryEditor'

export default {
    name: 'alter-table-editor',
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            stagingData: null,
        }
    },
    computed: {
        ...mapState({ exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg }),
        ...mapGetters({ isExecFailed: 'mxsWorkspace/isExecFailed' }),
        isLoading() {
            return Editor.getters('getLoadingTblCreationInfo')
        },
        data() {
            return this.$typy(Editor.getters('getTblCreationInfo'), 'data').safeObjectOrEmpty
        },
    },
    activated() {
        this.watch_isLoading()
    },
    deactivated() {
        this.$typy(this.unwatch_isLoading).safeFunction()
    },
    methods: {
        ...mapMutations({ SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG' }),
        ...mapActions({ exeStmtAction: 'mxsWorkspace/exeStmtAction' }),
        assignData() {
            this.stagingData = this.$helpers.lodash.cloneDeep(this.data)
        },
        //Watcher to work with multiple worksheets which are kept alive
        watch_isLoading() {
            this.unwatch_isLoading = this.$watch(
                'isLoading',
                v => {
                    if (!v && this.$typy(this.stagingData).isNull) this.assignData()
                },
                { deep: true, immediate: true }
            )
        },
        revertChanges() {
            this.assignData()
        },
        applyChanges() {
            this.SET_EXEC_SQL_DLG({
                ...this.exec_sql_dlg,
                is_opened: true,
                sql: this.$refs.editor.buildAlterScript(),
                on_exec: this.confirmAlter,
                on_after_cancel: this.clearAlterResult,
            })
        },
        async confirmAlter() {
            const { quotingIdentifier: quoting } = this.$helpers
            const { schema, name } = this.data.options
            await this.exeStmtAction({
                sql: this.exec_sql_dlg.sql,
                action: `Apply changes to ${quoting(schema)}.${quoting(name)}`,
            })
            if (!this.isExecFailed) {
                const data = this.$helpers.lodash.cloneDeep(this.stagingData)
                Editor.update({
                    where: QueryEditor.getters('getActiveQueryTabId'),
                    data(editor) {
                        editor.tbl_creation_info.data = data
                    },
                })
            }
        },
        clearAlterResult() {
            this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null })
        },
    },
}
</script>
<style lang="scss" scoped>
.alter-table-editor {
    box-shadow: none !important;
}
</style>
