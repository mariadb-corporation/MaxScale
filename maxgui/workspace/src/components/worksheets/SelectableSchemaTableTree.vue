<template>
    <mxs-treeview
        v-model="selectedObjs"
        class="mxs-treeview--objects-select overflow-y-auto mxs-color-helper all-border-separator pa-2 rounded"
        :items="items"
        hoverable
        dense
        open-on-click
        transition
        selectable
        return-object
        :load-children="loadTables"
    >
        <template v-slot:label="{ item: node }">
            <div class="d-flex align-center">
                <v-icon size="18" color="blue-azure" :class="{ 'ml-1': iconSheet(node) }">
                    {{ iconSheet(node) }}
                </v-icon>
                <span class="ml-1 text-truncate d-inline-block node-name">
                    {{ node.name }}
                </span>
            </div>
        </template>
    </mxs-treeview>
</template>
<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// ========== Component for selecting schema and table objects ==========
import { mapState, mapMutations } from 'vuex'
import Worksheet from '@wsSrc/store/orm/models/Worksheet'
import SchemaSidebar from '@wsSrc/store/orm/models/SchemaSidebar'
import queries from '@wsSrc/api/queries'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'selectable-schema-table-tree',
    props: {
        value: { type: Array, required: true },
        connId: { type: String, required: true },
        preselectedSchemas: { type: Array, default: () => [] },
        shouldRefresh: { type: Boolean, required: true },
    },
    data() {
        return {
            items: [],
        }
    },
    computed: {
        ...mapState({
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
        }),
        selectedObjs: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        activeRequestConfig() {
            return Worksheet.getters('getActiveRequestConfig')
        },
    },
    watch: {
        shouldRefresh: {
            immediate: true,
            async handler(v) {
                if (v) await this.fetchSchemas()
            },
        },
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        iconSheet(node) {
            if (node.type === this.NODE_TYPES.SCHEMA) return '$vuetify.icons.mxs_database'
        },
        async fetchSchemas() {
            const [e, res] = await this.$helpers.to(
                queries.post({
                    id: this.connId,
                    body: { sql: SchemaSidebar.getters('getDbSql') },
                    config: this.activeRequestConfig,
                })
            )
            let errMsg
            if (e) errMsg = this.$mxs_t('errors.retrieveSchemaObj')
            else {
                const result = this.$typy(res, 'data.data.attributes.results[0]').safeObject
                if (this.$typy(result, 'errno').isDefined)
                    errMsg += `\n${this.$helpers.queryResErrToStr(result)}`
                else {
                    const { nodes } = queryHelper.genNodeData({
                        queryResult: result,
                        nodeAttrs: { isEmptyChildren: true },
                    })
                    this.items = nodes
                    this.selectedObjs = nodes.filter(n => this.preselectedSchemas.includes(n.id))
                }
            }

            if (errMsg) this.SET_SNACK_BAR_MESSAGE({ text: [errMsg], type: 'error' })
        },
        async loadTables(node) {
            const { nodes } = await queryHelper.getChildNodeData({
                connId: this.connId,
                nodeGroup: queryHelper.genNodeGroup({
                    parentNode: node,
                    type: this.NODE_GROUP_TYPES.TBL_G,
                }),
                nodeAttrs: {
                    onlyName: true,
                    isLeaf: true,
                    activatable: false,
                },
                config: this.activeRequestConfig,
            })
            node.children = nodes
        },
    },
}
</script>

<style lang="scss" scoped>
.mxs-treeview--objects-select {
    max-height: 500px;
    font-size: 0.875rem;
    background-color: #fbfbfb;
}
</style>
