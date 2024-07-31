<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import LazyInput from '@wsComps/TblStructureEditor/LazyInput.vue'
import DataTypeInput from '@wsComps/TblStructureEditor/DataTypeInput.vue'
import CharsetCollateInput from '@wsComps/TblStructureEditor/CharsetCollateInput.vue'
import BoolInput from '@wsComps/TblStructureEditor/BoolInput.vue'
import TblToolbar from '@wsComps/TblStructureEditor/TblToolbar.vue'
import {
  getColumnTypes,
  checkUniqueZeroFillSupport,
  checkAutoIncrementSupport,
} from '@wsComps/TblStructureEditor/utils.js'
import erdHelper from '@/utils/erdHelper'
import {
  CREATE_TBL_TOKEN_MAP,
  COL_ATTR_MAP,
  COL_ATTR_IDX_MAP,
  GENERATED_TYPE_MAP,
} from '@/constants/workspace'
import { useDisplay } from 'vuetify/lib/framework.mjs'

const props = defineProps({
  modelValue: { type: Object, required: true },
  initialData: { type: Object, required: true },
  dim: { type: Object, required: true },
  defTblCharset: { type: String, required: true },
  defTblCollation: { type: String, required: true },
  charsetCollationMap: { type: Object, required: true },
  colKeyCategoryMap: { type: Object, required: true },
})
const emit = defineEmits(['update:modelValue'])

const colAttrs = Object.values(COL_ATTR_MAP)
const COL_SPECS = colAttrs.filter((attr) => attr !== COL_ATTR_MAP.ID)
const DATA_TYPE_ITEMS = getColumnTypes().reduce((acc, item) => {
  // place header first, then all its types and add a divider
  acc = [...acc, { type: 'subheader', value: item.header }, ...item.types, { type: 'divider' }]
  return acc
}, [])
const TXT_FIELDS = [COL_ATTR_MAP.NAME, COL_ATTR_MAP.DEF_EXP, COL_ATTR_MAP.COMMENT]
const BOOL_FIELDS = [
  COL_ATTR_MAP.PK,
  COL_ATTR_MAP.NN,
  COL_ATTR_MAP.UN,
  COL_ATTR_MAP.UQ,
  COL_ATTR_MAP.ZF,
  COL_ATTR_MAP.AI,
]
const CHARSET_COLLATE_FIELDS = [COL_ATTR_MAP.CHARSET, COL_ATTR_MAP.COLLATE]
const GEN_TYPE_ITEMS = Object.values(GENERATED_TYPE_MAP)
const ABBR_HEADER_MAP = {
  [COL_ATTR_MAP.PK]: CREATE_TBL_TOKEN_MAP.primaryKey,
  [COL_ATTR_MAP.NN]: CREATE_TBL_TOKEN_MAP.nn,
  [COL_ATTR_MAP.UN]: CREATE_TBL_TOKEN_MAP.un,
  [COL_ATTR_MAP.UQ]: CREATE_TBL_TOKEN_MAP.uniqueKey,
  [COL_ATTR_MAP.ZF]: CREATE_TBL_TOKEN_MAP.zf,
  [COL_ATTR_MAP.AI]: CREATE_TBL_TOKEN_MAP.ai,
  [COL_ATTR_MAP.GENERATED]: 'GENERATED',
}

const selectedItems = ref([])
const isVertTable = ref(false)
const hiddenColSpecs = ref([])
const headerHeight = ref(0)

const typy = useTypy()
const {
  immutableUpdate,
  uuidv1,
  lodash: { cloneDeep },
} = useHelpers()
const { width: windowWidth } = useDisplay()

