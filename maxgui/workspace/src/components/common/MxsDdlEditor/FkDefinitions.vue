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
            // new referenced tables keyed by id
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
            return this.stagingKeys.reduce((map, key) => {
                map[key.id] =
                    this.allTables.find(
                        t =>
                            t.id === key.referenced_tbl_id ||
                            (t.options.schema === key.referenced_schema_name &&
                                t.options.name === key.referenced_table_name)
                    ) || {}
                return map
            }, {})
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
            return { ...this.lookupTables, ...this.newLookupTables }
        },
        allTables() {
            return Object.values(this.allTableMap)
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
            const { quotingIdentifier: quote, lodash } = this.$helpers
            return lodash.map(this.allTableMap, (tbl, id) => ({
                id,
                text: `${quote(tbl.options.schema)}.${quote(tbl.options.name)}`,
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
                map[tbl.id] = queryHelper.tableParserTransformer({
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
        updateStagingKeys(rowIdx, keyField, value) {
            this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                [rowIdx]: { [keyField]: { $set: value } },
            })
        },
        /**
         * Checks whether the referenced target table can be found in the lookupTables
         * @param {string} id - table id
         */
        isReferencedTblPersisted(id) {
            return Boolean(this.lookupTables[id])
        },
        onChangeInput(item) {
            const {
                NAME,
                REFERENCING_COL,
                REFERENCED_TARGET,
                REFERENCED_COL,
                ON_UPDATE,
                ON_DELETE,
            } = this.FK_EDITOR_ATTRS
            switch (item.field) {
                case NAME:
                    this.updateStagingKeys(item.rowIdx, 'name', item.value)
                    break
                case ON_UPDATE:
                    this.updateStagingKeys(item.rowIdx, 'on_update', item.value)
                    break
                case ON_DELETE:
                    this.updateStagingKeys(item.rowIdx, 'on_delete', item.value)
                    break
                case REFERENCING_COL:
                    this.updateStagingKeys(
                        item.rowIdx,
                        'index_cols',
                        item.value.map(id => ({ id }))
                    )
                    break
                /**
                 * For REFERENCED_TARGET and REFERENCED_COL,
                 * if the referenced table is in lookupTables, the data will be assigned with
                 * ids; otherwise, names will be assigned. This is an intention to
                 * keep new referenced tables data in memory (newLookupTables) and because of the
                 * following reasons:
                 * In alter-table-editor component, lookupTables always has 1 table which is itself.
                 * Using referenced names for referenced targets data as newLookupTables is kept in
                 * memory.
                 * In entity-editor-ctr component, lookupTables has all tables in the ERD, ids are
                 * used for reference targets because the names can be altered.
                 */
                case REFERENCED_TARGET: {
                    if (this.isReferencedTblPersisted(item.value)) {
                        this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                            [item.rowIdx]: {
                                $unset: ['referenced_schema_name', 'referenced_table_name'],
                                referenced_index_cols: { $set: [] },
                                referenced_tbl_id: { $set: item.value },
                            },
                        })
                    } else {
                        const newReferencedTbl = this.newLookupTables[item.value]
                        this.stagingKeys = this.$helpers.immutableUpdate(this.stagingKeys, {
                            [item.rowIdx]: {
                                referenced_index_cols: { $set: [] },
                                $unset: ['referenced_tbl_id'],
                                referenced_schema_name: { $set: newReferencedTbl.options.schema },
                                referenced_table_name: { $set: newReferencedTbl.options.name },
                            },
                        })
                    }
                    break
                }
                case REFERENCED_COL: {
                    let values = []
                    if (item.value.length) {
                        const keyId = this.stagingKeys[item.rowIdx].id
                        const referencedTblId = this.fkReferencedTableMap[keyId].id
                        if (this.isReferencedTblPersisted(referencedTblId))
                            values = item.value.map(id => ({ id }))
                        else
                            values = item.value.map(id => ({
                                name: this.allTableColNameMap[referencedTblId][id],
                            }))
                    }
                    this.updateStagingKeys(item.rowIdx, 'referenced_index_cols', values)
                    break
                }
            }
        },
    },
}
</script>
