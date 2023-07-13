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
                            getColOptions($typy(fkReferencedTableMap[rowData[0]], 'id').safeString)
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
            REFERENCE_OPTIONS: state => state.mxsWorkspace.config.REFERENCE_OPTIONS,
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
                map[key.id] = this.allTables.find(
                    t =>
                        t.id === key.referenced_tbl_id ||
                        (t.options.schema === key.referenced_schema_name &&
                            t.options.name === key.referenced_table_name)
                )
                return map
            }, {})
        },
        idxOfColId() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.ID]
        },
        idxOfColName() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.NAME]
        },
        idxOfColType() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.TYPE]
        },
        rows() {
            return this.stagingKeys.map(
                ({ id, name, index_cols, referenced_index_cols, on_update, on_delete }) => {
                    const referencedTbl = this.fkReferencedTableMap[id]
                    let referencedColNames = [],
                        referencedColIds = []
                    if (referencedTbl) {
                        referencedColNames = referenced_index_cols.map(c => {
                            if (c.name) return c.name
                            return this.allTableColNameMap[referencedTbl.id][c.id]
                        })
                        referencedColIds = referencedTbl.definitions.cols.reduce((res, c) => {
                            if (referencedColNames.includes(c[this.idxOfColName]))
                                res.push(c[this.idxOfColId])
                            return res
                        }, [])
                    }
                    return [
                        id,
                        name,
                        index_cols.map(c => c.id),
                        this.$typy(referencedTbl, 'id').safeString,
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
        allTables() {
            return Object.values({ ...this.lookupTables, ...this.newLookupTables })
        },
        /**
         * @returns {Object.<string, Object.<string, string>>} e.g. { "tbl_1": { "col_1": "id", "col_2": "name" } }
         */
        allTableColNameMap() {
            return this.allTables.reduce((res, tbl) => {
                res[tbl.id] = queryHelper.createColNameMap(
                    this.$typy(tbl, 'definitions.cols').safeArray
                )
                return res
            }, {})
        },
        /**
         * @returns {Object.<string, Array.<Array>>}  e.g. { "tbl_123": [][] }
         */
        allTableColMap() {
            return this.allTables.reduce((res, tbl) => {
                res[tbl.id] = this.$typy(tbl, 'definitions.cols').safeArray
                return res
            }, {})
        },
        referencingColOptions() {
            return this.getColOptions(this.tableId)
        },
        referencedTargets() {
            const { quotingIdentifier: quote } = this.$helpers
            return this.allTables.map(tbl => ({
                id: tbl.id,
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
        getColOptions(tableId) {
            if (!this.allTableColMap[tableId]) return []
            return this.allTableColMap[tableId].map(c => ({
                id: c[this.idxOfColId],
                text: c[this.idxOfColName],
                type: c[this.idxOfColType],
            }))
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
        addNewKey() {
            const tableName = this.$typy(this.lookupTables[this.tableId], 'options.name').safeString
            this.stagingKeys.push({
                id: `key_${this.$helpers.uuidv1()}`,
                index_cols: [],
                name: `${tableName}_ibfk_${this.stagingKeys.length}`,
                on_delete: this.REFERENCE_OPTIONS.NO_ACTION,
                on_update: this.REFERENCE_OPTIONS.NO_ACTION,
                referenced_index_cols: [],
                referenced_schema_name: '',
                referenced_table_name: '',
            })
        },
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
