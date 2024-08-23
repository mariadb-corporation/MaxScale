<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import LazyInput from '@wsComps/TblStructureEditor/LazyInput.vue'
import { FK_EDITOR_ATTR_MAP } from '@/constants/workspace'

const props = defineProps({
  field: { type: String, required: true },
  referencingColOptions: { type: Array, required: true },
  refColOpts: { type: Array, required: true },
})
const attrs = useAttrs()

const {
  lodash: { keyBy },
} = useHelpers()
const typy = useTypy()
const { t } = useI18n()
const { COLS, REF_COLS } = FK_EDITOR_ATTR_MAP

const items = computed(() => {
  switch (props.field) {
    case COLS:
      return props.referencingColOptions
    case REF_COLS:
      return props.refColOpts
    default:
      return []
  }
})
const itemMap = computed(() => keyBy(items.value, 'id'))
const selectionText = computed(() => {
  const selectedNames = attrs.modelValue.map((id) => typy(itemMap.value, `[${id}]text`).safeString)
  if (selectedNames.length > 1)
    return `${selectedNames[0]} (+${selectedNames.length - 1} ${t('others')})`
  return typy(selectedNames, '[0]').safeString
})

function getColOrder(col) {
  return attrs.modelValue.findIndex((id) => id === col.id) + 1
}
</script>

<template>
  <LazyInput
    isSelect
    :items="items"
    item-title="text"
    item-value="id"
    :selectionText="selectionText"
    multiple
    required
    :rules="[(v) => Boolean(v.length)]"
    useCustomInput
  >
    <template #default="{ props }">
      <VSelect v-bind="props">
        <template #selection="{ item, index }">
          <template v-if="index === 0"> {{ item.raw.text }}</template>
          <template v-else-if="index === 1">
            &nbsp;(+{{ $attrs.modelValue.length - 1 }} {{ $t('others') }})
          </template>
        </template>
        <template #item="{ item, props }">
          <VListItem v-bind="props">
            <template #prepend>
              <span
                class="col-order pr-1"
                :class="{ 'col-order--visible': getColOrder(item.raw) > 0 }"
              >
                {{ getColOrder(item.raw) }}
              </span>
            </template>
            <template #title="{ title }">
              <VIcon
                class="mr-3"
                :icon="
                  $attrs.modelValue.includes(item.raw.id)
                    ? '$mdiCheckboxMarked'
                    : '$mdiCheckboxBlankOutline'
                "
              />
              {{ title }}
            </template>
            <template #append>
              <span class="ma-0 ml-auto pl-2 label-field text-small-text">
                {{ item.raw.type }}
              </span>
            </template>
          </VListItem>
        </template>
      </VSelect>
    </template>
  </LazyInput>
</template>

<style lang="scss" scoped>
.col-order {
  width: 20px;
  visibility: hidden;
  &--visible {
    visibility: visible;
  }
}
</style>
