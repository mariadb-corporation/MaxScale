<template>
    <v-card class="alter-table-editor fill-height" :loading="isFetchingData" tile>
        <keep-alive>
            <mxs-ddl-editor
                v-if="!$typy(stagingData).isEmptyObject"
                :key="queryTab.id"
                v-model="stagingData"
                :dim="dim"
                :initialData="initialData"
                :connData="{ id: connId, config: activeRequestConfig }"
                :onExecute="onExecute"
                :lookupTables="{ [stagingData.id]: stagingData }"
                :hintedRefTargets="hintedRefTargets"
                :activeSpec.sync="activeSpec"
            />
        </keep-alive>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import AlterEditor from '@wsModels/AlterEditor'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import QueryTabTmp from '@wsModels/QueryTabTmp'

export default {
    name: 'alter-table-editor',
    props: {
        dim: { type: Object, required: true },
        queryEditorTmp: { type: Object, required: true },
        queryTab: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            UNPARSED_TBL_PLACEHOLDER: state => state.mxsWorkspace.config.UNPARSED_TBL_PLACEHOLDER,
        }),
        queryTabTmp() {
            return QueryTabTmp.find(this.queryTab.id) || {}
        },
        alterEditor() {
            return AlterEditor.find(this.queryTab.id) || {}
        },
        queryTabConn() {
            return QueryConn.getters('findQueryTabConn')(this.queryTab.id)
        },
        isFetchingData() {
            return this.$typy(this.alterEditor, 'is_fetching').safeBoolean
        },
        initialData() {
            return this.$typy(this.alterEditor, 'data').safeObjectOrEmpty
        },
        connId() {
            return this.$typy(this.queryTabConn, 'id').safeString
        },
        activeRequestConfig() {
            return Worksheet.getters('activeRequestConfig')
        },
        alterEditorStagingData() {
            return this.$typy(this.queryTabTmp, 'alter_editor_staging_data').safeObjectOrEmpty
        },
        stagingData: {
            get() {
                return this.alterEditorStagingData
            },
            set(data) {
                QueryTabTmp.update({
                    where: this.queryTab.id,
                    data: { alter_editor_staging_data: data },
                })
            },
        },
        activeSpec: {
            get() {
                return this.$typy(this.alterEditor, 'active_spec').safeString
            },
            set(v) {
                AlterEditor.update({
                    where: this.queryTab.id,
                    data: { active_spec: v },
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
            return this.$typy(this.queryEditorTmp, 'db_tree').safeArray.find(
                n => n.name === this.schema
            )
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
    watch: {
        isFetchingData: {
            immediate: true,
            handler(v) {
                if (!v && this.$typy(this.alterEditorStagingData).isEmptyObject) {
                    QueryTabTmp.update({
                        where: this.queryTab.id,
                        data: {
                            alter_editor_staging_data: this.$helpers.lodash.cloneDeep(
                                this.initialData
                            ),
                        },
                    })
                }
            },
        },
    },
    methods: {
        ...mapActions({ exeDdlScript: 'mxsWorkspace/exeDdlScript' }),
        async onExecute() {
            await this.exeDdlScript({
                connId: this.connId,
                schema: this.initialData.options.schema,
                name: this.initialData.options.name,
                successCb: () => {
                    const data = this.$helpers.lodash.cloneDeep(this.stagingData)
                    AlterEditor.update({
                        where: this.queryTab.id,
                        data: { data },
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
