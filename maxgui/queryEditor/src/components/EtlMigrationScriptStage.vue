<template>
    <etl-stage-ctr v-resize.quiet="setTblMaxHeight" :headerHeight="60">
        <template v-slot:header>
            <div class="etl-migration-script-stage-header">
                <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                    {{ $mxs_t('migrationScript') }}
                </h3>
                <p class="mt-4 migration-script-info mxs-color-helper text-deep-ocean">
                    {{ $mxs_t('info.migrationScriptInfo') }}
                </p>
            </div>
        </template>
        <template v-slot:body>
            <v-col cols="12" class="fill-height">
                <v-row class="fill-height pt-4">
                    <v-col cols="12" md="6" class="fill-height">
                        <div ref="tableWrapper" class="table-wrapper fill-height">
                            <mxs-data-table
                                v-model="selectItems"
                                :loading="isLoading"
                                :headers="tableHeaders"
                                :items="tableRows"
                                fixed-header
                                hide-default-footer
                                :items-per-page="-1"
                                :height="tableMaxHeight"
                                @click:row="selectItems = [$event]"
                            />
                        </div>
                    </v-col>
                    <v-col cols="12" md="6" class="fill-height">
                        <etl-transform-ctr
                            v-if="activeRow && !isLoading"
                            v-model="activeRow"
                            :hasRowChanged="hasRowChanged"
                            @on-discard="discard"
                        />
                    </v-col>
                </v-row>
            </v-col>
        </template>
        <template v-slot:footer>
            <div class="btn-ctr">
                <v-checkbox
                    v-model="isConfirmed"
                    color="primary"
                    class="mb-4 v-checkbox--mariadb"
                    hide-details
                >
                    <template v-slot:label>
                        <v-tooltip
                            top
                            transition="slide-y-transition"
                            content-class="shadow-drop mxs-color-helper white text-navigation py-1 px-4"
                        >
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
                    class="mt-auto font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    :disabled="!isConfirmed"
                    @click="next"
                >
                    {{ $mxs_t('startMigration') }}
                </v-btn>
            </div>
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
import EtlStageCtr from '@queryEditorSrc/components/EtlStageCtr.vue'
import EtlTransformCtr from '@queryEditorSrc/components/EtlTransformCtr.vue'
import { mapState, mapActions } from 'vuex'

export default {
    name: 'etl-migration-script-stage',
    components: {
        EtlStageCtr,
        EtlTransformCtr,
    },
    data() {
        return {
            isLoading: true,
            isConfirmed: false,
            activeItem: null,
            tableMaxHeight: 450,
            selectItems: [],
            activeRow: null,
            stagingScriptMap: null,
        }
    },
    computed: {
        ...mapState({
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
            are_conns_alive: state => state.etlMem.are_conns_alive,
        }),
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        asyncQueryId() {
            return this.$typy(this.activeEtlTask, 'meta.async_query_id').safeString
        },
        // Persisted data
        migrationScript() {
            return this.$typy(this.activeEtlTask, 'meta.migration_script').safeArray
        },
        generatedScriptMap() {
            return this.migrationScript.reduce((map, obj) => {
                const id = this.$helpers.uuidv1()
                map[id] = { ...obj, id }
                return map
            }, {})
        },
        // Table data
        tableHeaders() {
            return [
                { text: 'SCHEMA', value: 'schema' },
                { text: 'TABLE', value: 'table' },
            ]
        },
        tableRows() {
            if (this.stagingScriptMap) return Object.values(this.stagingScriptMap)
            return []
        },
        hasRowChanged() {
            const defRow = this.$typy(this.generatedScriptMap, `[${this.activeRow.id}]`).safeObject
            return !this.$helpers.lodash.isEqual(defRow, this.activeRow)
        },
    },
    watch: {
        // Polling result
        asyncQueryId: {
            immediate: true,
            async handler(v) {
                this.isLoading = Boolean(v)
                if (v && this.are_conns_alive)
                    await this.$helpers
                        .delay(2000)
                        .then(async () => await this.getPrepareEtlRes(this.activeEtlTask.id))
            },
        },
        tableRows: {
            deep: true,
            immediate: true,
            handler(v) {
                // Highlight the first row
                if (v.length) this.selectItems = [v[0]]
            },
        },
        selectItems: {
            deep: true,
            immediate: true,
            handler(v) {
                if (v.length) this.activeRow = v[0]
            },
        },
        generatedScriptMap: {
            deep: true,
            immediate: true,
            handler(v) {
                this.stagingScriptMap = this.$helpers.lodash.cloneDeep(v)
            },
        },
        activeRow: {
            deep: true,
            handler(v) {
                if (v) this.stagingScriptMap[v.id] = v
            },
        },
    },
    async created() {
        await this.validateActiveEtlTaskConns()
    },
    mounted() {
        this.$helpers.doubleRAF(() => this.setTblMaxHeight())
    },
    methods: {
        ...mapActions({
            getPrepareEtlRes: 'etlMem/getPrepareEtlRes',
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
        }),
        setTblMaxHeight() {
            this.tableMaxHeight =
                this.$typy(this.$refs, 'tableWrapper.clientHeight').safeNumber || 450
        },
        // Discard changes on the active row
        discard() {
            const rowId = this.activeRow.id
            this.stagingScriptMap[rowId] = this.$helpers.lodash.cloneDeep(
                this.generatedScriptMap[rowId]
            )
        },
        next() {
            // Remove id
            const migration_script = this.tableRows.map(o => {
                delete o.id
                return o
            })
            EtlTask.update({
                where: this.activeEtlTask.id,
                data(obj) {
                    obj.active_stage_index = obj.active_stage_index + 1
                    obj.meta.migration_script = migration_script
                },
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.confirm-label,
.migration-script-info {
    font-size: 14px;
}
</style>
