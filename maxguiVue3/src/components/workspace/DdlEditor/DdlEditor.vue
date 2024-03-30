<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TableOpts from '@wsComps/DdlEditor/TableOpts.vue'
import ColDefinitions from '@wsComps/DdlEditor/ColDefinitions.vue'
import TableScriptBuilder from '@/utils/TableScriptBuilder.js'
import erdHelper from '@/utils/erdHelper'
import { WS_EMITTER_KEY, DDL_EDITOR_SPECS } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Object, required: true },
  dim: { type: Object, required: true },
  initialData: { type: Object, required: true },
  isCreating: { type: Boolean, default: false }, // set true to output CREATE TABLE script
  schemas: { type: Array, default: () => [] }, // list of schema names
  onExecute: { type: Function, default: () => null },
  // parsed tables to be looked up in fk-definitions
  lookupTables: { type: Object, default: () => ({}) },
  /**
   * hinted referenced targets is a list of tables that are in
   * the same schema as the table being altered/created
   */
  hintedRefTargets: { type: Array, default: () => [] },
  connData: { type: Object, required: true },
  activeSpec: { type: String, required: true }, //sync
  showApplyBtn: { type: Boolean, default: true },
})
const emit = defineEmits(['update:modelValue', 'update:activeSpec', 'is-form-valid'])

const {
  immutableUpdate,
  lodash: { isEqual, cloneDeep, keyBy },
} = useHelpers()
const typy = useTypy()
const store = useStore()
const { t } = useI18n()

const wsEventListener = inject(WS_EMITTER_KEY)

const TAB_HEIGHT = 25
const TOOLBAR_HEIGHT = 28

const formRef = ref(null)
const tableOptsRef = ref(null)
const isFormValid = ref(true)
const tableOptsHeight = ref(0)
const newLookupTables = ref({})

const charset_collation_map = computed(() => store.state.editorsMem.charset_collation_map)
const engines = computed(() => store.state.editorsMem.engines)
const def_db_charset_map = computed(() => store.state.editorsMem.def_db_charset_map)
const exec_sql_dlg = computed(() => store.state.mxsWorkspace.exec_sql_dlg)

const activeSpecTab = computed({
  get: () => props.activeSpec,
  set: (v) => emit('update:activeSpec', v),
})
const stagingData = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const tblOpts = computed({
  get: () => typy(stagingData.value, 'options').safeObjectOrEmpty,
  set: (v) =>
    (stagingData.value = immutableUpdate(stagingData.value, {
      options: { $set: v },
    })),
})
const defs = computed({
  get: () => typy(stagingData.value, 'defs').safeObjectOrEmpty,
  set: (v) =>
    (stagingData.value = immutableUpdate(stagingData.value, {
      defs: { $set: v },
    })),
})
const initialDefinitions = computed(() => typy(props.initialData, 'defs').safeObjectOrEmpty)
const keyCategoryMap = computed({
  get: () => typy(defs.value, `key_category_map`).safeObjectOrEmpty,
  set: (v) =>
    (stagingData.value = immutableUpdate(stagingData.value, {
      defs: { key_category_map: { $set: v } },
    })),
})
const editorDim = computed(() => ({ ...props.dim, height: props.dim.height - TOOLBAR_HEIGHT }))
const tabDim = computed(() => ({
  width: editorDim.value.width - 24,
  height: editorDim.value.height - tableOptsHeight.value - TAB_HEIGHT - 16,
}))
const hasChanged = computed(() => !isEqual(props.initialData, stagingData.value))
const allLookupTables = computed(() =>
  Object.values({ ...props.lookupTables, ...newLookupTables.value })
)
const knownTargets = computed(() => erdHelper.genRefTargets(allLookupTables.value))
// keyed by text as the genRefTargets method assign qualified name of the table to text field
const knownTargetMap = computed(() => keyBy(knownTargets.value, 'text'))
const refTargets = computed(() => [
  ...knownTargets.value,
  ...props.hintedRefTargets.filter((item) => !knownTargetMap.value[item.text]),
])
const tablesColNameMap = computed(() => erdHelper.createTablesColNameMap(allLookupTables.value))
const colKeyCategoryMap = computed(() => erdHelper.genColKeyTypeMap(defs.value.key_category_map))
/**
 * @returns {Object.<string, object.<object>>}  e.g. { "tbl_123": {} }
 */
