<template>
    <div class="fill-height">
        <!-- TODO: add toolbar to add/drop key -->
        <v-progress-linear v-if="isLoading" indeterminate />
        <mxs-virtual-scroll-tbl
            v-else
            :headers="headers"
            :rows="rows"
            :itemHeight="32"
            :maxHeight="dim.height"
            :boundingWidth="dim.width"
            showSelect
            :isVertTable="isVertTable"
            v-on="$listeners"
            @selected-rows="selectedItems = $event"
        >
            <!-- TODO: Render inputs -->
        </mxs-virtual-scroll-tbl>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'fk-definitions',
    props: {
        value: { type: Array, required: true },
        stagingColNameMap: { type: Object, required: true },
        initialData: { type: Array, required: true },
        dim: { type: Object, required: true },
        schema: { type: String, required: true },
        // existing parsed tables
        lookupTables: { type: Object, required: true },
    },
    data() {
        return {
            selectedItems: [],
            isVertTable: false,
            isLoading: true,
            // existing lookupTables and new referenced tables that are not found in lookupTables
            allLookupTables: {},
            rows: [],
        }
    },
    computed: {
        headers() {
            let header = { sortable: false }
            return [
                { text: 'id', hidden: true },
                { text: this.$mxs_t('name'), ...header },
                { text: this.$mxs_t('referencingCols'), minWidth: 146, ...header },
                { text: this.$mxs_t('referencedSchema'), width: 150, minWidth: 136, ...header },
                { text: this.$mxs_t('referencedTbl'), width: 150, minWidth: 124, ...header },
                { text: this.$mxs_t('referencedCols'), minWidth: 142, ...header },
                { text: this.$mxs_t('match'), width: 90, resizable: false, ...header },
                { text: this.$mxs_t('onUpdate'), width: 120, minWidth: 86, ...header },
                { text: this.$mxs_t('onDelete'), width: 120, minWidth: 86, ...header },
            ]
        },
        keys: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        /**
         * nested hash. e.g
         * { "tbl_123": { "col_123": "id", "col_234": "name" } }
         */
        allTableColNameMap() {
            return Object.keys(this.allLookupTables).reduce((res, key) => {
                res[key] = queryHelper.createColNameMap(
                    this.$typy(this.allLookupTables[key], 'definitions.cols').safeArray
                )
                return res
            }, {})
        },
    },
    async created() {
        await this.fetchReferencedTablesData()
        this.isLoading = false
    },
    methods: {
        addNameToCols({ cols, map }) {
            return cols.map(({ id }) => ({ id, name: map[id] }))
        },
        async fetchReferencedTablesData() {
            const quote = this.$helpers.quotingIdentifier
            const targets = this.keys.reduce(
                (res, { referenced_table_name, referenced_schema_name }) => {
                    if (referenced_table_name) {
                        /**
                         * If referenced_schema_name is not defined, the target table is in the
                         * same schema as the one being altered/created
                         */
                        const schema = quote(
                            referenced_schema_name ? referenced_schema_name : this.schema
                        )
                        const table = quote(referenced_table_name)
                        res.push(`${schema}.${table}`)
                    }
                    return res
                },
                []
            )
            let newLookupTables = {}
            if (targets.length) {
                //TODO: Fetch and parsed targets tables
            }
            this.allLookupTables = this.$helpers.lodash.merge(this.lookupTables, newLookupTables)
            this.rows = this.keys.map(
                ({
                    id,
                    name,
                    index_cols,
                    referenced_tbl_id,
                    referenced_index_cols,
                    match_option,
                    on_update,
                    on_delete,
                }) => {
                    const referencedTbl = this.allLookupTables[referenced_tbl_id]
                    let row = [
                        id,
                        name,
                        this.addNameToCols({ cols: index_cols, map: this.stagingColNameMap }),
                        referencedTbl.options.schema,
                        referencedTbl.options.name,
                        this.addNameToCols({
                            cols: referenced_index_cols,
                            map: this.allTableColNameMap[referencedTbl.id],
                        }),
                        match_option,
                        on_update,
                        on_delete,
                    ]
                    return row
                }
            )
        },
    },
}
</script>
