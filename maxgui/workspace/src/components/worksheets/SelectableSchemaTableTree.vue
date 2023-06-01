<template>
    <div class="selectable-schema-table-tree">
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
        <div class="input-message-ctr mt-3">
            <p v-if="inputMsg" :class="`v-messages__message ${inputMsg.type}--text`">
                {{ inputMsg.text }}
            </p>
        </div>
    </div>
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
/*
 * Emits
 * $emit('selected-tables', object[])
 */
import { mapState } from 'vuex'
import Worksheet from '@wsSrc/store/orm/models/Worksheet'
import SchemaSidebar from '@wsSrc/store/orm/models/SchemaSidebar'
import queries from '@wsSrc/api/queries'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'selectable-schema-table-tree',
    props: {
        connId: { type: String, required: true },
        preselectedSchemas: { type: Array, default: () => [] },
        triggerDataFetch: { type: Boolean, required: true },
    },
    data() {
        return {
            selectedObjs: [],
            items: [],
            inputMsg: null,
        }
    },
    computed: {
        ...mapState({
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
        }),
        activeRequestConfig() {
            return Worksheet.getters('getActiveRequestConfig')
        },
        parsedObjs() {
            return this.selectedObjs.reduce(
                (obj, o) => {
                    // SCHEMA nodes will be included in selectedObjs even though those have no tables
                    if (o.type === this.NODE_TYPES.SCHEMA)
                        obj.emptySchemas.push(this.minimizeNode(o))
                    else obj.tables.push(this.minimizeNode(o))
                    return obj
                },
                { tables: [], emptySchemas: [] }
            )
        },
        tables() {
            return this.parsedObjs.tables
        },
        emptySchemas() {
            return this.parsedObjs.emptySchemas
        },
    },
    watch: {
        triggerDataFetch: {
            immediate: true,
            async handler(v) {
                if (v) {
                    this.items = []
                    await this.fetchSchemas()
                    await this.handlePreselectedSchemas()
                }
            },
        },
        selectedObjs: {
            deep: true,
            handler(v) {
                if (v.length) {
                    if (!this.tables.length)
                        this.inputMsg = {
                            type: 'error',
                            text: this.$mxs_t('errors.emptyVisualizeSchema'),
                        }
                    else if (this.emptySchemas.length)
                        this.inputMsg = {
                            type: 'warning',
                            text: this.$mxs_t('warnings.ignoredVisualizeSchemas'),
                        }
                    else this.inputMsg = null
                } else this.inputMsg = null
            },
        },
        tables: {
            deep: true,
            immediate: true,
            handler(v) {
                this.$emit('selected-tables', v)
            },
        },
    },
    methods: {
        iconSheet(node) {
            if (node.type === this.NODE_TYPES.SCHEMA) return '$vuetify.icons.mxs_database'
        },
        minimizeNode(node) {
            return {
                name: node.name,
                qualified_name: node.qualified_name,
                schema: node.parentNameData[this.NODE_TYPES.SCHEMA],
            }
        },
        async fetchSchemas() {
            const [e, res] = await this.$helpers.to(
                queries.post({
                    id: this.connId,
                    body: { sql: SchemaSidebar.getters('getDbSql') },
                    config: this.activeRequestConfig,
                })
            )

            if (e) this.errMsg = this.$mxs_t('errors.retrieveSchemaObj')
            else {
                const result = this.$typy(res, 'data.data.attributes.results[0]').safeObject
                if (this.$typy(result, 'errno').isDefined)
                    this.errMsg += `\n${this.$helpers.queryResErrToStr(result)}`
                else {
                    const { nodes } = queryHelper.genNodeData({
                        queryResult: result,
                        nodeAttrs: { isEmptyChildren: true },
                    })
                    this.items = nodes
                }
            }
        },
        async handlePreselectedSchemas() {
            const nodes = this.items.filter(n => this.preselectedSchemas.includes(n.qualified_name))
            let selectedObjs = []
            for (const node of nodes) {
                await this.loadTables(node)
                /**
                 * If preselected schemas have no tables, add the schema node to selectedObjs
                 * so the input validation message can be shown.
                 */
                if (node.children.length) selectedObjs = [...selectedObjs, ...node.children]
                else selectedObjs = [...selectedObjs, node]
            }
            this.selectedObjs = selectedObjs
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
                },
                config: this.activeRequestConfig,
            })
            node.children = nodes
        },
    },
}
</script>

<style lang="scss" scoped>
.selectable-schema-table-tree {
    .input-message-ctr {
        min-height: 24px;
    }
    .mxs-treeview--objects-select {
        max-height: 500px;
        font-size: 0.875rem;
        background-color: #fbfbfb;
    }
}
</style>
