<template>
    <v-card class="alter-table-editor fill-height" :loading="isLoading" tile>
        <mxs-ddl-editor
            v-if="!$typy(stagingData).isEmptyObject"
            v-model="stagingData"
            :dim="dim"
            :initialData="initialData"
            :connData="{ id: activeQueryTabConnId, config: activeRequestConfig }"
            :onExecute="onExecute"
            :lookupTables="{ [stagingData.id]: stagingData }"
            :activeSpec.sync="activeSpec"
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
import QueryTab from '@wsSrc/store/orm/models/QueryTab'
import QueryTabTmp from '@wsSrc/store/orm/models/QueryTabTmp'

export default {
    name: 'alter-table-editor',
    props: {
        dim: { type: Object, required: true },
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
        stagingAlterData() {
            return QueryTab.getters('stagingAlterData')
        },
        stagingData: {
            get() {
                return this.stagingAlterData
            },
            set(data) {
                QueryTabTmp.update({
                    where: this.activeQueryTabId,
                    data: { staging_alter_data: data },
                })
            },
        },
        activeSpec: {
            get() {
                return Editor.getters('activeSpec')
            },
            set(v) {
                Editor.update({
                    where: this.activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.active_spec = v
                    },
                })
            },
        },
    },
    activated() {
        this.watch_isLoading()
    },
    deactivated() {
        this.$typy(this.unwatch_isLoading).safeFunction()
    },
    beforeDestroy() {
        this.$typy(this.unwatch_isLoading).safeFunction()
    },
    methods: {
        ...mapActions({ exeDdlScript: 'mxsWorkspace/exeDdlScript' }),
        watch_isLoading() {
            this.unwatch_isLoading = this.$watch(
                'isLoading',
                v => {
                    if (!v && this.$typy(this.stagingAlterData).isEmptyObject) {
                        QueryTabTmp.update({
                            where: this.activeQueryTabId,
                            data: {
                                staging_alter_data: this.$helpers.lodash.cloneDeep(
                                    this.initialData
                                ),
                            },
                        })
                    }
                },
                { deep: true, immediate: true }
            )
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