const tableMaxHeight = computed(() => props.dim.height - headerHeight.value)
const data = computed({ get: () => props.modelValue, set: (v) => emit('update:modelValue', v) })
const headers = computed(() =>
  colAttrs.map((field) => {
    const h = {
      text: field,
      sortable: false,
      uppercase: true,
      hidden: hiddenColSpecs.value.includes(field),
      useCellSlot: true,
      cellProps: isVertTable.value
        ? null
        : { class: 'px-1 d-inline-flex align-center justify-center' },
    }
    switch (field) {
      case COL_ATTR_MAP.NAME:
      case COL_ATTR_MAP.TYPE:
        h.required = true
        break
      case COL_ATTR_MAP.PK:
      case COL_ATTR_MAP.NN:
      case COL_ATTR_MAP.UN:
      case COL_ATTR_MAP.UQ:
      case COL_ATTR_MAP.ZF:
      case COL_ATTR_MAP.AI:
        if (!isVertTable.value) {
          h.headerProps = { class: 'text-center' }
          h.cellProps.class += ' px-0'
        }
        h.minWidth = 50
        h.maxWidth = 50
        h.resizable = false
        break
      case COL_ATTR_MAP.GENERATED:
        h.width = 144
        h.minWidth = 126
        break
      case COL_ATTR_MAP.ID:
        h.hidden = true
        break
    }
    return h
  })
)
const cols = computed(() => Object.values(data.value.col_map || {}))
const transformedCols = computed(() =>
  cols.value.map((col) => {
    let type = col.data_type
    if (col.data_type_size) type += `(${col.data_type_size})`
    const categories = props.colKeyCategoryMap[col.id] || []

    let uq = false
    if (categories.includes(CREATE_TBL_TOKEN_MAP.uniqueKey)) {
      /**
       * UQ input is a checkbox for a column, so it can't handle composite unique
       * key. Thus ignoring composite unique key.
       */
      uq = erdHelper.isSingleUQ({
        keyCategoryMap: typy(data.value, 'key_category_map').safeObjectOrEmpty,
        colId: col.id,
      })
    }
    return {
      [COL_ATTR_MAP.ID]: col.id,
      [COL_ATTR_MAP.NAME]: col.name,
      [COL_ATTR_MAP.TYPE]: type,
      [COL_ATTR_MAP.PK]: categories.includes(CREATE_TBL_TOKEN_MAP.primaryKey),
      [COL_ATTR_MAP.NN]: col.nn,
      [COL_ATTR_MAP.UN]: col.un,
      [COL_ATTR_MAP.UQ]: uq,
      [COL_ATTR_MAP.ZF]: col.zf,
      [COL_ATTR_MAP.AI]: col.ai,
      [COL_ATTR_MAP.GENERATED]: col.generated ? col.generated : GENERATED_TYPE_MAP.NONE,
      [COL_ATTR_MAP.DEF_EXP]: col.default_exp,
      [COL_ATTR_MAP.CHARSET]: typy(col.charset).safeString,
      [COL_ATTR_MAP.COLLATE]: typy(col.collate).safeString,
      [COL_ATTR_MAP.COMMENT]: typy(col.comment).safeString,
    }
  })
)
const rows = computed(() => transformedCols.value.map((col) => [...Object.values(col)]))
const autoIncrementCol = computed(() => cols.value.find((col) => col.ai))
const initialKeyCategoryMap = computed(
  () => typy(props.initialData, 'key_category_map').safeObjectOrEmpty
)
const stagingKeyCategoryMap = computed(() => typy(data.value, 'key_category_map').safeObjectOrEmpty)
const initialPk = computed(
  () =>
    typy(Object.values(initialKeyCategoryMap.value[CREATE_TBL_TOKEN_MAP.primaryKey] || {}), `[0]`)
      .safeObject
)
const stagingPk = computed(
  () =>
    typy(Object.values(stagingKeyCategoryMap.value[CREATE_TBL_TOKEN_MAP.primaryKey] || {}), `[0]`)
      .safeObject
)

onBeforeMount(() => handleShowColSpecs())

function handleShowColSpecs() {
  if (windowWidth.value >= 1680) hiddenColSpecs.value = []
  else hiddenColSpecs.value = [COL_ATTR_MAP.CHARSET, COL_ATTR_MAP.COLLATE, COL_ATTR_MAP.COMMENT]
}

function deleteSelectedRows() {
  const selectedIds = selectedItems.value.map((row) => row[0])
  let defs = immutableUpdate(data.value, { col_map: { $unset: selectedIds } })
  /* All associated columns in keys also need to be deleted.
   * When a column is deleted, the composite key needs to be modified. i.e.
   * The cols array must remove the selected ids
   * The key is dropped if cols is empty.
   */
  selectedIds.forEach((id) => {
    const categories = props.colKeyCategoryMap[id] || []
    categories.forEach((category) => {
      defs = keySideEffect({ defs, category, colId: id, mode: 'delete' })
    })
  })
  data.value = defs
  selectedItems.value = []
}

