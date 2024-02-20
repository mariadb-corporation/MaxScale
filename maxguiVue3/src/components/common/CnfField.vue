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
field: {
  id: string,
  label: string,
  icon?: string,
  iconColor?: string,
  enumValues?: array, enum items for type enum
  href?: string, external link.
  isVariable?: boolean, if true, label won't be capitalized
  suffix?: string
}
type: positiveNumber, nonNegativeNumber, boolean, enum, string, color
*/

defineOptions({
  inheritAttrs: false,
})
const attrs = useAttrs()
const props = defineProps({
  field: { type: Object, required: true },
  type: { type: String, required: true },
  required: { type: Boolean, default: true },
})
const emit = defineEmits(['tooltip'])

const typy = useTypy()
const { preventNonNumericalVal, validateHexColor } = useHelpers()
const { t } = useI18n()

const customInputSlotName = computed(() => `${props.field.id}-input`)
const isColorInput = computed(() => props.type === 'color')

function validateNumber({ v, inputName }) {
  if (typy(v).isEmptyString) return t('errors.requiredInput', { inputName })
  if (props.type === 'positiveNumber') {
    if (v <= 0) return t('errors.largerThanZero', { inputName })
    if (v > 0) return true
  } else if (v >= 0) return true
  return t('errors.negativeNum')
}
function showInfoTooltip(data) {
  if (!props.field.href) emit('tooltip', data)
}
function rmInfoTooltip() {
  if (!props.field.href) emit('tooltip', undefined)
}
function onIconClick() {
  if (props.field.href) window.open(props.field.href, '_blank', 'noopener,noreferrer')
}
function validateColor(v) {
  return validateHexColor(v) || t('errors.hexColor')
}
</script>

<template>
  <div class="cnf-field" :class="{ 'pb-4': type !== 'boolean' }">
    <label
      v-if="type !== 'boolean'"
      class="field__label text-small-text label-required"
      :class="{ 'field__label-variable': field.isVariable }"
      :for="field.id"
    >
      {{ field.label }}
    </label>
    <VIcon
      v-if="field.icon && type !== 'boolean'"
      size="14"
      :color="field.iconColor"
      class="ml-1 mb-1 pointer"
      :icon="field.icon"
      @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
      @mouseleave="rmInfoTooltip"
      @click="onIconClick"
    />
    <slot v-if="$slots[customInputSlotName]" :name="customInputSlotName" />
    <template v-else>
      <VSelect
        v-if="type === 'enum'"
        :id="field.id"
        v-bind="$attrs"
        :items="field.enumValues"
        hide-details="auto"
      >
        <template #selection="{ item }">
          <span class="text-capitalize">{{ $t(item.title, 1) }}</span>
        </template>
        <template #item="{ props }">
          <VListItem v-bind="props">
            <template #title="{ title }">
              <span class="text-capitalize"> {{ $t(title, 1) }}</span>
            </template>
          </VListItem>
        </template>
      </VSelect>
      <VCheckbox
        v-else-if="type === 'boolean'"
        :id="field.id"
        v-bind="$attrs"
        hide-details="auto"
        density="compact"
      >
        <template #label>
          {{ field.label }}
          <VIcon
            v-if="field.icon"
            class="ml-1 material-icons-outlined pointer"
            size="16"
            :color="field.iconColor"
            :icon="field.icon"
            @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
            @mouseleave="rmInfoTooltip"
            @click="onIconClick"
          />
        </template>
      </VCheckbox>
      <VTextField
        v-else-if="type === 'positiveNumber' || type === 'nonNegativeNumber'"
        :id="field.id"
        v-bind="$attrs"
        type="number"
        hide-details="auto"
        :required="required"
        :suffix="field.suffix"
        :rules="[(v) => validateNumber({ v, inputName: field.label })]"
        @keypress="preventNonNumericalVal($event)"
      />
      <VTextField
        v-else-if="type === 'string' || isColorInput"
        :id="field.id"
        v-bind="$attrs"
        hide-details="auto"
        :required="required"
        :max-length="isColorInput ? 7 : -1"
        :rules="[
          (v) =>
            isColorInput
              ? validateColor(v)
              : !!v || $t('errors.requiredInput', { inputName: field.label }),
        ]"
      >
        <template v-if="isColorInput" #append-inner>
          <span
            v-if="attrs.modelValue"
            :style="{ backgroundColor: attrs.modelValue }"
            class="pa-2 rounded mxs-color-helper all-border-table-border"
          />
        </template>
      </VTextField>
    </template>
  </div>
</template>

<style lang="scss" scoped>
.field__label-variable {
  &::first-letter {
    text-transform: lowercase;
  }
}
</style>
