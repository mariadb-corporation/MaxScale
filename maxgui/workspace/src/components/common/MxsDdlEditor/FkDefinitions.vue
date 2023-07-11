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
                <template
                    v-for="(h, key) in headers"
                    v-slot:[`header-${h.text}`]="{ data: { header } }"
                >
                    <span :key="key">{{ $mxs_t(header.text) }} </span>
                </template>
                <template
                    v-for="h in headers"
                    v-slot:[h.text]="{ data: { cell, rowIdx, rowData } }"
                >
                    <fk-definition-col
                        :key="h.text"
                        :data="{
                            field: h.text,
                            value: cell,
                            rowIdx,
                        }"
                        :height="28"
                        :referencingColOptions="referencingColOptions"
                        :referencedTargets="referencedTargets"
                        :referencedColOptions="
                            getColOptions({
                                map: allTableColNameMap,
                                tableId: $typy(fkReferencedTableMap[rowData[0]], 'id').safeString,
                            })
                        "
                        @on-input="onChangeInput"
                    />
                </template>
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
import FkDefinitionCol from '@wsSrc/components/common/MxsDdlEditor/FkDefinitionCol.vue'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'fk-definitions',
    components: { TblToolbar, FkDefinitionCol },
    props: {
        value: { type: Array, required: true },
        tableId: { type: String, required: true },
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
            // new referenced tables keyed by genTableKey
            newLookupTables: {},
            stagingKeys: {},
        }
    },
    computed: {
        ...mapState({
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            FK_EDITOR_ATTRS: state => state.mxsWorkspace.config.FK_EDITOR_ATTRS,
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTR_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTR_IDX_MAP,
        }),
        headers() {
            let header = { sortable: false }
            const {
                ID,
                NAME,
                REFERENCING_COL,
                REFERENCED_TARGET,
                REFERENCED_COL,
                ON_UPDATE,
                ON_DELETE,
            } = this.FK_EDITOR_ATTRS
            return [
                { text: ID, hidden: true },
                { text: NAME, ...header },
                { text: REFERENCING_COL, minWidth: 146, ...header },
                { text: REFERENCED_TARGET, minWidth: 146, ...header },
                { text: REFERENCED_COL, minWidth: 142, ...header },
                { text: ON_UPDATE, width: 166, minWidth: 86, ...header },
                { text: ON_DELETE, width: 166, minWidth: 86, ...header },
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
        // mapped by FK id
        fkReferencedTableMap() {
            return this.stagingKeys.reduce(
                (
                    map,
                    {
                        id,
                        referenced_schema_name = '',
                        referenced_table_name = '',
                        referenced_tbl_id,
                    }
                ) => {
                    map[id] = this.getReferencedTable({
                        id: referenced_tbl_id,
                        key: this.genTableKey({
                            schemaName: referenced_schema_name,
                            tableName: referenced_table_name,
                        }),
                    })
                    return map
                },
                {}
            )
        },
        idxOfColId() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.ID]
        },
        idxOfColName() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.NAME]
        },
        rows() {
            return this.stagingKeys.map(
                ({ id, name, index_cols, referenced_index_cols, on_update, on_delete }) => {
                    const referencedTbl = this.fkReferencedTableMap[id]
                    const referencedColNames = referenced_index_cols.map(c => {
                        if (c.name) return c.name
                        else return this.allTableColNameMap[referencedTbl.id][c.id]
                    })
                    const referencedColIds = referencedTbl.definitions.cols.reduce((res, c) => {
                        if (referencedColNames.includes(c[this.idxOfColName]))
                            res.push(c[this.idxOfColId])
                        return res
                    }, [])
                    return [
                        id,
                        name,
                        index_cols.map(c => c.id),
                        referencedTbl.id,
                        referencedColIds,
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
        // Keyed by table id
        allTableMap() {
            const { lodash } = this.$helpers
            // convert newLookupTables to key by id
            const newLookupTables = lodash.keyBy(Object.values(this.newLookupTables), 'id')
            return lodash.merge(this.lookupTables, newLookupTables)
        },
        /**
         * @returns nested hash. e.g. { "tbl_123": { "col_123": "id", "col_234": "name" } }
         */
        allTableColNameMap() {
            return Object.keys(this.allTableMap).reduce((res, id) => {
                res[id] = queryHelper.createColNameMap(
                    this.$typy(this.allTableMap[id], 'definitions.cols').safeArray
                )
                return res
            }, {})
        },
        referencingColOptions() {
            return this.getColOptions({ map: this.allTableColNameMap, tableId: this.tableId })
        },
        referencedTargets() {
            return this.$helpers.lodash.map(this.allTableMap, (tbl, id) => ({
                id,
                text: this.genTableKey({
                    schemaName: tbl.options.schema,
                    tableName: tbl.options.name,
                }),
            }))
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
         * @return {string} qualifier name
         */
        genTableKey({ schemaName, tableName }) {
            const { quotingIdentifier: quote } = this.$helpers
            return `${quote(schemaName)}.${quote(tableName)}`
        },
        getReferencedTable({ id, key }) {
            return this.lookupTables[id] || this.newLookupTables[key] || {}
        },
        getColOptions({ map, tableId }) {
            return this.$helpers.lodash.map(map[tableId], (text, id) => ({ id, text }))
        },
        async fetchReferencedTablesData(targets) {
            this.isLoading = true
            const [, parsedTables] = await queryHelper.queryAndParseDDL({
                connId: this.connData.id,
                tableNodes: targets,
                config: this.connData.config,
            })
            this.newLookupTables = parsedTables.reduce((map, tbl) => {
                const key = this.genTableKey({
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
        //TODO: handle input change
        onChangeInput() {},
    },
}
</script>
