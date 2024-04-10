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
                :data="rows"
                :itemHeight="32"
                :maxHeight="dim.height - headerHeight"
                :boundingWidth="dim.width"
                showSelect
                :isVertTable="isVertTable"
                :noDataText="$mxs_t('noEntity', { entityName: $mxs_t('foreignKeys') })"
                :selectedItems.sync="selectedItems"
                v-on="$listeners"
            >
                <template v-slot:[FK_EDITOR_ATTRS.NAME]="{ data: { cell, rowData } }">
                    <lazy-text-field
                        :value="cell"
                        :name="FK_EDITOR_ATTRS.NAME"
                        :height="28"
                        :required="true"
                        @on-input="
                            onChangeInput({ value: $event, field: FK_EDITOR_ATTRS.NAME, rowData })
                        "
                    />
                </template>
                <template v-slot:[FK_EDITOR_ATTRS.REF_TARGET]="{ data: { cell, rowData } }">
                    <lazy-select
                        :value="cell"
                        class="ref-target-input"
                        :height="28"
                        :name="FK_EDITOR_ATTRS.REF_TARGET"
                        :items="refTargets"
                        item-text="text"
                        item-value="id"
                        :selectionText="
                            $typy(
                                refTargets.find(item => item.id === cell),
                                'text'
                            ).safeString
                        "
                        :required="true"
                        :rules="[v => !!v]"
                        @on-input="
                            onChangeInput({
                                value: $event,
                                field: FK_EDITOR_ATTRS.REF_TARGET,
                                rowData,
                            })
                        "
                    />
                    <!-- TODO: Add an option for REF_TARGET input to manually type in new target -->
                </template>
                <template
                    v-for="optField in refOptFields"
                    v-slot:[optField]="{ data: { cell, rowData } }"
                >
                    <lazy-select
                        :key="optField"
                        class="ref-opt-input"
                        :value="cell"
                        :name="optField"
                        :height="28"
                        :items="refOptItems"
                        @on-input="onChangeInput({ value: $event, field: optField, rowData })"
                    />
                </template>
                <template
                    v-for="field in columnFields"
                    v-slot:[field]="{ data: { cell, rowData } }"
                >
                    <fk-col-field-input
                        :key="field"
                        :value="cell"
                        :field="field"
                        :height="28"
                        :referencingColOptions="referencingColOptions"
                        :refColOpts="getColOptions($typy(fkRefTblMap[rowData[0]], 'id').safeString)"
                        @on-input="onChangeInput({ value: $event, field, rowData })"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations } from 'vuex'
import TblToolbar from '@share/components/common/MxsDdlEditor/TblToolbar.vue'
import FkColFieldInput from '@share/components/common/MxsDdlEditor/FkColFieldInput.vue'
import LazyTextField from '@share/components/common/MxsDdlEditor/LazyTextField'
import LazySelect from '@share/components/common/MxsDdlEditor/LazySelect'
import queryHelper from '@wsSrc/store/queryHelper'
import { checkFkSupport } from '@share/components/common/MxsDdlEditor/utils.js'
import erdHelper from '@wsSrc/utils/erdHelper'
import {
    CREATE_TBL_TOKENS,
    FK_EDITOR_ATTRS,
    REF_OPTS,
    UNPARSED_TBL_PLACEHOLDER,
} from '@wsSrc/constants'