function addNewCol() {
  const col = {
    name: 'name',
    data_type: 'CHAR',
    un: false,
    zf: false,
    nn: false,
    charset: undefined,
    collate: undefined,
    generated: undefined,
    ai: false,
    default_exp: CREATE_TBL_TOKEN_MAP.null,
    comment: undefined,
    id: `col_${uuidv1()}`,
  }
  data.value = immutableUpdate(data.value, { col_map: { $merge: { [col.id]: col } } })
}

/**
 * @param {object} param
 * @param {string|boolean} param.value - cell value
 * @param {array} param.rowData - row data
 * @param {string} param.field - field name
 */
function onChangeInput({ value, rowData, field }) {
  let defs = cloneDeep(data.value)
  const { ID, TYPE, PK, NN, UQ, AI, GENERATED, CHARSET } = COL_ATTR_MAP
  const colId = rowData[COL_ATTR_IDX_MAP[ID]]
  const param = { defs, colId, value }
  switch (field) {
    case TYPE:
      defs = onChangeType(param)
      break
    case PK:
    case UQ: {
      if (field === PK) defs = onTogglePk(param)
      defs = keySideEffect({
        defs,
        category: field === PK ? CREATE_TBL_TOKEN_MAP.primaryKey : CREATE_TBL_TOKEN_MAP.uniqueKey,
        colId,
        mode: value ? 'add' : 'drop',
      })
      break
    }
    case NN:
      defs = toggleNotNull(param)
      break
    case AI:
      defs = onToggleAI(param)
      break
    case GENERATED:
      defs = immutableUpdate(defs, {
        col_map: {
          [colId]: {
            [field]: { $set: value },
            default_exp: { $set: '' },
            ai: { $set: false },
            nn: { $set: false },
          },
        },
      })
      break
    case CHARSET:
      defs = setCharset(param)
      break
    default: {
      defs = immutableUpdate(defs, {
        col_map: { [colId]: { [field]: { $set: value || undefined } } },
      })
    }
  }
  data.value = defs
}

function setCharset({ defs, colId, value }) {
  const charset = value === props.defTblCharset ? undefined : value || undefined
  const { defCollation } = props.charsetCollationMap[charset] || {}
  return immutableUpdate(defs, {
    col_map: {
      [colId]: {
        charset: { $set: charset },
        collate: { $set: defCollation }, // also set a default collation
      },
    },
  })
}

function uncheck_UN_ZF_AI({ defs, colId }) {
  return immutableUpdate(defs, {
    col_map: { [colId]: { un: { $set: false }, zf: { $set: false }, ai: { $set: false } } },
  })
}

function setSerialType(param) {
  const { colId, value } = param
  let defs = param.defs
  defs = uncheckAI({ defs, colId, value })
  defs = immutableUpdate(defs, {
    col_map: { [colId]: { un: { $set: true }, nn: { $set: true }, ai: { $set: true } } },
  })
  defs = keySideEffect({ defs, colId, category: CREATE_TBL_TOKEN_MAP.uniqueKey, mode: 'add' })
  return defs
}

function onChangeType(param) {
  const { colId, value } = param
  let defs = immutableUpdate(param.defs, {
    col_map: {
      [colId]: {
        data_type: { $set: value },
        charset: { $set: undefined },
        collate: { $set: undefined },
      },
    },
  })
  if (value === 'SERIAL') defs = setSerialType({ defs, colId, value })
  if (!checkUniqueZeroFillSupport(value) || !checkAutoIncrementSupport(value))
    defs = uncheck_UN_ZF_AI({ defs, colId, value })
  return defs
}

/**
 * This uncheck auto_increment
 * @param {object} defs - current defs
 * @returns {Object} - returns new defs
 */
function uncheckAI(defs) {
  return (defs = immutableUpdate(defs, {
    col_map: { [autoIncrementCol.value.id]: { ai: { $set: false } } },
  }))
}

/**
 * This updates NN cell and `default` cell.
 * @param {object} payload.defs - current defs
 * @param {string} payload.colIdx - column index
 * @param {boolean} payload.value
 * @returns {object} - returns new defs
 */
function toggleNotNull({ defs, colId, value }) {
  const { default_exp = CREATE_TBL_TOKEN_MAP.null } = typy(
    props.initialData,
    `col_map[${colId}]`
  ).safeObjectOrEmpty
  return immutableUpdate(defs, {
    col_map: {
      [colId]: { nn: { $set: value }, default_exp: { $set: value ? undefined : default_exp } },
    },
  })
}

