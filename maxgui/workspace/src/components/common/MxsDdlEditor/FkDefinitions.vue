<template>
    <div class="fill-height">
        <v-progress-linear v-if="isLoading" indeterminate />
        <template v-else>
            <tbl-toolbar
                :selectedItems="selectedItems"
                :isVertTable.sync="isVertTable"
                @get-computed-height="headerHeight = $event"
                @on-delete-selected-items="deleteSelectedKeys"
                @on-add="addNewKey"
            />
            <mxs-virtual-scroll-tbl
                :headers="headers"
                :rows="rows"
                :itemHeight="32"
                :maxHeight="dim.height - headerHeight"
                :boundingWidth="dim.width"
                showSelect
                :isVertTable="isVertTable"
                v-on="$listeners"
                @selected-rows="selectedItems = $event"
            >
                <!-- TODO: Render inputs -->
            </mxs-virtual-scroll-tbl>
        </template>
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
import { mapState } from 'vuex'
import TblToolbar from '@wsSrc/components/common/MxsDdlEditor/TblToolbar.vue'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'fk-definitions',
    components: { TblToolbar },
    props: {
        value: { type: Array, required: true },
        stagingCols: { type: Array, required: true },
        stagingColNameMap: { type: Object, required: true },
        initialData: { type: Array, required: true },
        dim: { type: Object, required: true },
        // existing parsed tables
        lookupTables: { type: Object, required: true },
        connData: { type: Object, required: true },
        charsetCollationMap: { type: Object, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            selectedItems: [],
            isVertTable: false,
            isLoading: true,
            // existing lookupTables and new referenced tables that are not found in lookupTables
            allLookupTables: {},
            rows: [],
            stagingKeys: {},
        }
    },
    computed: {
        ...mapState({
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
        }),
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
        unknownTargets() {
            const { quotingIdentifier: quote } = this.$helpers
            const targets = this.keys.reduce(
                (res, { referenced_table_name, referenced_schema_name }) => {
                    if (referenced_table_name) {
                        res.push({
                            qualified_name: `${quote(referenced_schema_name)}.${quote(
                                referenced_table_name
                            )}`,
                            parentNameData: {
                                [this.NODE_TYPES.SCHEMA]: referenced_schema_name,
                            },
                        })
                    }
                    return res
                },
                []
            )
            return this.$helpers.lodash.uniqBy(targets, 'qualified_name')
        },
    },
    async created() {
        this.allLookupTables = this.lookupTables
        this.stagingKeys = this.$helpers.lodash.cloneDeep(this.keys)
        await this.fetchReferencedTablesData(this.unknownTargets)
        this.genRows()
        this.isLoading = false
    },
    methods: {
        addNameToCols({ cols, map }) {
            return cols.map(({ id }) => ({ id, name: map[id] }))
        },
        async fetchReferencedTablesData(targets) {
            if (targets.length) {
                const [, parsedTables] = await queryHelper.queryAndParseDDL({
                    connId: this.connData.id,
                    tableNodes: targets,
                    config: this.connData.config,
                })
                this.stagingKeys = queryHelper.transformKeys({
                    keys: this.stagingKeys,
                    cols: this.stagingCols,
                    parsedTables,
                })
                const newLookupTables = parsedTables.reduce((map, tbl) => {
                    map[tbl.id] = queryHelper.tableParserTransformer({
                        parsedTable: tbl,
                        charsetCollationMap: this.charsetCollationMap,
                    })
                    return map
                }, {})
                this.allLookupTables = this.$helpers.lodash.merge(
                    this.allLookupTables,
                    newLookupTables
                )
            }
        },
        genRows() {
            this.rows = this.stagingKeys.map(
                ({
                    id,
                    name,
                    index_cols,
                    referenced_tbl_id,
                    referenced_index_cols,
                    match_option = '',
                    on_update = '',
                    on_delete = '',
                }) => {
                    const referencedTbl = this.allLookupTables[referenced_tbl_id]
                    return [
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
                }
            )
        },
        deleteSelectedKeys(selectedItems) {
            const { xorWith, isEqual } = this.$helpers.lodash
            this.rows = xorWith(this.rows, selectedItems, isEqual)
            //TODO: drop keys
        },
        //TODO: add key
        addNewKey() {},
    },
}
</script>
