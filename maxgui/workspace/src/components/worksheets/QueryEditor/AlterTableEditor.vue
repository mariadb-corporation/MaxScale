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
            :hintedRefTargets="hintedRefTargets"
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
import { mapState, mapActions } from 'vuex'
import Editor from '@wsModels/Editor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'

export default {
    name: 'alter-table-editor',
    props: {
        dim: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            UNPARSED_TBL_PLACEHOLDER: state => state.mxsWorkspace.config.UNPARSED_TBL_PLACEHOLDER,
        }),
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
        schema() {
            return this.$typy(this.stagingData, 'options.schema').safeString
        },
        tblName() {
            return this.$typy(this.stagingData, 'options.name').safeString
        },
        sidebarSchemaNode() {
            return SchemaSidebar.getters('dbTreeData').find(n => n.name === this.schema)
        },
        tablesInSchema() {
            const schemaGroupNode = this.$typy(this.sidebarSchemaNode, 'children').safeArray.find(
                n => n.type === this.NODE_GROUP_TYPES.TBL_G
            )
            return this.$typy(schemaGroupNode, 'children').safeArray.filter(
                n => n.name !== this.tblName
            )
        },
        hintedRefTargets() {
            return this.tablesInSchema.map(n => ({
                id: `${this.UNPARSED_TBL_PLACEHOLDER}${n.qualified_name}`,
                text: n.qualified_name,
                name: n.name,
                schema: n.parentNameData[this.NODE_TYPES.SCHEMA],
            }))
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