export default {
    name: 'fk-definitions',
    components: { TblToolbar, FkColFieldInput, LazySelect, LazyTextField },
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
        headers() {
            let header = { sortable: false, uppercase: true, useCellSlot: true }
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
        refOptFields() {
            const { ON_UPDATE, ON_DELETE } = this.FK_EDITOR_ATTRS
            return [ON_UPDATE, ON_DELETE]
        },
        columnFields() {
            const { COLS, REF_COLS } = this.FK_EDITOR_ATTRS
            return [COLS, REF_COLS]
        },
        refOptItems() {
            return Object.values(REF_OPTS)
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
            return CREATE_TBL_TOKENS.foreignKey
        },
        plainKeyToken() {
            return CREATE_TBL_TOKENS.key
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
                let referencedColIds = []
                if (refTbl) {
                    const refTblCols = Object.values(refTbl.defs.col_map)
                    referencedColIds = ref_cols.map(c => {
                        if (c.name) return refTblCols.find(item => item.name === c.name).id
                        return c.id
                    })
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
            return this.$helpers.lodash.uniqBy(targets, target => `${target.schema}.${target.tbl}`)
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
        this.FK_EDITOR_ATTRS = FK_EDITOR_ATTRS
        await this.init()
    },
    methods: {
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
        }),
        async init() {
            this.assignData()
            if (this.unknownTargets.length) {
                this.isLoading = true
                await this.fetchUnparsedRefTbl(this.unknownTargets)
                this.isLoading = false
            }
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
        async fetchUnparsedRefTbl(targets) {
            const [e, parsedTables] = await queryHelper.queryAndParseTblDDL({
                connId: this.connData.id,
                targets,
                config: this.connData.config,
                charsetCollationMap: this.charsetCollationMap,
            })
            if (e)
                this.SET_SNACK_BAR_MESSAGE({
                    text: [
                        this.$mxs_t('errors.failedToGetRefTbl'),
                        ...this.$helpers.getErrorsArr(e),
                    ],
                    type: 'error',
                })
            else
                this.tmpLookupTables = this.$helpers.immutableUpdate(this.tmpLookupTables, {
                    $merge: parsedTables.reduce((map, parsedTable) => {
                        map[parsedTable.id] = parsedTable
                        return map
                    }, {}),
                })
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
                on_delete: REF_OPTS.NO_ACTION,
                on_update: REF_OPTS.NO_ACTION,
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
        async onChangeInput(item) {
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
                 * In alter-table-editor component, when the component is mounted,
                 * lookupTables always has 1 table which is itself. Unknown targets or new targets
                 * will be fetched, parsed and kept in memory.
                 * Using referenced names directly for referenced targets data as the names are not
                 * mutable.
                 * In entity-editor-ctr component, lookupTables has all tables in the ERD, ids are
                 * used for reference targets because the names can be altered.
                 */
                case REF_TARGET: {
                    if (this.isReferencedTblPersisted(item.value)) {
                        this.setTargetRefTblId({ keyId: id, value: item.value })
                    } else {
                        let newReferencedTbl = this.tmpLookupTables[item.value]
                        let errors = []
                        if (item.value.includes(UNPARSED_TBL_PLACEHOLDER)) {
                            const unparsedTblTarget = this.refTargets.find(
                                target => target.id === item.value
                            )
                            const [e, parsedTables] = await queryHelper.queryAndParseTblDDL({
                                connId: this.connData.id,
                                targets: [
                                    {
                                        schema: unparsedTblTarget.schema,
                                        tbl: unparsedTblTarget.name,
                                    },
                                ],
                                config: this.connData.config,
                                charsetCollationMap: this.charsetCollationMap,
                            })
                            if (e) errors = this.$helpers.getErrorsArr(e)
                            else {
                                newReferencedTbl = parsedTables[0]
                                this.tmpLookupTables = this.$helpers.immutableUpdate(
                                    this.tmpLookupTables,
                                    { [newReferencedTbl.id]: { $set: newReferencedTbl } }
                                )
                            }
                        }
                        if (newReferencedTbl)
                            this.setNewTargetRefTblName({ keyId: id, newReferencedTbl })
                        else {
                            this.SET_SNACK_BAR_MESSAGE({
                                text: [this.$mxs_t('errors.failedToGetRefTbl'), ...errors],
                                type: 'error',
                            })
                            this.setTargetRefTblId({ keyId: id, value: '' })
                        }
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
        setTargetRefTblId({ keyId, value }) {
            this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(this.stagingKeyCategoryMap, {
                [this.foreignKeyToken]: {
                    [keyId]: {
                        $unset: ['ref_schema_name', 'ref_tbl_name'],
                        ref_cols: { $set: [] },
                        ref_tbl_id: { $set: value },
                    },
                },
            })
        },
        setNewTargetRefTblName({ keyId, newReferencedTbl }) {
            this.stagingKeyCategoryMap = this.$helpers.immutableUpdate(this.stagingKeyCategoryMap, {
                [this.foreignKeyToken]: {
                    [keyId]: {
                        ref_cols: { $set: [] },
                        $unset: ['ref_tbl_id'],
                        ref_schema_name: {
                            $set: newReferencedTbl.options.schema,
                        },
                        ref_tbl_name: {
                            $set: newReferencedTbl.options.name,
                        },
                    },
                },
            })
        },
    },
}
</script>
