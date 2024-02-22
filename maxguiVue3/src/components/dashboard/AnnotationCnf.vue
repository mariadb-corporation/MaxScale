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
const props = defineProps({
  modelValue: { type: Object, required: true },
})
const emit = defineEmits(['update:modelValue', 'on-delete'])

const { t } = useI18n()
const {
  uuidv1,
  lodash: { debounce },
  validateHexColor,
} = useHelpers()

let activeColorField = ref(null)

let data = computed({
  get() {
    return props.modelValue
  },
  set(value) {
    emit('update:modelValue', value)
  },
})

const annotationFields = [
  { dataId: 'content', label: t('label'), type: 'string', isLabel: true },
  { dataId: 'yMax', label: t('maxValue'), type: 'nonNegativeNumber' },
  { dataId: 'yMin', label: t('minValue'), type: 'nonNegativeNumber' },
  {
    dataId: 'backgroundColor',
    label: t('labelBgColor'),
    type: 'color',
    isLabel: true,
  },
  {
    dataId: 'color',
    label: t('labelTxtColor'),
    type: 'color',
    isLabel: true,
  },
  { dataId: 'borderColor', label: t('lineColor'), type: 'color' },
].map((field) => ({ ...field, id: `field_${uuidv1()}` }))

const setColorFieldValue = debounce(({ field, value }) => {
  if (value) {
    const isValidColor = validateHexColor(value)
    if (isValidColor) setFieldValue({ field, value })
  }
}, 300)

function getFieldValue(field) {
  return field.isLabel ? data.value.label[field.dataId] : data.value[field.dataId]
}

function setFieldValue({ field, value }) {
  if (field.isLabel) data.value.label[field.dataId] = value
  else data.value[field.dataId] = value
}

function onClickColorInput(v) {
  activeColorField.value = v
}

function onUpdateColor({ field, value }) {
  setColorFieldValue({ field, value })
}
</script>

<template>
  <VContainer
    fluid
    class="annotation-cnf mxs-color-helper all-border-table-border rounded pa-0 relative"
  >
    <VRow class="mt-3 mr-4 mb-0 ml-2">
      <TooltipBtn
        density="comfortable"
        icon
        variant="text"
        size="small"
        color="error"
        data-test="delete-btn"
        class="delete-btn absolute"
        @click="emit('on-delete')"
      >
        <template #btn-content>
          <VIcon size="10" icon="mxs:close" />
        </template>
        {{ $t('delete') }}
      </TooltipBtn>
      <VCol v-for="field in annotationFields" :key="field.id" cols="4" class="pa-0 pl-2">
        <CnfField
          v-if="!$typy(getFieldValue(field)).isNull"
          :modelValue="getFieldValue(field)"
          :type="field.type"
          :field="field"
          :height="32"
          :data-test="`cnf-field-${field.type}`"
          @update:modelValue="
            field.type === 'color'
              ? onUpdateColor({ field, value: $event })
              : setFieldValue({ field, value: $event })
          "
          @click="field.type === 'color' ? onClickColorInput(field) : null"
        />
      </VCol>
      <VMenu
        v-if="activeColorField"
        :key="`#${activeColorField.id}`"
        :modelValue="Boolean(activeColorField)"
        :activator="`#${activeColorField.id}`"
        :close-on-content-click="false"
      >
        <VColorPicker
          :modes="['hex']"
          mode="hex"
          :modelValue="getFieldValue(activeColorField)"
          @update:modelValue="onUpdateColor({ field: activeColorField, value: $event })"
        />
      </VMenu>
    </VRow>
  </VContainer>
</template>

<style lang="scss">
.annotation-cnf {
  .delete-btn {
    top: 4px;
    right: 4px;
  }
}
</style>
