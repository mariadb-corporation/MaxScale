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
import TableOpts from '@wsComps/DdlEditor/TableOpts.vue'
import ColDefinitions from '@wsComps/DdlEditor/ColDefinitions.vue'
import FkDefinitionsWrapper from '@wsComps/DdlEditor/FkDefinitionsWrapper.vue'
import IndexDefinitions from '@wsComps/DdlEditor/IndexDefinitions.vue'
import TableScriptBuilder from '@/utils/TableScriptBuilder.js'
import erdHelper from '@/utils/erdHelper'
import { DDL_EDITOR_SPECS } from '@/constants/workspace'
import { WS_KEY, WS_DDL_EDITOR_KEY } from '@/constants/injectionKeys'

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

const wsEvtListener = inject(WS_KEY)
const dispatchEvt = useEventDispatcher(WS_DDL_EDITOR_KEY)

const TAB_HEIGHT = 25
const TOOLBAR_HEIGHT = 28

const formRef = ref(null)
const tableOptsRef = ref(null)
const formValidity = ref(true)
const tableOptsHeight = ref(0)
const newLookupTables = ref({})

const charset_collation_map = computed(() => store.state.ddlEditor.charset_collation_map)
const engines = computed(() => store.state.ddlEditor.engines)
const def_db_charset_map = computed(() => store.state.ddlEditor.def_db_charset_map)
const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)

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

watch(formValidity, (v) => emit('is-form-valid', Boolean(v)))
watch(wsEvtListener, async (v) => {
  if (props.showApplyBtn) await shortKeyHandler(v.name)
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

async function validate() {
  // Emit the validate event so that the LazyInput components can perform self-validation.
  let areLazyInputValueValid = true
  dispatchEvt('validate', {
    callback: (v) => {
      if (!v) areLazyInputValueValid = false
    },
  })
  await typy(formRef.value, 'validate').safeFunction()
  formValidity.value = formValidity.value && areLazyInputValueValid
  return formValidity.value && areLazyInputValueValid
}

async function onApply() {
  if (await validate()) {
    const refTargetMap = keyBy(refTargets.value, 'id')
    const builder = new TableScriptBuilder({
      initialData: props.initialData,
      stagingData: stagingData.value,
      refTargetMap,
      tablesColNameMap: tablesColNameMap.value,
      options: { isCreating: props.isCreating },
    })
    store.commit('workspace/SET_EXEC_SQL_DLG', {
      ...exec_sql_dlg.value,
      is_opened: true,
      sql: builder.build(),
      on_exec: props.onExecute,
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
defineExpose({ validate })
</script>

<template>
  <VForm ref="formRef" v-model="formValidity">
    <div
      class="d-flex align-center border-bottom--table-border"
      :style="{ height: `${TOOLBAR_HEIGHT}px` }"
    >
      <TooltipBtn
        v-if="!isCreating"
        square
        size="small"
        variant="text"
        color="primary"
        :disabled="!hasChanged"
        data-test="revert-btn"
        @click="onRevert"
      >
        <template #btn-content>
          <VIcon size="16" icon="mxs:reload" />
        </template>
        {{ $t('revertChanges') }}
      </TooltipBtn>
      <TooltipBtn
        v-if="showApplyBtn"
        square
        size="small"
        variant="text"
        color="primary"
        :disabled="!hasChanged"
        data-test="apply-btn"
        @click="onApply"
      >
        <template #btn-content>
          <VIcon size="16" icon="mxs:running" />
        </template>
        {{ $t('apply') }}
      </TooltipBtn>
      <slot name="toolbar-append" />
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
          <template v-else-if="activeSpecTab === DDL_EDITOR_SPECS.FK">
            <FkDefinitionsWrapper
              :engine="$typy(tblOpts, 'engine').safeString"
              v-model="keyCategoryMap"
              v-model:newLookupTables="newLookupTables"
              :lookupTables="lookupTables"
              :allLookupTables="allLookupTables"
              :allTableColMap="allTableColMap"
              :refTargets="refTargets"
              :tablesColNameMap="tablesColNameMap"
              :tableId="stagingData.id"
              :dim="tabDim"
              :connData="connData"
              :charsetCollationMap="charset_collation_map"
            />
          </template>
          <IndexDefinitions
            v-else-if="activeSpecTab === DDL_EDITOR_SPECS.INDEXES"
            v-model="keyCategoryMap"
            :tableColNameMap="$typy(tablesColNameMap[stagingData.id]).safeObjectOrEmpty"
            :dim="tabDim"
            :tableColMap="$typy(allTableColMap[stagingData.id]).safeObjectOrEmpty"
          />
        </KeepAlive>
      </VSlideXTransition>
    </div>
  </VForm>
</template>
