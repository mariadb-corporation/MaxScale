<template>
    <v-row v-resize.quiet="setTblMaxHeight" class="fill-height">
        <v-progress-linear v-if="isLoading" indeterminate color="primary" />
        <template v-else>
            <v-col cols="12" md="6" class="fill-height pt-0">
                <div ref="tableWrapper" class="fill-height migration-tbl-wrapper">
                    <mxs-data-table
                        v-model="selectItems"
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
                <etl-script-editors
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
 * @get-activeRow: object
 * @get-staging-data: array
 */
import EtlTask from '@wsModels/EtlTask'
import EtlScriptEditors from '@wkeComps/DataMigration/EtlScriptEditors.vue'

export default {
    name: 'etl-tbl-script',
    components: { EtlScriptEditors },
    inheritAttrs: false,
    props: {
        data: { type: Array, required: true },
    },
    data() {
        return {
            tableMaxHeight: 450,
            selectItems: [],
            stagingDataMap: null,
        }
    },
    computed: {
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        defDataMap() {
            return this.data.reduce((map, obj) => {
                const id = this.$helpers.uuidv1()
                map[id] = { ...obj, id }
                return map
            }, {})
        },
        hasRowChanged() {
            if (this.activeRowId) {
                const defRow = this.$typy(this.defDataMap, `[${this.activeRowId}]`).safeObject
                return !this.$helpers.lodash.isEqual(defRow, this.activeRow)
            }
            return false
        },
        isLoading() {
            return this.$typy(this.activeEtlTask, 'meta.is_loading').safeBoolean
        },
        activeRowId() {
            return this.$typy(this.activeRow, 'id').safeString
        },
        activeRow: {
            get() {
                return this.$typy(this.selectItems, '[0]').safeObject
            },
            set(v) {
                if (v) this.selectItems = [v]
            },
        },
        tableRows() {
            if (this.stagingDataMap) return Object.values(this.stagingDataMap)
            return []
        },
        stagingData() {
            return this.$helpers.lodash.cloneDeep(this.tableRows).map(o => {
                // Remove id as it is generated for UI keying purpose
                delete o.id
                return o
            })
        },
        firstErrObj() {
            return this.tableRows.find(o => o.error)
        },
    },
    watch: {
        stagingDataMap: {
            deep: true,
            immediate: true,
            handler() {
                this.$emit('get-staging-data', this.stagingData)
            },
        },
        // Should be triggered once unless the default data has changed.
        defDataMap: {
            deep: true,
            immediate: true,
            handler(v) {
                this.stagingDataMap = this.$helpers.lodash.cloneDeep(v)
                // Select the first row as active or the first object has error field
                if (this.tableRows.length)
                    this.selectItems = [this.firstErrObj || this.tableRows[0]]
            },
        },
        activeRow: {
            deep: true,
            immediate: true,
            handler(v) {
                if (v) {
                    this.stagingDataMap[v.id] = v
                    this.$emit('get-activeRow', v)
                }
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
            this.activeRow = this.$helpers.lodash.cloneDeep(this.defDataMap[this.activeRowId])
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
