<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TblToolbar from '@wsComps/DdlEditor/TblToolbar.vue'
import FkColFieldInput from '@wsComps/DdlEditor/FkColFieldInput.vue'
import LazyInput from '@wsComps/DdlEditor/LazyInput.vue'
import { queryAndParseTblDDL } from '@/store/queryHelper'
import { checkFkSupport } from '@wsComps/DdlEditor/utils.js'
import erdHelper from '@/utils/erdHelper'
import {
  CREATE_TBL_TOKENS,
  FK_EDITOR_ATTRS,
  REF_OPTS,
  UNPARSED_TBL_PLACEHOLDER,
} from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Object, required: true },
  tableId: { type: String, required: true },
  dim: { type: Object, required: true },
  lookupTables: { type: Object, required: true },
  newLookupTables: { type: Object, required: true },
  allLookupTables: { type: Array, required: true },
  allTableColMap: { type: Object, required: true },
  refTargets: { type: Array, required: true },
  tablesColNameMap: { type: Object, required: true },
  connData: { type: Object, required: true },
  charsetCollationMap: { type: Object, required: true },
})
const emit = defineEmits(['update:modelValue', 'update:newLookupTables'])

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const {
  lodash: { keyBy, uniqBy, isEqual, cloneDeep },
  getErrorsArr,
  immutableUpdate,
  uuidv1,
} = useHelpers()

const headerHeight = ref(0)
const selectedItems = ref([])
const isVertTable = ref(false)
const isLoading = ref(false)
const stagingKeyCategoryMap = ref({})

const REF_OPT_FIELDS = [FK_EDITOR_ATTRS.ON_UPDATE, FK_EDITOR_ATTRS.ON_DELETE]
const COL_FIELDS = [FK_EDITOR_ATTRS.COLS, FK_EDITOR_ATTRS.REF_COLS]
const REF_OPT_ITEMS = Object.values(REF_OPTS)

const headers = computed(() => {
  const commonHeaderProps = {
    sortable: false,
    uppercase: true,
    useCellSlot: true,
    cellProps: isVertTable.value
      ? null
      : { class: 'px-1 d-inline-flex align-center justify-center' },
  }
  return [
    { text: FK_EDITOR_ATTRS.ID, hidden: true },
    { text: FK_EDITOR_ATTRS.NAME, required: true, ...commonHeaderProps },
    { text: FK_EDITOR_ATTRS.COLS, required: true, minWidth: 146, ...commonHeaderProps },
    { text: FK_EDITOR_ATTRS.REF_TARGET, required: true, minWidth: 146, ...commonHeaderProps },
    { text: FK_EDITOR_ATTRS.REF_COLS, required: true, minWidth: 142, ...commonHeaderProps },
    { text: FK_EDITOR_ATTRS.ON_UPDATE, width: 166, minWidth: 86, ...commonHeaderProps },
    { text: FK_EDITOR_ATTRS.ON_DELETE, width: 166, minWidth: 86, ...commonHeaderProps },
  ]
})

// new referenced tables keyed by id
const tmpLookupTables = computed({
  get: () => props.newLookupTables,
  set: (v) => emit('update:newLookupTables', v),
})
const keyCategoryMap = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const plainKeyMap = computed(
  () => typy(props.modelValue, `[${CREATE_TBL_TOKENS.key}]`).safeObjectOrEmpty
)
const plainKeyNameMap = computed(() => keyBy(Object.values(plainKeyMap.value), 'name'))
const fkMap = computed(
  () => typy(stagingKeyCategoryMap.value, `[${CREATE_TBL_TOKENS.foreignKey}]`).safeObjectOrEmpty
)
const fks = computed(() => Object.values(fkMap.value))
// mapped by FK id
const fkRefTblMap = computed(() =>
  fks.value.reduce((map, key) => {
    map[key.id] = props.allLookupTables.find(
      (t) =>
        t.id === key.ref_tbl_id ||
        (t.options.schema === key.ref_schema_name && t.options.name === key.ref_tbl_name)
    )
    return map
  }, {})
)
const rows = computed(() =>
  fks.value.map(({ id, name, cols, ref_cols, on_update, on_delete }) => {
    const refTbl = fkRefTblMap.value[id]
    let referencedColIds = []
    if (refTbl) {
      const refTblCols = Object.values(refTbl.defs.col_map)
      referencedColIds = ref_cols.map((c) => {
        if (c.name) return refTblCols.find((item) => item.name === c.name).id
        return c.id
      })
    }
    return [
      id,
      name,
      cols.map((c) => c.id),
      typy(refTbl, 'id').safeString,
      referencedColIds,
      on_update,
      on_delete,
    ]
  })
)
const unknownTargets = computed(() =>
  uniqBy(
    fks.value.reduce((res, { ref_tbl_name, ref_schema_name }) => {
      if (ref_tbl_name) res.push({ schema: ref_schema_name, tbl: ref_tbl_name })
      return res
    }, []),
    (target) => `${target.schema}.${target.tbl}`
  )
)
const referencingColOptions = computed(() => getColOptions(props.tableId))

