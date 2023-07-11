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
        stagingColNameMap: { type: Object, required: true },
        initialData: { type: Array, required: true },
        dim: { type: Object, required: true },
        // existing parsed tables, keyed by table id
        lookupTables: { type: Object, required: true },
        connData: { type: Object, required: true },
        charsetCollationMap: { type: Object, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            selectedItems: [],
            isVertTable: false,
            isLoading: false,
            // new referenced tables that are not found in lookupTables keyed by genNewLookupTableKey
            newLookupTables: {},
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
        rows() {
            return this.stagingKeys.map(
                ({
                    id,
                    name,
                    index_cols,
                    referenced_schema_name,
                    referenced_table_name,
                    referenced_tbl_id,
                    referenced_index_cols,
                    on_update = '',
                    on_delete = '',
                }) => {
                    const key = this.genNewLookupTableKey({
                        schemaName: referenced_schema_name,
                        tableName: referenced_table_name,
                    })
                    const referencedTbl = this.getReferencedTable({ id: referenced_tbl_id, key })
                    return [
                        id,
                        name,
                        index_cols.map(({ id }) => ({
                            name: this.$typy(this.stagingColNameMap, `${id}`).safeString,
                        })),
                        this.$typy(referencedTbl, 'options.schema').safeString,
                        this.$typy(referencedTbl, 'options.name').safeString,
                        referenced_index_cols,
                        on_update,
                        on_delete,
                    ]
                }
            )
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
    watch: {
        keys: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingKeys)) this.assignData()
            },
        },
        stagingKeys: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(this.keys, v)) this.keys = v
            },
        },
    },
    async created() {
        await this.init()
    },
    methods: {
        async init() {
            this.assignData()
            if (this.unknownTargets.length)
                await this.fetchReferencedTablesData(this.unknownTargets)
        },
        assignData() {
            this.stagingKeys = this.$helpers.lodash.cloneDeep(this.keys)
        },
        /**
         * Generate a key for newLookupTables using schema name and table name.
         * Tables in newLookupTables won't have names or its col names changed,
         * so it's safe to use schema name and table name as id. By doing this,
         * the table being altered doesn't need to call transformKeys to replace
         * referenced target names with ids.
         * @param {object} param
         * @param {string} param.schemaName - schema name
         * @param {string} param.tableName - table name
         */
        genNewLookupTableKey({ schemaName, tableName }) {
            return `${schemaName}.${tableName}`
        },
        getReferencedTable({ id, key }) {
            return this.lookupTables[id] || this.newLookupTables[key] || {}
        },
        async fetchReferencedTablesData(targets) {
            this.isLoading = true
            const [, parsedTables] = await queryHelper.queryAndParseDDL({
                connId: this.connData.id,
                tableNodes: targets,
                config: this.connData.config,
            })
            this.newLookupTables = parsedTables.reduce((map, tbl) => {
                const key = this.genNewLookupTableKey({
                    schemaName: tbl.options.schema,
                    tableName: tbl.name,
                })
                map[key] = queryHelper.tableParserTransformer({
                    parsedTable: tbl,
                    charsetCollationMap: this.charsetCollationMap,
                })
                return map
            }, {})
            this.isLoading = false
        },
        deleteSelectedKeys(selectedItems) {
            const ids = selectedItems.map(([id]) => id)
            this.stagingKeys = this.stagingKeys.filter(k => !ids.includes(k.id))
        },
        //TODO: add key
        addNewKey() {},
    },
}
</script>
