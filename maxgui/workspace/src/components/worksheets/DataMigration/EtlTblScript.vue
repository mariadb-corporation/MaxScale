<template>
    <v-row v-resize.quiet="setTblMaxHeight" class="fill-height">
        <v-col cols="12" md="6" class="fill-height py-0">
            <div ref="tableWrapper" class="fill-height migration-tbl-wrapper">
                <mxs-data-table
                    v-model="selectedItems"
                    :items="tableRows"
                    fixed-header
                    hide-default-footer
                    :items-per-page="-1"
                    :height="tableMaxHeight"
                    v-bind="$attrs"
                    @click:row="onRowClick"
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
                v-if="shouldShowScriptEditors"
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
import { ETL_STATUS } from '@wsSrc/constants'

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
            selectedItems: [],
            tableRows: [],
        }
    },
    computed: {
        defDataMap() {
            return this.data.reduce((map, obj) => {
                const id = this.$helpers.uuidv1()
                map[id] = { ...obj, id }
                return map
            }, {})
        },
        hasChanged() {
            if (this.activeRowId) {
                const initialData = this.$typy(this.defDataMap, `[${this.activeRowId}]`).safeObject
                return !this.$helpers.lodash.isEqual(initialData, this.activeRow)
            }
            return false
        },
        isRunning() {
            return this.task.status === ETL_STATUS.RUNNING
        },
        activeRowId() {
            return this.$typy(this.activeRow, 'id').safeString
        },
        activeRow: {
            get() {
                return this.$typy(this.selectedItems, '[0]').safeObject
            },
            set(v) {
                if (v) this.selectedItems = [v]
            },
        },
        // keep all properties except `id` as it's generated for UI purpose
        stagingData() {
            // eslint-disable-next-line no-unused-vars
            return this.tableRows.map(({ id, ...rest }) => rest)
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
        shouldShowScriptEditors() {
            return this.activeRow && !this.isRunning && this.hasScriptFields
        },
    },
    watch: {
        stagingData: {
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
                this.tableRows = Object.values(this.$helpers.lodash.cloneDeep(v))
                // Select the first row as active or the first object has error field
                if (this.tableRows.length)
                    this.selectedItems = [this.firstErrObj || this.tableRows[0]]
                this.$nextTick(() => this.setTblMaxHeight())
            },
        },
        activeRow: {
            deep: true,
            immediate: true,
            handler(v) {
                if (v) this.$emit('get-activeRow', v)
            },
        },
    },
    methods: {
        setTblMaxHeight() {
            this.tableMaxHeight =
                this.$typy(this.$refs, 'tableWrapper.clientHeight').safeNumber || 450
        },
        onRowClick(row) {
            if (!this.isRunning) this.selectedItems = [row]
        },
        // Discard changes on the active row
        discard() {
            this.activeRow = this.$helpers.lodash.cloneDeep(this.defDataMap[this.activeRowId])
        },
    },
}
</script>
<style lang="scss">
.migration-tbl-wrapper {
    tbody {
        tr {
            cursor: pointer;
        }
    }
}
</style>
