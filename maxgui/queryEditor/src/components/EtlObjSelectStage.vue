<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                {{ $mxs_t('selectObjsToMigrate') }}
            </h3>
        </template>
        <template v-slot:body>
            <v-row class="fill-height">
                <v-col cols="12" md="6" class="fill-height">
                    <div class="d-flex flex-column fill-height">
                        <v-text-field
                            v-model="filterTxt"
                            name="searchSchema"
                            dense
                            outlined
                            height="28"
                            class="vuetify-input--override filter-objects mb-2"
                            hide-details
                            :placeholder="$mxs_t('filterSchemaObjects')"
                        />
                        <!--  TODO: Add reload feat -->
                        <mxs-treeview
                            ref="tree"
                            v-model="selectedObjs"
                            class="mxs-treeview--src-treeview fill-height mxs-color-helper all-border-separator pa-3"
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
                                    <v-icon
                                        size="18"
                                        color="deep-ocean"
                                        :class="{ 'ml-1': iconSheet(node) }"
                                    >
                                        {{ iconSheet(node) }}
                                    </v-icon>
                                    <span
                                        v-mxs-highlighter="{
                                            keyword: filterTxt,
                                            txt: node.name,
                                        }"
                                        class="ml-1 text-truncate d-inline-block node-name"
                                    >
                                        {{ node.name }}
                                    </span>
                                </div>
                            </template>
                        </mxs-treeview>
                        <p v-if="errMsg" class="mt-4 v-messages__message error--text">
                            {{ errMsg }}
                        </p>
                        <p v-else-if="infoMsg" class="mt-4 v-messages__message warning--text">
                            {{ infoMsg }}
                        </p>
                    </div>
                </v-col>
                <v-col cols="12" md="6" class="fill-height">
                    <etl-logs class="fill-height" />
                </v-col>
            </v-row>
        </template>
        <template v-slot:footer>
            <v-btn
                small
                height="36"
                color="primary"
                class="font-weight-medium px-7 text-capitalize"
                rounded
                depressed
                :disabled="Boolean(errMsg)"
                @click="next"
            >
                {{ $mxs_t('prepareMigrationScript') }}
            </v-btn>
        </template>
    </etl-stage-ctr>
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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import { mapActions, mapMutations, mapState } from 'vuex'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import EtlStageCtr from '@queryEditorSrc/components/EtlStageCtr.vue'
import EtlLogs from '@queryEditorSrc/components/EtlLogs.vue'

export default {
    name: 'etl-obj-select-stage',
    components: {
        EtlStageCtr,
        EtlLogs,
    },
    data() {
        return {
            filterTxt: '',
            selectedObjs: [],
            errMsg: '',
            infoMsg: '',
        }
    },
    computed: {
        ...mapState({
            are_conns_alive: state => state.etlMem.are_conns_alive,
            src_schema_tree: state => state.etlMem.src_schema_tree,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
        }),
        parsedObjs() {
            return this.selectedObjs.reduce(
                (obj, o) => {
                    const schema = queryHelper.getSchemaName(o)
                    // TBL_G nodes will be included in selectedObjs if those have no tables
                    if (o.type === this.NODE_GROUP_TYPES.TBL_G) obj.emptySchemas.push(schema)
                    else obj.etlPrepareTables.push({ schema, table: o.name })
                    return obj
                },
                { etlPrepareTables: [], emptySchemas: [] }
            )
        },
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
    },
    watch: {
        selectedObjs: {
            deep: true,
            handler(v) {
                if (v.length) {
                    const { etlPrepareTables, emptySchemas } = this.parsedObjs
                    if (etlPrepareTables.length) {
                        this.errMsg = ''
                        if (emptySchemas.length) this.infoMsg = this.$mxs_t('info.ignoreSchemas')
                    } else this.errMsg = this.$mxs_t('errors.invalidChosenSchemas')
                } else this.errMsg = this.$mxs_t('errors.emptyMigrationObj')
            },
        },
    },
    async created() {
        await this.validateActiveEtlTaskConns()
        if (this.are_conns_alive) await this.fetchSrcSchemas()
    },
    methods: {
        ...mapMutations({ SET_MIGRATION_OBJS: 'etlMem/SET_MIGRATION_OBJS' }),
        ...mapActions({
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
            loadChildNodes: 'etlMem/loadChildNodes',
            fetchSrcSchemas: 'etlMem/fetchSrcSchemas',
            handleEtlCall: 'etlMem/handleEtlCall',
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

        async next() {
            await this.validateActiveEtlTaskConns()
            const { etlPrepareTables = {} } = this.parsedObjs
            if (!etlPrepareTables.length) this.errMsg = this.$mxs_t('errors.emptyMigrationObj')
            else this.SET_MIGRATION_OBJS(etlPrepareTables)

            if (this.are_conns_alive) {
                await this.handleEtlCall({
                    id: this.activeEtlTask.id,
                    stageIdx: this.ETL_STAGE_INDEX.MIGR_SCRIPT,
                })
                EtlTask.update({
                    where: this.activeEtlTask.id,
                    data(obj) {
                        obj.active_stage_index = obj.active_stage_index + 1
                    },
                })
            }
        },
    },
}
</script>

<style lang="scss">
.mxs-treeview--src-treeview {
    border-radius: 4px;
    background-color: #fbfbfb;
    font-size: 0.875rem;
    overflow-y: auto;
    min-height: auto;
    .v-treeview-node__content {
        margin-left: 0px;
    }
}
</style>
