<template>
    <v-card class="alter-table-editor fill-height" :loading="isLoading" tile>
        <mxs-ddl-editor
            v-if="stagingData"
            v-model="stagingData"
            :dim="dim"
            :initialData="initialData"
            :connData="{ id: activeQueryTabConnId, config: activeRequestConfig }"
            :onExecute="onExecute"
            :lookupTables="{ [stagingData.id]: stagingData }"
            :activeSpec.sync="activeSpecTab"
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
import { mapActions } from 'vuex'
import Editor from '@wsModels/Editor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'

export default {
    name: 'alter-table-editor',
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            stagingData: null,
            activeSpecTab: '',
        }
    },
    computed: {
        isLoading() {
            return Editor.getters('isLoadingTblCreationInfo')
        },
        initialData() {
            return this.$typy(Editor.getters('tblCreationInfo'), 'data').safeObjectOrEmpty
        },
        activeQueryTabConnId() {
            return this.$typy(QueryConn.getters('activeQueryTabConn'), 'id').safeString
        },
        activeRequestConfig() {
            return Worksheet.getters('activeRequestConfig')
        },
        activeQueryTabId() {
            return QueryEditor.getters('activeQueryTabId')
        },
        activeSpec() {
            return Editor.getters('activeSpec')
        },
    },
    watch: {
        activeSpecTab(v) {
            Editor.update({
                where: this.activeQueryTabId,
                data(editor) {
                    editor.tbl_creation_info.active_spec = v
                },
            })
        },
    },
    activated() {
        this.watch_isLoading()
        this.watch_activeSpec()
    },
    deactivated() {
        this.$typy(this.unwatch_isLoading).safeFunction()
        this.$typy(this.unwatch_activeSpec).safeFunction()
    },
    methods: {
        ...mapActions({ exeDdlScript: 'mxsWorkspace/exeDdlScript' }),
        assignData() {
            this.stagingData = this.$helpers.lodash.cloneDeep(this.initialData)
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
        watch_activeSpec() {
            this.unwatch_activeSpec = this.$watch('activeSpec', v => (this.activeSpecTab = v), {
                immediate: true,
            })
        },
        async onExecute() {
            await this.exeDdlScript({
                connId: this.activeQueryTabConnId,
                schema: this.initialData.options.schema,
                name: this.initialData.options.name,
                successCb: () => {
                    const data = this.$helpers.lodash.cloneDeep(this.stagingData)
                    Editor.update({
                        where: this.activeQueryTabId,
                        data(editor) {
                            editor.tbl_creation_info.data = data
                        },
                    })
                },
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.alter-table-editor {
    box-shadow: none !important;
}
</style>
