<template>
    <mxs-stage-ctr>
        <template v-slot:header>
            <h3
                class="mxs-stage-ctr__title mxs-color-helper text-navigation font-weight-light"
                data-test="stage-header-title"
            >
                {{ $mxs_t('selectObjsToMigrate') }}
            </h3>
        </template>
        <template v-slot:body>
            <v-container fluid class="fill-height">
                <v-row class="fill-height">
                    <v-col cols="12" md="6" class="fill-height">
                        <div class="d-flex flex-column fill-height">
                            <etl-create-mode-input :taskId="task.id" class="mb-2" />
                            <!-- TODO: Replace below treeview with SelectableSchemaTableTree component -->
                            <mxs-treeview
                                ref="tree"
                                v-model="selectedObjs"
                                class="mxs-treeview--src-treeview fill-height overflow-y-auto mxs-color-helper all-border-separator pa-2 rounded"
                                :items="srcSchemaTree"
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
                                        <mxs-schema-node-icon
                                            :node="node"
                                            :size="20"
                                            class="ml-1"
                                        />
                                        <span class="ml-1 text-truncate d-inline-block node-name">
                                            {{ node.name }}
                                        </span>
                                    </div>
                                </template>
                            </mxs-treeview>
                        </div>
                    </v-col>
                    <v-col cols="12" md="6" class="fill-height">
                        <etl-logs :task="task" class="fill-height" :class="{ 'pt-10': isLarge }" />
                    </v-col>
                </v-row>
            </v-container>
        </template>
        <template v-slot:footer>
            <div class="etl-obj-select-stage-footer d-flex flex-column justify-end">
                <p v-if="errMsg" class="v-messages__message error--text">
                    {{ errMsg }}
                </p>
                <p v-else-if="waringMsg" class="v-messages__message warning--text">
                    {{ waringMsg }}
                </p>
                <v-checkbox
                    v-if="isReplaceMode"
                    v-model="isConfirmed"
                    color="primary"
                    class="mt-0 mb-4 v-checkbox--mariadb"
                    hide-details
                    data-test="confirm-checkbox"
                >
                    <template v-slot:label>
                        <v-tooltip top transition="slide-y-transition" max-width="340">
                            <template v-slot:activator="{ on }">
                                <div class="d-flex align-center" v-on="on">
                                    <label
                                        class="v-label ml-1 mxs-color-helper text-deep-ocean confirm-label"
                                    >
                                        {{ $mxs_t('etlConfirmMigration') }}
                                    </label>
                                    <v-icon
                                        class="ml-1 material-icons-outlined pointer"
                                        size="16"
                                        color="warning"
                                    >
                                        $vuetify.icons.mxs_statusWarning
                                    </v-icon>
                                </div>
                            </template>
                            <span>{{ $mxs_t('info.etlConfirm') }}</span>
                        </v-tooltip>
                    </template>
                </v-checkbox>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="font-weight-medium px-7 text-capitalize prepare-btn"
                    rounded
                    depressed
                    :disabled="disabled"
                    data-test="prepare-btn"
                    @click="next"
                >
                    {{ $mxs_t('prepareMigrationScript') }}
                </v-btn>
            </div>
        </template>
    </mxs-stage-ctr>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import EtlCreateModeInput from '@wkeComps/DataMigration/EtlCreateModeInput.vue'
import EtlLogs from '@wkeComps/DataMigration/EtlLogs.vue'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import { NODE_TYPES, NODE_GROUP_TYPES, ETL_CREATE_MODES } from '@wsSrc/constants'

export default {
    name: 'etl-obj-select-stage',
    components: {
        EtlLogs,
        EtlCreateModeInput,
    },
    props: { task: { type: Object, required: true } },
    data() {
        return {
            selectedObjs: [],
            errMsg: '',
            waringMsg: '',
            isConfirmed: false,
        }
    },
    computed: {
        srcSchemaTree() {
            return EtlTask.getters('findSrcSchemaTree')(this.task.id)
        },
        createMode() {
            return EtlTask.getters('findCreateMode')(this.task.id)
        },
        categorizeObjs() {
            return this.selectedObjs.reduce(
                (obj, o) => {
                    const schema = schemaNodeHelper.getSchemaName(o)
                    // TBL_G nodes will be included in selectedObjs if those have no tables
                    if (o.type === NODE_GROUP_TYPES.TBL_G) obj.emptySchemas.push(schema)
                    else obj.tables.push({ schema, table: o.name })
                    return obj
                },
                { tables: [], emptySchemas: [] }
            )
        },
        tables() {
            return this.categorizeObjs.tables
        },
        isReplaceMode() {
            return this.createMode === ETL_CREATE_MODES.REPLACE
        },
        disabled() {
            if (this.tables.length) return this.isReplaceMode ? !this.isConfirmed : false
            return true
        },
        isLarge() {
            return this.$vuetify.breakpoint.width >= 960
        },
    },
    watch: {
        selectedObjs: {
            deep: true,
            handler(v) {
                if (v.length) {
                    if (this.tables.length) {
                        this.errMsg = ''
                        this.waringMsg = this.categorizeObjs.emptySchemas.length
                            ? this.$mxs_t('warnings.ignoredMigrationSchemas')
                            : ''
                    } else this.errMsg = this.$mxs_t('errors.emptyMigrationSchema')
                } else this.errMsg = this.$mxs_t('errors.emptyMigrationObj')

                EtlTaskTmp.update({ where: this.task.id, data: { migration_objs: this.tables } })
            },
        },
    },
    async created() {
        await EtlTask.dispatch('fetchSrcSchemas')
    },
    methods: {
        /**
         *
         * @param {Object} param.node - node to have group node
         * @param {Object} param.groupNode - group node to be added
         */
        addGroupNode({ node, groupNode }) {
            const tree = schemaNodeHelper.deepReplaceNode({
                treeData: this.srcSchemaTree,
                node: { ...node, children: [groupNode] },
            })
            EtlTaskTmp.update({
                where: this.task.id,
                data: { src_schema_tree: tree },
            })
        },
        /**
         * For now, only TBL nodes can be migrated, so when expanding a SCHEMA node
         * the TBL_G will be automatically added and expanded to improves UX.
         * If sproc, functions or views are later supported, this won't be needed
         */
        async handleLoadChildren(node) {
            if (node.type === NODE_TYPES.SCHEMA) {
                const tblGroupNode = schemaNodeHelper.genNodeGroup({
                    parentNode: node,
                    type: NODE_GROUP_TYPES.TBL_G,
                })
                this.addGroupNode({ node, groupNode: tblGroupNode })
                await EtlTask.dispatch('loadChildNodes', tblGroupNode)
                // expand TBL_G node automatically after fetching its child
                this.$refs.tree.updateOpen(tblGroupNode.id, true)
            }
        },

        async next() {
            EtlTask.update({
                where: this.task.id,
                data(obj) {
                    obj.active_stage_index = obj.active_stage_index + 1
                    obj.is_prepare_etl = true
                },
            })
            await EtlTask.dispatch('handleEtlCall', {
                id: this.task.id,
                tables: EtlTask.getters('findMigrationObjs')(this.task.id),
            })
        },
    },
}
</script>

<style lang="scss">
.etl-obj-select-stage-footer {
    .prepare-btn {
        width: 215px;
    }
    .confirm-label {
        font-size: 0.875rem;
    }
}
.mxs-treeview--src-treeview {
    font-size: 0.875rem;
    background-color: #fbfbfb;
    .v-treeview-node__content {
        margin-left: 0px;
    }
}
</style>