function onToggleAI(param) {
  const { colId, value } = param
  let defs = param.defs
  if (autoIncrementCol.value) defs = uncheckAI(defs)
  defs = immutableUpdate(defs, {
    col_map: { [colId]: { ai: { $set: value }, generated: { $set: undefined } } },
  })
  return toggleNotNull({ defs, colId, value: true })
}

function onTogglePk(param) {
  const { colId, value } = param
  let defs = param.defs
  defs = keySideEffect({
    defs,
    colId,
    category: CREATE_TBL_TOKEN_MAP.primaryKey,
    mode: value ? 'add' : 'drop',
  })
  defs = keySideEffect({ defs, colId, category: CREATE_TBL_TOKEN_MAP.uniqueKey, mode: 'drop' })
  defs = toggleNotNull({ defs, colId, value: true })
  return defs
}

/**
 * @param {object} param.defs - parsed defs
 * @param {string} param.colId - col id
 * @returns {object} new defs object
 */
function updatePk({ defs, colId, mode }) {
  // Get PK object.
  const pkObj = stagingPk.value || {
    cols: [],
    id: initialPk.value ? initialPk.value.id : `key_${uuidv1()}`,
  }

  switch (mode) {
    case 'delete':
    case 'drop': {
      const targetIndex = pkObj.cols.findIndex((c) => c.id === colId)
      if (targetIndex >= 0) pkObj.cols.splice(targetIndex, 1)
      break
    }
    case 'add':
      pkObj.cols.push({ id: colId })
      break
  }

  return immutableUpdate(defs, {
    key_category_map: pkObj.cols.length
      ? { $merge: { [CREATE_TBL_TOKEN_MAP.primaryKey]: { [pkObj.id]: pkObj } } }
      : { $unset: [CREATE_TBL_TOKEN_MAP.primaryKey] },
  })
}

function genKey({ defs, category, colId }) {
  const existingKey = Object.values(initialKeyCategoryMap.value[category] || {}).find((key) => {
    return key.cols.every((col) => col.id === colId)
  })
  if (existingKey) return existingKey
  return erdHelper.genKey({ defs, category, colId })
}

/**
 * @param {object} param.defs - parsed defs
 * @param {string} param.category - uniqueKey, fullTextKey, spatialKey, key or foreignKey
 * @param {string} param.colId - col id
 * @returns {object} new defs object
 */
function updateKey({ defs, category, colId, mode }) {
  let keyMap = cloneDeep(defs.key_category_map[category]) || {}
  switch (mode) {
    case 'drop':
      keyMap = immutableUpdate(keyMap, {
        $unset: Object.values(keyMap).reduce((ids, k) => {
          if (k.cols.every((c) => c.id === colId)) ids.push(k.id)
          return ids
        }, []),
      })
      break
    case 'delete':
      keyMap = immutableUpdate(keyMap, {
        $set: Object.values(cloneDeep(keyMap)).reduce((obj, key) => {
          const targetIndex = key.cols.findIndex((c) => c.id === colId)
          if (targetIndex >= 0) key.cols.splice(targetIndex, 1)
          if (key.cols.length) obj[key.id] = key
          return obj
        }, {}),
      })
      break
    case 'add': {
      const newKey = genKey({ defs, category, colId })
      keyMap = immutableUpdate(keyMap, { $merge: { [newKey.id]: newKey } })
      break
    }
  }
  return immutableUpdate(defs, {
    key_category_map: Object.keys(keyMap).length
      ? { $merge: { [category]: keyMap } }
      : { $unset: [category] },
  })
}

/**
 * @param {object} param.defs - column defs
 * @param {string} param.category - key category
 * @param {string} param.colId - column id
 * @param {string} param.mode - add|drop|delete. delete mode should be used
 * only after dropping a column as it's reserved for handling composite keys.
 * The column in the composite key objects will be deleted.
 */
function keySideEffect({ defs, category, colId, mode }) {
  switch (category) {
    case CREATE_TBL_TOKEN_MAP.primaryKey:
      return updatePk({ defs, colId, mode })
    case CREATE_TBL_TOKEN_MAP.uniqueKey:
    case CREATE_TBL_TOKEN_MAP.fullTextKey:
    case CREATE_TBL_TOKEN_MAP.spatialKey:
    case CREATE_TBL_TOKEN_MAP.key:
    case CREATE_TBL_TOKEN_MAP.foreignKey:
      return updateKey({ defs, category, colId, mode })
    default:
      return defs
  }
}

