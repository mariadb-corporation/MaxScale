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
import ScriptEditors from '@wkeComps/DataMigration/ScriptEditors.vue'
import { ETL_STATUS } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  task: { type: Object, required: true },
  data: { type: Array, required: true },
})
const emit = defineEmits(['get-staging-data', 'get-activeRow'])

const typy = useTypy()
const {
  uuidv1,
  lodash: { isEqual, cloneDeep, omit },
} = useHelpers()

const tableWrapperRef = ref(null)
const tableMaxHeight = ref(450)
const selectedItems = ref([])
const tableRows = ref([])

const defDataMap = computed(() =>
  props.data.reduce((map, obj) => {
    const id = uuidv1()
    map[id] = { ...obj, id }
    return map
  }, {})
)
const activeRow = computed({
  get: () => typy(selectedItems.value, '[0]').safeObject,
  set: (v) => {
    if (v) selectedItems.value = [v]
  },
})
const activeRowId = computed(() => typy(activeRow.value, 'id').safeString)
const hasChanged = computed(() => {
  if (activeRowId.value) {
    const initialData = typy(defDataMap.value, `[${activeRowId.value}]`).safeObject
    return !isEqual(initialData, activeRow.value)
  }
  return false
})
const isRunning = computed(() => props.task.status === ETL_STATUS.RUNNING)
// keep all properties except `id` as it's generated for UI purpose
const stagingData = computed(() => tableRows.value.map((row) => omit(row, ['id'])))
const firstErrObj = computed(() => tableRows.value.find((o) => o.error))
const hasScriptFields = computed(
  () =>
    typy(activeRow.value, 'insert').isDefined &&
    typy(activeRow.value, 'create').isDefined &&
    typy(activeRow.value, 'select').isDefined
)
const shouldShowScriptEditors = computed(
  () => activeRow.value && !isRunning.value && hasScriptFields.value
)

watch(stagingData, (v) => emit('get-staging-data', v), {
  deep: true,
  immediate: true,
})
// Should be triggered once unless the default data has changed.
watch(
  defDataMap,
  (v) => {
    tableRows.value = Object.values(cloneDeep(v))
    // Select the first row as active or the first object has error field
    if (tableRows.value.length) selectedItems.value = [firstErrObj.value || tableRows.value[0]]
    nextTick(() => setTblMaxHeight())
  },
  { deep: true, immediate: true }
)
watch(
  activeRow,
  (v) => {
    if (v) emit('get-activeRow', v)
  },
  { deep: true, immediate: true }
)

function setTblMaxHeight() {
  tableMaxHeight.value = typy(tableWrapperRef.value, 'clientHeight').safeNumber || 450
}

function onRowClick(row) {
  if (!isRunning.value) selectedItems.value = [row]
}

// Discard changes on the active row
function discard() {
  activeRow.value = cloneDeep(defDataMap.value[activeRowId.value])
}
</script>

<template>
  <VRow v-resize.quiet="setTblMaxHeight" class="fill-height">
    <VCol cols="12" md="6" class="fill-height py-0">
      <div ref="tableWrapperRef" class="fill-height migration-tbl-wrapper">
        <VDataTableVirtual
          :items="tableRows"
          fixed-header
          :height="tableMaxHeight"
          density="comfortable"
          v-bind="$attrs"
        >
          <template v-for="(_, name) in $slots" #[name]="slotData">
            <slot :name="name" v-bind="slotData" />
          </template>
          <template #item="{ item, columns }">
            <tr
              class="v-data-table__tr"
              :class="{ 'tr--selected': $helpers.lodash.isEqual(activeRow, item) }"
              @click="onRowClick(item)"
            >
              <CustomTblCol
                v-for="(h, i) in columns"
                :key="h.value"
                :value="item[h.value]"
                :name="h.value"
                :autoTruncate="h.autoTruncate"
                class="cursor--pointer"
                v-bind="columns[i].cellProps"
              >
                <template v-for="(_, name) in $slots" #[name]="slotData">
                  <slot :name="name" v-bind="slotData" :item="item" />
                </template>
              </CustomTblCol>
            </tr>
          </template>
          <template #bottom />
        </VDataTableVirtual>
      </div>
    </VCol>
    <VCol cols="12" md="6" class="fill-height py-0">
      <ScriptEditors
        v-if="shouldShowScriptEditors"
        v-model="activeRow"
        :hasChanged="hasChanged"
        @on-discard="discard"
      />
    </VCol>
  </VRow>
</template>

<style lang="scss" scoped>
.migration-tbl-wrapper {
  :deep(tbody) {
    tr {
      &.tr--selected {
        td {
          background: colors.$selected-tr-color;
        }
      }
    }
  }
}
</style>