const allTableColMap = computed(() =>
  allLookupTables.value.reduce((res, tbl) => {
    res[tbl.id] = typy(tbl, 'defs.col_map').safeObjectOrEmpty
    return res
  }, {})
)

watch(isFormValid, (v) => emit('is-form-valid', v))
watch(wsEventListener, async (v) => {
  if (props.showApplyBtn) await shortKeyHandler(v.event)
})
onMounted(() => setTableOptsHeight())

function setTableOptsHeight() {
  if (tableOptsRef.value) {
    const { height } = tableOptsRef.value.$el.getBoundingClientRect()
    tableOptsHeight.value = height
  }
}

function onRevert() {
  stagingData.value = cloneDeep(props.initialData)
}

async function onApply() {
  await typy(formRef.value, 'validate').safeFunction()
  if (isFormValid.value) {
    const refTargetMap = keyBy(refTargets.value, 'id')
    const builder = new TableScriptBuilder({
      initialData: props.initialData,
      stagingData: stagingData.value,
      refTargetMap,
      tablesColNameMap: tablesColNameMap.value,
      options: { isCreating: props.isCreating },
    })
    store.commit('mxsWorkspace/SET_EXEC_SQL_DLG', {
      ...exec_sql_dlg.value,
      is_opened: true,
      sql: builder.build(),
      on_exec: props.onExecute,
      after_cancel: () =>
        store.commit('mxsWorkspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, result: null }),
    })
  } else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredInputs')],
      type: 'error',
    })
}

async function shortKeyHandler(key) {
  if (hasChanged.value && (key === 'ctrl-enter' || key === 'mac-cmd-enter')) await onApply()
}
</script>

<template>
  <VForm ref="formRef" v-model="isFormValid">
    <div
      class="d-flex align-center mxs-color-helper border-bottom-table-border"
      :style="{ height: `${TOOLBAR_HEIGHT}px` }"
    >
      <TooltipBtn
        v-if="!isCreating"
        class="toolbar-square-btn"
        variant="text"
        color="primary"
        :disabled="!hasChanged"
        @click="onRevert"
      >
        <template #btn-content>
          <VIcon size="16" icon="mxs:reload" />
        </template>
        {{ $t('apply') }}
      </TooltipBtn>
      <TooltipBtn
        v-if="showApplyBtn"
        class="toolbar-square-btn"
        variant="text"
        color="primary"
        :disabled="!hasChanged"
        @click="onApply"
      >
        <template #btn-content>
          <VIcon size="16" icon="mxs:running" />
        </template>
        {{ $t('apply') }}
      </TooltipBtn>
      <slot name="toolbar-append" :formRef="formRef" />
    </div>
    <TableOpts
      ref="tableOptsRef"
      v-model="tblOpts"
      :engines="engines"
      :charsetCollationMap="charset_collation_map"
      :defDbCharset="$typy(def_db_charset_map, `${$typy(tblOpts, 'schema').safeString}`).safeString"
      :isCreating="isCreating"
      :schemas="schemas"
      @after-expand="setTableOptsHeight"
      @after-collapse="setTableOptsHeight"
    />
    <VTabs v-model="activeSpecTab" :height="TAB_HEIGHT">
      <VTab v-for="spec of DDL_EDITOR_SPECS" :key="spec" :value="spec" class="text-primary">
        {{ $t(spec) }}
      </VTab>
    </VTabs>
    <div class="px-3 py-2" :style="{ height: `${tabDim.height + 16}px` }">
      <VSlideXTransition>
        <KeepAlive>
          <ColDefinitions
            v-if="activeSpecTab === DDL_EDITOR_SPECS.COLUMNS"
            v-model="defs"
            :charsetCollationMap="charset_collation_map"
            :initialData="initialDefinitions"
            :dim="tabDim"
            :defTblCharset="$typy(tblOpts, 'charset').safeString"
            :defTblCollation="$typy(tblOpts, 'collation').safeString"
            :colKeyCategoryMap="colKeyCategoryMap"
          />
        </KeepAlive>
      </VSlideXTransition>
    </div>
  </VForm>
</template>