/**
 * Using for conditionally disable GENERATED input if column is PK
 * https://mariadb.com/kb/en/generated-columns/#index-support
 * @param {array} rowData
 */
function isPkRow(rowData) {
  return typy(rowData, `[${COL_ATTR_IDX_MAP[COL_ATTR_MAP.PK]}]`).safeBoolean
}
</script>

<template>
  <div class="fill-height">
    <TblToolbar
      v-model:isVertTable="isVertTable"
      :selectedItems="selectedItems"
      @get-computed-height="headerHeight = $event"
      @on-delete="deleteSelectedRows"
      @on-add="addNewCol"
    >
      <template #append>
        <FilterList
          v-model="hiddenColSpecs"
          reverse
          activatorClass="ml-2"
          :label="$t('specs')"
          :items="COL_SPECS"
          :activatorProps="{ size: 'small', density: 'comfortable' }"
          :maxHeight="tableMaxHeight - 20"
        />
      </template>
    </TblToolbar>
    <VirtualScrollTbl
      v-model:selectedItems="selectedItems"
      :headers="headers"
      :data="rows"
      :itemHeight="32"
      :maxHeight="tableMaxHeight"
      :boundingWidth="dim.width"
      showSelect
      :isVertTable="isVertTable"
    >
      <template
        v-for="(value, key) in ABBR_HEADER_MAP"
        #[`header-${key}`]="{ data: { header } }"
        :key="key"
      >
        <VTooltip location="top" :disabled="isVertTable">
          <template #activator="{ props }">
            <span class="d-inline-block text-truncate text-uppercase w-100" v-bind="props">
              {{ isVertTable ? value : header.text }}
            </span>
          </template>
          {{ value }}
        </VTooltip>
      </template>
      <template #[`header-${COL_ATTR_MAP.DEF_EXP}`]>
        <span class="text-truncate">DEFAULT/EXPRESSION </span>
      </template>
      <template #[COL_ATTR_MAP.TYPE]="{ data: { rowData, cell } }">
        <DataTypeInput
          :modelValue="cell"
          :items="DATA_TYPE_ITEMS"
          @update:modelValue="
            onChangeInput({ value: $typy($event).safeString, rowData, field: COL_ATTR_MAP.TYPE })
          "
        />
      </template>
      <template v-for="field in BOOL_FIELDS" #[field]="{ data: { rowData, cell } }" :key="field">
        <BoolInput
          :modelValue="cell"
          :rowData="rowData"
          :field="field"
          @update:modelValue="onChangeInput({ value: $event, rowData, field })"
        />
      </template>
      <template #[COL_ATTR_MAP.GENERATED]="{ data: { rowData, cell } }">
        <LazyInput
          :modelValue="cell"
          :items="GEN_TYPE_ITEMS"
          :disabled="isPkRow(rowData)"
          isSelect
          @update:modelValue="
            onChangeInput({ value: $event, rowData, field: COL_ATTR_MAP.GENERATED })
          "
        />
      </template>
      <template
        v-for="field in CHARSET_COLLATE_FIELDS"
        #[field]="{ data: { rowData, cell } }"
        :key="field"
      >
        <CharsetCollateInput
          :modelValue="$typy(cell).isEmpty ? undefined : cell"
          :rowData="rowData"
          :field="field"
          :charsetCollationMap="charsetCollationMap"
          :defTblCharset="defTblCharset"
          :defTblCollation="defTblCollation"
          @update:modelValue="onChangeInput({ value: $event, rowData, field })"
        />
      </template>
      <template v-for="field in TXT_FIELDS" #[field]="{ data: { rowData, cell } }" :key="field">
        <LazyInput
          :modelValue="cell"
          :required="field === COL_ATTR_MAP.NAME"
          @update:modelValue="onChangeInput({ value: $event, rowData, field })"
          @blur="
            onChangeInput({
              value: $typy($event, 'srcElement.value').safeString,
              rowData,
              field,
            })
          "
        />
      </template>
    </VirtualScrollTbl>
  </div>
</template>