watch(
  keyCategoryMap,
  (v) => {
    if (!isEqual(v, stagingKeyCategoryMap.value)) assignData()
  },
  { deep: true }
)
watch(
  stagingKeyCategoryMap,
  (v) => {
    if (!isEqual(keyCategoryMap.value, v)) keyCategoryMap.value = v
  },
  { deep: true }
)

onBeforeMount(async () => await init())

async function init() {
  assignData()
  if (unknownTargets.value.length) {
    isLoading.value = true
    await fetchUnparsedRefTbl(unknownTargets.value)
    isLoading.value = false
  }
}

function assignData() {
  stagingKeyCategoryMap.value = cloneDeep(keyCategoryMap.value)
}

function getColOptions(tableId) {
  if (!props.allTableColMap[tableId]) return []
  return erdHelper.genIdxColOpts({
    tableColMap: props.allTableColMap[tableId],
    disableHandler: (type) => !checkFkSupport(type),
  })
}

async function fetchUnparsedRefTbl(targets) {
  const [e, parsedTables] = await queryAndParseTblDDL({
    connId: props.connData.id,
    targets,
    config: props.connData.config,
    charsetCollationMap: props.charsetCollationMap,
  })
  if (e)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.failedToGetRefTbl'), ...getErrorsArr(e)],
      type: 'error',
    })
  else
    tmpLookupTables.value = immutableUpdate(tmpLookupTables.value, {
      $merge: parsedTables.reduce((map, parsedTable) => {
        map[parsedTable.id] = parsedTable
        return map
      }, {}),
    })
}

function deleteSelectedKeys() {
  const map = selectedItems.value.reduce(
    (res, [id, name]) => {
      res.ids.push(id)
      res.names.push(name)
      /**
       * When creating an FK, if the col is not indexed,
       * a plain key will be generated automatically using
       * the same name as the FK.
       */
      const plainKey = plainKeyNameMap.value[name]
      if (plainKey) res.plainKeyIds.push(plainKey.id)
      return res
    },
    { ids: [], names: [], plainKeyIds: [] }
  )

  const newFkMap = immutableUpdate(fkMap.value, { $unset: map.ids })
  const newPlainKeyMap = immutableUpdate(plainKeyMap.value, { $unset: map.plainKeyIds })

  let newKeyCategoryMap = immutableUpdate(
    stagingKeyCategoryMap.value,
    Object.keys(newFkMap).length
      ? { [CREATE_TBL_TOKENS.foreignKey]: { $set: newFkMap } }
      : { $unset: [CREATE_TBL_TOKENS.foreignKey] }
  )
  // Drop also PLAIN key
  newKeyCategoryMap = immutableUpdate(
    newKeyCategoryMap,
    Object.keys(newPlainKeyMap).length
      ? { [CREATE_TBL_TOKENS.key]: { $set: newPlainKeyMap } }
      : { $unset: [CREATE_TBL_TOKENS.key] }
  )
  stagingKeyCategoryMap.value = newKeyCategoryMap
  selectedItems.value = []
}

function addNewKey() {
  const tableName = typy(props.lookupTables[props.tableId], 'options.name').safeString
  const newKey = {
    id: `key_${uuidv1()}`,
    cols: [],
    name: `${tableName}_ibfk_${fks.value.length}`,
    on_delete: REF_OPTS.NO_ACTION,
    on_update: REF_OPTS.NO_ACTION,
    ref_cols: [],
    ref_schema_name: '',
    ref_tbl_name: '',
  }
  stagingKeyCategoryMap.value = immutableUpdate(
    stagingKeyCategoryMap.value,
    CREATE_TBL_TOKENS.foreignKey in stagingKeyCategoryMap.value
      ? { [CREATE_TBL_TOKENS.foreignKey]: { [newKey.id]: { $set: newKey } } }
      : { $merge: { [CREATE_TBL_TOKENS.foreignKey]: { [newKey.id]: newKey } } }
  )
}

function updateStagingKeys(id, keyField, value) {
  stagingKeyCategoryMap.value = immutableUpdate(stagingKeyCategoryMap.value, {
    [CREATE_TBL_TOKENS.foreignKey]: {
      [id]: { [keyField]: { $set: value } },
    },
  })
}

/**
 * Checks whether the referenced target table can be found in the lookupTables
 * @param {string} id - table id
 */
function isReferencedTblPersisted(id) {
  return Boolean(props.lookupTables[id])
}

