<template>
    <v-row v-resize.quiet="setTblMaxHeight" class="fill-height">
        <v-col cols="12" md="6" class="fill-height py-0">
            <div ref="tableWrapper" class="fill-height migration-tbl-wrapper">
                <mxs-data-table
                    :value="isRunning ? [] : selectItems"
                    :items="tableRows"
                    fixed-header
                    hide-default-footer
                    :items-per-page="-1"
                    :height="tableMaxHeight"
                    v-bind="{ ...$attrs }"
                    @input="isRunning ? null : (selectItems = $event)"
                    @click:row="isRunning ? null : (selectItems = [$event])"
                    v-on="$listeners"
                >
                    <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
                        <slot :name="slot" v-bind="slotData" />
                    </template>
                </mxs-data-table>
            </div>
        </v-col>
        <v-col cols="12" md="6" class="fill-height py-0">
            <etl-script-editors
                v-if="activeRow && !isRunning && hasScriptFields"
                v-model="activeRow"
                :hasChanged="hasChanged"
                @on-discard="discard"
            />
        </v-col>
    </v-row>
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
/**
 * Emit:
 * @get-activeRow: object
 * @get-staging-data: array
 */
import EtlScriptEditors from '@wkeComps/DataMigration/EtlScriptEditors.vue'
import { mapState } from 'vuex'

export default {
    name: 'etl-tbl-script',
    components: { EtlScriptEditors },
    inheritAttrs: false,
    props: {
        task: { type: Object, required: true },
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
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
        }),
        defDataMap() {
            return this.data.reduce((map, obj) => {
                const id = this.$helpers.uuidv1()
                map[id] = { ...obj, id }
                return map
            }, {})
        },
        hasChanged() {
            if (this.activeRowId) {
                const defRow = this.$typy(this.defDataMap, `[${this.activeRowId}]`).safeObject
                return !this.$helpers.lodash.isEqual(defRow, this.activeRow)
            }
            return false
        },
        isRunning() {
            return this.task.status === this.ETL_STATUS.RUNNING
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
        hasScriptFields() {
            return (
                this.$typy(this.activeRow, 'insert').isDefined &&
                this.$typy(this.activeRow, 'create').isDefined &&
                this.$typy(this.activeRow, 'select').isDefined
            )
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
                this.$helpers.doubleRAF(() => this.setTblMaxHeight())
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
