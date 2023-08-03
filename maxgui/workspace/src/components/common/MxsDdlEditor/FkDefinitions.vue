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
                :noDataText="$mxs_t('noEntity', { entityName: $mxs_t('foreignKeys') })"
                :selectedItems.sync="selectedItems"
                v-on="$listeners"
            >
                <template v-for="h in headers" v-slot:[h.text]="{ data: { cell, rowData } }">
                    <fk-definition-col
                        :key="h.text"
                        :data="{
                            field: h.text,
                            value: cell,
                            rowData,
                        }"
                        :height="28"
                        :referencingColOptions="referencingColOptions"
                        :refTargets="refTargets"
                        :refColOpts="getColOptions($typy(fkRefTblMap[rowData[0]], 'id').safeString)"
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
import { checkFkSupport } from '@wsSrc/components/common/MxsDdlEditor/utils.js'
import erdHelper from '@wsSrc/utils/erdHelper'

export default {
    name: 'fk-definitions',
    components: { TblToolbar, FkDefinitionCol },
    props: {
        value: { type: Object, required: true },
        tableId: { type: String, required: true },
        dim: { type: Object, required: true },
        lookupTables: { type: Object, required: true },
        newLookupTables: { type: Object, required: true }, // sync
        allLookupTables: { type: Array, required: true },
        allTableColMap: { type: Object, required: true },
        refTargets: { type: Array, required: true },
        tablesColNameMap: { type: Object, required: true },
        connData: { type: Object, required: true },
        charsetCollationMap: { type: Object, required: true },
    },
    data() {
        return {
            headerHeight: 0,
            selectedItems: [],
            isVertTable: false,
            isLoading: false,
            stagingKeyCategoryMap: {},
        }
    },
    computed: {
        ...mapState({
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            FK_EDITOR_ATTRS: state => state.mxsWorkspace.config.FK_EDITOR_ATTRS,
            REF_OPTS: state => state.mxsWorkspace.config.REF_OPTS,
        }),
        headers() {
            let header = { sortable: false, uppercase: true }
            const {
                ID,
                NAME,
                COLS,
                REF_TARGET,
                REF_COLS,
                ON_UPDATE,
                ON_DELETE,
            } = this.FK_EDITOR_ATTRS
            return [
                { text: ID, hidden: true },
                { text: NAME, required: true, ...header },
                { text: COLS, required: true, minWidth: 146, ...header },
                { text: REF_TARGET, required: true, minWidth: 146, ...header },
                { text: REF_COLS, required: true, minWidth: 142, ...header },
                { text: ON_UPDATE, width: 166, minWidth: 86, ...header },
                { text: ON_DELETE, width: 166, minWidth: 86, ...header },
            ]
        },
        // new referenced tables keyed by id
        tmpLookupTables: {
            get() {
                return this.newLookupTables
            },
            set(v) {
                this.$emit('update:newLookupTables', v)
            },
        },
        keyCategoryMap: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        foreignKeyToken() {
            return this.CREATE_TBL_TOKENS.foreignKey
        },
        plainKeyToken() {
            return this.CREATE_TBL_TOKENS.key
        },
        plainKeyMap() {
            return this.$typy(this.value, `[${this.plainKeyToken}]`).safeObjectOrEmpty
        },
        plainKeyNameMap() {
            return this.$helpers.lodash.keyBy(Object.values(this.plainKeyMap), 'name')
        },
        fkMap() {
            return this.$typy(this.stagingKeyCategoryMap, `[${this.foreignKeyToken}]`)
                .safeObjectOrEmpty
        },
        fks() {
            return Object.values(this.fkMap)
        },
        // mapped by FK id
        fkRefTblMap() {
            return this.fks.reduce((map, key) => {
                map[key.id] = this.allLookupTables.find(
                    t =>
                        t.id === key.ref_tbl_id ||
                        (t.options.schema === key.ref_schema_name &&
                            t.options.name === key.ref_tbl_name)
                )
                return map
            }, {})
        },
        rows() {
            return this.fks.map(({ id, name, cols, ref_cols, on_update, on_delete }) => {
                const refTbl = this.fkRefTblMap[id]
                let referencedColNames = [],
                    referencedColIds = []
                if (refTbl) {
                    referencedColNames = ref_cols.map(c => {
                        if (c.name) return c.name
                        return this.tablesColNameMap[refTbl.id][c.id]
                    })
                    referencedColIds = Object.values(refTbl.defs.col_map).reduce((res, c) => {
                        if (referencedColNames.includes(c.name)) res.push(c.id)
                        return res
                    }, [])
                }
                return [
                    id,
                    name,
                    cols.map(c => c.id),
                    this.$typy(refTbl, 'id').safeString,
                    referencedColIds,
                    on_update,
                    on_delete,
                ]
            })
        },
        unknownTargets() {
            const targets = this.fks.reduce((res, { ref_tbl_name, ref_schema_name }) => {
                if (ref_tbl_name) res.push({ schema: ref_schema_name, tbl: ref_tbl_name })
                return res
            }, [])
            return this.$helpers.lodash.uniqBy(targets, 'qualified_name')
        },
        referencingColOptions() {
            return this.getColOptions(this.tableId)
        },
    },
    watch: {
        keyCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(v, this.stagingKeyCategoryMap)) this.assignData()
            },
        },
        stagingKeyCategoryMap: {
            deep: true,
            handler(v) {
                if (!this.$helpers.lodash.isEqual(this.keyCategoryMap, v)) this.keyCategoryMap = v
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
            this.stagingKeyCategoryMap = this.$helpers.lodash.cloneDeep(this.keyCategoryMap)
        },
        getColOptions(tableId) {
            if (!this.allTableColMap[tableId]) return []
            return erdHelper.genIdxColOpts({
                tableColMap: this.allTableColMap[tableId],
                disableHandler: type => !checkFkSupport(type),
            })
        },
        async fetchReferencedTablesData(targets) {
            this.isLoading = true
            const [, parsedTables] = await queryHelper.queryAndParseDDL({
                connId: this.connData.id,
                targets,
                config: this.connData.config,
                charsetCollationMap: this.charsetCollationMap,
            })
            this.tmpLookupTables = parsedTables.reduce((map, parsedTable) => {
                map[parsedTable.id] = parsedTable
                return map
            }, {})
            this.isLoading = false
        },
        deleteSelectedKeys(selectedItems) {
            const { immutableUpdate } = this.$helpers
            const map = selectedItems.reduce(
                (res, [id, name]) => {
                    res.ids.push(id)
                    res.names.push(name)
                    /**
                     * When creating an FK, if the col is not indexed,
                     * a plain key will be generated automatically using
                     * the same name as the FK.
                     */
                    const plainKey = this.plainKeyNameMap[name]
                    if (plainKey) res.plainKeyIds.push(plainKey.id)
                    return res
                },
                { ids: [], names: [], plainKeyIds: [] }
            )

            const fkMap = immutableUpdate(this.fkMap, { $unset: map.ids })
            const plainKeyMap = immutableUpdate(this.plainKeyMap, { $unset: map.plainKeyIds })

            let keyCategoryMap = this.$helpers.immutableUpdate(
                this.stagingKeyCategoryMap,
                Object.keys(fkMap).length
                    ? { [this.foreignKeyToken]: { $set: fkMap } }
                    : { $unset: [this.foreignKeyToken] }
            )
            // Drop also PLAIN key
            keyCategoryMap = immutableUpdate(
                keyCategoryMap,
                Object.keys(plainKeyMap).length
                    ? { [this.plainKeyToken]: { $set: plainKeyMap } }
                    : { $unset: [this.plainKeyToken] }
            )
            this.stagingKeyCategoryMap = keyCategoryMap
        },
        addNewKey() {
            const { immutableUpdate } = this.$helpers
            const tableName = this.$typy(this.lookupTables[this.tableId], 'options.name').safeString
            const newKey = {
                id: `key_${this.$helpers.uuidv1()}`,
                cols: [],
                name: `${tableName}_ibfk_${this.fks.length}`,
                on_delete: this.REF_OPTS.NO_ACTION,
                on_update: this.REF_OPTS.NO_ACTION,
                ref_cols: [],
                ref_schema_name: '',
                ref_tbl_name: '',
            }
            this.stagingKeyCategoryMap = immutableUpdate(
                this.stagingKeyCategoryMap,
                this.foreignKeyToken in this.stagingKeyCategoryMap
                    ? { [this.foreignKeyToken]: { [newKey.id]: { $set: newKey } } }
                    : { $merge: { [this.foreignKeyToken]: { [newKey.id]: newKey } } }
            )
        },
        updateStagingKeys(id, keyField, value) {
            this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(this.stagingKeyCategoryMap, {
                [this.foreignKeyToken]: {
                    [id]: { [keyField]: { $set: value } },
                },
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
            const { NAME, COLS, REF_TARGET, REF_COLS, ON_UPDATE, ON_DELETE } = this.FK_EDITOR_ATTRS
            const id = item.rowData[0]
            switch (item.field) {
                case NAME:
                    this.updateStagingKeys(id, 'name', item.value)
                    break
                case ON_UPDATE:
                    this.updateStagingKeys(id, 'on_update', item.value)
                    break
                case ON_DELETE:
                    this.updateStagingKeys(id, 'on_delete', item.value)
                    break
                case COLS:
                    this.updateStagingKeys(
                        id,
                        'cols',
                        item.value.map(id => ({ id }))
                    )
                    break
                /**
                 * For REF_TARGET and REF_COLS,
                 * if the referenced table is in lookupTables, the data will be assigned with
                 * ids; otherwise, names will be assigned. This is an intention to
                 * keep new referenced tables data in memory (tmpLookupTables) and because of the
                 * following reasons:
                 * In alter-table-editor component, lookupTables always has 1 table which is itself.
                 * Using referenced names for referenced targets data as tmpLookupTables is kept in
                 * memory.
                 * In entity-editor-ctr component, lookupTables has all tables in the ERD, ids are
                 * used for reference targets because the names can be altered.
                 */
                case REF_TARGET: {
                    if (this.isReferencedTblPersisted(item.value)) {
                        this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(
                            this.stagingKeyCategoryMap,
                            {
                                [this.foreignKeyToken]: {
                                    [id]: {
                                        $unset: ['ref_schema_name', 'ref_tbl_name'],
                                        ref_cols: { $set: [] },
                                        ref_tbl_id: { $set: item.value },
                                    },
                                },
                            }
                        )
                    } else {
                        const newReferencedTbl = this.tmpLookupTables[item.value]
                        this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(
                            this.stagingKeyCategoryMap,
                            {
                                [this.foreignKeyToken]: {
                                    [id]: {
                                        ref_cols: { $set: [] },
                                        $unset: ['ref_tbl_id'],
                                        ref_schema_name: { $set: newReferencedTbl.options.schema },
                                        ref_tbl_name: { $set: newReferencedTbl.options.name },
                                    },
                                },
                            }
                        )
                    }
                    break
                }
                case REF_COLS: {
                    let values = []
                    if (item.value.length) {
                        const referencedTblId = this.fkRefTblMap[id].id
                        if (this.isReferencedTblPersisted(referencedTblId))
                            values = item.value.map(id => ({ id }))
                        else
                            values = item.value.map(id => ({
                                name: this.tablesColNameMap[referencedTblId][id],
                            }))
                    }
                    this.updateStagingKeys(id, 'ref_cols', values)
                    break
                }
            }
        },
    },
}
</script>