async function onChangeInput(item) {
  const id = item.rowData[0]
  switch (item.field) {
    case FK_EDITOR_ATTRS.NAME:
      updateStagingKeys(id, 'name', item.value)
      break
    case FK_EDITOR_ATTRS.ON_UPDATE:
      updateStagingKeys(id, 'on_update', item.value)
      break
    case FK_EDITOR_ATTRS.ON_DELETE:
      updateStagingKeys(id, 'on_delete', item.value)
      break
    case FK_EDITOR_ATTRS.COLS:
      updateStagingKeys(
        id,
        'cols',
        item.value.map((id) => ({ id }))
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
    case FK_EDITOR_ATTRS.REF_TARGET: {
      if (isReferencedTblPersisted(item.value)) {
        setTargetRefTblId({ keyId: id, value: item.value })
      } else {
        let newReferencedTbl = tmpLookupTables.value[item.value]
        let errors = []
        if (item.value.includes(UNPARSED_TBL_PLACEHOLDER)) {
          const unparsedTblTarget = props.refTargets.find((target) => target.id === item.value)
          const [e, parsedTables] = await queryAndParseTblDDL({
            connId: props.connData.id,
            targets: [
              {
                schema: unparsedTblTarget.schema,
                tbl: unparsedTblTarget.name,
              },
            ],
            config: props.connData.config,
            charsetCollationMap: props.charsetCollationMap,
          })
          if (e) errors = getErrorsArr(e)
          else {
            newReferencedTbl = parsedTables[0]
            tmpLookupTables.value = immutableUpdate(tmpLookupTables.value, {
              [newReferencedTbl.id]: { $set: newReferencedTbl },
            })
          }
        }
        if (newReferencedTbl) setNewTargetRefTblName({ keyId: id, newReferencedTbl })
        else {
          store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
            text: [t('errors.failedToGetRefTbl'), ...errors],
            type: 'error',
          })
          setTargetRefTblId({ keyId: id, value: '' })
        }
      }
      break
    }
    case FK_EDITOR_ATTRS.REF_COLS: {
      let values = []
      if (item.value.length) {
        const referencedTblId = fkRefTblMap.value[id].id
        if (isReferencedTblPersisted(referencedTblId)) values = item.value.map((id) => ({ id }))
        else
          values = item.value.map((id) => ({
            name: props.tablesColNameMap[referencedTblId][id],
          }))
      }
      updateStagingKeys(id, 'ref_cols', values)
      break
    }
  }
}

function setTargetRefTblId({ keyId, value }) {
  stagingKeyCategoryMap.value = immutableUpdate(stagingKeyCategoryMap.value, {
    [CREATE_TBL_TOKENS.foreignKey]: {
      [keyId]: {
        $unset: ['ref_schema_name', 'ref_tbl_name'],
        ref_cols: { $set: [] },
        ref_tbl_id: { $set: value },
      },
    },
  })
}

function setNewTargetRefTblName({ keyId, newReferencedTbl }) {
  stagingKeyCategoryMap.value = immutableUpdate(stagingKeyCategoryMap.value, {
    [CREATE_TBL_TOKENS.foreignKey]: {
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
}
</script>

<template>
  <div class="fill-height">
    <VProgressLinear v-if="isLoading" indeterminate color="primary" />
    <template v-else>
      <TblToolbar
        v-model:isVertTable="isVertTable"
        :selectedItems="selectedItems"
        @get-computed-height="headerHeight = $event"
        @on-delete="deleteSelectedKeys"
        @on-add="addNewKey"
      />
      <VirtualScrollTbl
        v-model:selectedItems="selectedItems"
        :headers="headers"
        :data="rows"
        :itemHeight="32"
        :maxHeight="dim.height - headerHeight"
        :boundingWidth="dim.width"
        showSelect
        :isVertTable="isVertTable"
        :noDataText="$t('noEntity', [$t('foreignKeys')])"
      >
        <template #[FK_EDITOR_ATTRS.NAME]="{ data: { cell, rowData } }">
          <LazyInput
            :modelValue="cell"
            required
            @update:modelValue="
              onChangeInput({ value: $event, field: FK_EDITOR_ATTRS.NAME, rowData })
            "
            @blur="
              onChangeInput({
                value: $typy($event, 'srcElement.value').safeString,
                field: FK_EDITOR_ATTRS.NAME,
                rowData,
              })
            "
          />
        </template>
        <template v-for="field in COL_FIELDS" #[field]="{ data: { cell, rowData } }" :key="field">
          <FkColFieldInput
            :modelValue="cell"
            :field="field"
            :referencingColOptions="referencingColOptions"
            :refColOpts="getColOptions($typy(fkRefTblMap[rowData[0]], 'id').safeString)"
            @update:modelValue="onChangeInput({ value: $event, field, rowData })"
          />
        </template>
        <template #[FK_EDITOR_ATTRS.REF_TARGET]="{ data: { cell, rowData } }">
          <LazyInput
            :modelValue="cell"
            isSelect
            :items="refTargets"
            item-title="text"
            item-value="id"
            :selectionText="
              $typy(
                refTargets.find((item) => item.id === cell),
                'text'
              ).safeString
            "
            required
            @update:modelValue="
              onChangeInput({ value: $event, field: FK_EDITOR_ATTRS.REF_TARGET, rowData })
            "
          />
          <!-- TODO: Add an option for REF_TARGET input to manually type in new target -->
        </template>
        <template
          v-for="field in REF_OPT_FIELDS"
          #[field]="{ data: { cell, rowData } }"
          :key="field"
        >
          <LazyInput
            :modelValue="cell"
            :items="REF_OPT_ITEMS"
            isSelect
            @update:modelValue="onChangeInput({ value: $event, field, rowData })"
          />
        </template>
      </VirtualScrollTbl>
    </template>
  </div>
</template>
