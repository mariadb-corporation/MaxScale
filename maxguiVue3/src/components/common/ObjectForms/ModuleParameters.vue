<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component takes modules props to render VSelect component for selecting a module.
When a module is selected, a parameter inputs table will be rendered.
moduleName props is defined to render correct label for select input
*/
defineOptions({ inheritAttrs: false })
const props = defineProps({
  modules: { type: Array, required: true },
  moduleName: { type: String, default: '' },
  hideModuleOpts: { type: Boolean, default: false },
  defModuleId: { type: String, default: '' },
  validate: { type: Function, required: true },
})
const emit = defineEmits(['get-module-id', 'get-changed-params'])
const typy = useTypy()

let selectedModule = ref(null)
let changedParams = ref({})

const paramsInfo = computed(() => typy(selectedModule.value, 'attributes.parameters').safeArray)
const paramsObj = computed(() =>
  paramsInfo.value.reduce((obj, param) => ((obj[param.name] = param.default_value), obj), {})
)

watch(
  () => props.defModuleId,
  (v) => {
    const defModule = props.modules.find((item) => item.id === v)
    if (defModule) selectedModule.value = defModule
  },
  { immediate: true }
)
watch(
  selectedModule,
  (v) => {
    emit('get-module-id', typy(v, 'id').safeString)
  },
  { immediate: true }
)
watch(
  changedParams,
  (v) => {
    emit('get-changed-params', v)
  },
  { immediate: true, deep: true }
)
defineExpose({ selectedModule, paramsObj, paramsInfo })
</script>

<template>
  <template v-if="!hideModuleOpts">
    <label
      data-test="label"
      class="text-capitalize field__label text-small-text d-block"
      for="module-select"
    >
      {{ $t(moduleName, 1) }}
    </label>
    <VSelect
      id="module-select"
      v-model="selectedModule"
      :items="modules"
      item-title="id"
      return-object
      :placeholder="$t('select', 1, { entityName: $t(moduleName, 1) })"
      :rules="[(v) => !!v || $t('errors.requiredInput', { inputName: $t(moduleName, 1) })]"
    />
  </template>
  <ParametersTable
    v-if="paramsInfo.length && !$typy(paramsObj).isEmptyObject"
    :data="paramsObj"
    :paramsInfo="paramsInfo"
    :tableProps="{ showCellBorder: false }"
    creationMode
    showAdvanceToggle
    class="d-flex flex-column"
    titleWrapperClass="mx-n9"
    :parentValidate="validate"
    v-bind="$attrs"
    @changed-params="changedParams = $event"
  />
</template>
