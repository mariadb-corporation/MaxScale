<template>
    <div>
        <v-text-field
            v-model="filterTxt"
            name="searchSchema"
            dense
            outlined
            height="28"
            class="vuetify-input--override filter-objects"
            :placeholder="$mxs_t('filterSchemaObjects')"
        />
        <mxs-treeview
            ref="tree"
            v-model="selectedObjs"
            class="mxs-treeview--src-treeview"
            :items="src_schema_tree"
            :search="filterTxt"
            :filter="filter"
            hoverable
            dense
            open-on-click
            transition
            selectable
            :load-children="handleLoadChildren"
            return-object
        >
            <template v-slot:label="{ item: node }">
                <div class="d-flex align-center">
                    <v-icon size="18" color="deep-ocean" :class="{ 'ml-1': iconSheet(node) }">
                        {{ iconSheet(node) }}
                    </v-icon>
                    <span
                        v-mxs-highlighter="{ keyword: filterTxt, txt: node.name }"
                        class="ml-1 text-truncate d-inline-block node-name"
                    >
                        {{ node.name }}
                    </span>
                </div>
            </template>
        </mxs-treeview>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapMutations, mapState } from 'vuex'
import queryHelper from '@queryEditorSrc/store/queryHelper'

export default {
    name: 'etl-obj-select-ctr',
    data() {
        return {
            filterTxt: '',
            selectedObjs: [],
        }
    },
    computed: {
        ...mapState({
            are_conns_alive: state => state.etlMem.are_conns_alive,
            src_schema_tree: state => state.etlMem.src_schema_tree,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
        }),
        etlPrepareTables() {
            return this.selectedObjs.map(o => ({
                schema: queryHelper.getSchemaName(o),
                table: o.name,
            }))
        },
    },
    async created() {
        this.validateEtlTaskConns()
        if (this.are_conns_alive) await this.fetchSrcSchemas()
    },
    methods: {
        ...mapActions({
            validateEtlTaskConns: 'etlMem/validateEtlTaskConns',
            loadChildNodes: 'etlMem/loadChildNodes',
            fetchSrcSchemas: 'etlMem/fetchSrcSchemas',
        }),
        ...mapMutations({ SET_SRC_SCHEMA_TREE: 'etlMem/SET_SRC_SCHEMA_TREE' }),
        filter(node, search, textKey) {
            return this.$helpers.ciStrIncludes(node[textKey], search)
        },
        iconSheet(node) {
            const { SCHEMA } = this.NODE_TYPES
            const { TBL_G } = this.NODE_GROUP_TYPES
            switch (node.type) {
                case SCHEMA:
                    return '$vuetify.icons.mxs_database'
                case TBL_G:
                    return '$vuetify.icons.mxs_table'
            }
        },
        /**
         *
         * @param {Object} param.node - node to have group node
         * @param {Object} param.groupNode - group node to be added
         */
        addGroupNode({ node, groupNode }) {
            const tree = queryHelper.deepReplaceNode({
                treeData: this.src_schema_tree,
                node: { ...node, children: [groupNode] },
            })
            this.SET_SRC_SCHEMA_TREE(tree)
        },
        /**
         * For now, only TBL nodes can be migrated, so when expanding a SCHEMA node
         * the TBL_G will be automatically added and expanded to improves UX.
         * If sproc, functions or views are later supported, this won't be needed
         */
        async handleLoadChildren(node) {
            if (node.type === this.NODE_TYPES.SCHEMA) {
                const tblGroupNode = queryHelper.genNodeGroup({
                    parentNode: node,
                    type: this.NODE_GROUP_TYPES.TBL_G,
                })
                this.addGroupNode({ node, groupNode: tblGroupNode })
                await this.loadChildNodes(tblGroupNode)
                // expand TBL_G node automatically after fetching its child
                this.$refs.tree.updateOpen(tblGroupNode.id, true)
            }
        },
    },
}
</script>

<style lang="scss">
.mxs-treeview--src-treeview {
    .v-treeview-node__content {
        margin-left: 0px;
    }
}
</style>
