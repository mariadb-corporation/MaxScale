<template>
    <v-row v-resize.quiet="setTblMaxHeight" class="fill-height">
        <v-progress-linear v-if="isLoading" indeterminate color="primary" />
        <template v-else>
            <v-col cols="12" md="6" class="fill-height pt-0">
                <div ref="tableWrapper" class="fill-height migration-tbl-wrapper">
                    <mxs-data-table
                        v-model="selectItems"
                        :headers="headers"
                        :items="tableRows"
                        fixed-header
                        hide-default-footer
                        :items-per-page="-1"
                        :height="tableMaxHeight"
                        v-bind="{ ...$attrs }"
                        @click:row="selectItems = [$event]"
                        v-on="$listeners"
                    >
                        <template
                            v-for="slot in Object.keys($scopedSlots)"
                            v-slot:[slot]="slotData"
                        >
                            <slot :name="slot" v-bind="slotData" />
                        </template>
                    </mxs-data-table>
                </div>
            </v-col>
            <v-col cols="12" md="6" class="fill-height pt-0">
                <etl-transform-ctr
                    v-if="activeRow && !isLoading"
                    v-model="activeRow"
                    :hasRowChanged="hasRowChanged"
                    @on-discard="discard"
                />
            </v-col>
        </template>
    </v-row>
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
/**
 * Emit:
 * @get-new-migration-objs: array.
 */
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import EtlTransformCtr from '@queryEditorSrc/components/EtlTransformCtr.vue'
import { mapState } from 'vuex'

export default {
    name: 'etl-migration-tbl',
    components: { EtlTransformCtr },
    inheritAttrs: false,
    props: {
        headers: { type: Array, required: true },
        stagingMigrationObjs: { type: Array, required: true }, //sync
        activeItem: { type: Object }, //sync
    },
    data() {
        return {
            tableMaxHeight: 450,
            selectItems: [],
            stagingScriptMap: null,
            scopeActiveItem: null,
        }
    },
    computed: {
        ...mapState({
            migration_objs: state => state.etlMem.migration_objs,
        }),
        activeRow: {
            get() {
                if (this.$typy(this.activeItem).isDefined) return this.activeItem
                return this.scopeActiveItem
            },
            set(v) {
                if (this.$typy(this.activeItem).isDefined) this.$emit('update:activeItem', v)
                else this.scopeActiveItem = v
            },
        },
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        generatedScriptMap() {
            return this.migration_objs.reduce((map, obj) => {
                const id = this.$helpers.uuidv1()
                map[id] = { ...obj, id }
                return map
            }, {})
        },
        tableRows() {
            if (this.stagingScriptMap) return Object.values(this.stagingScriptMap)
            return []
        },
        hasRowChanged() {
            const activeRowId = this.$typy(this.activeRow, 'id').safeString
            if (activeRowId) {
                const defRow = this.$typy(this.generatedScriptMap, `[${activeRowId}]`).safeObject
                return !this.$helpers.lodash.isEqual(defRow, this.activeRow)
            }
            return false
        },
        isLoading() {
            return this.$typy(this.activeEtlTask, 'meta.is_loading').safeBoolean
        },
    },
    watch: {
        tableRows: {
            deep: true,
            immediate: true,
            handler(v) {
                // Highlight the first row
                if (v.length) this.selectItems = [v[0]]
                // Remove id as id is generated for UI keying purpose
                const stagingMigrationObjs = this.$helpers.lodash.cloneDeep(v).map(o => {
                    delete o.id
                    return o
                })
                this.$emit('update:stagingMigrationObjs', stagingMigrationObjs)
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
    mounted() {
        this.$helpers.doubleRAF(() => this.setTblMaxHeight())
    },
    methods: {
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
    },
}
</script>
<style lang="scss" scoped>
.confirm-label,
.migration-script-info {
    font-size: 14px;
}
.migration-tbl-wrapper {
    tbody {
        tr {
            cursor: pointer;
        }
    }
}
</style>

<style lang="scss">
.migration-tbl-wrapper {
    tbody {
        tr {
            cursor: pointer;
        }
    }
}
</style>
