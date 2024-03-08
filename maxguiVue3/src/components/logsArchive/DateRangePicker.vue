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
/**
 * This component returns TIME_REF_POINTS or ISO date strings
 */
import { isToday, fromUnixTime, getUnixTime } from 'date-fns'
import { TIME_REF_POINTS } from '@/constants'

const props = defineProps({ modelValue: { type: Array, required: true } })
const emit = defineEmits(['update:modelValue'])

const DEF_WIDTH = 160
const CUSTOM_RANGE_WIDTH = 250
const {
  NOW,
  START_OF_TODAY,
  END_OF_YESTERDAY,
  START_OF_YESTERDAY,
  NOW_MINUS_2_DAYS,
  NOW_MINUS_LAST_WEEK,
  NOW_MINUS_LAST_2_WEEKS,
  NOW_MINUS_LAST_MONTH,
} = TIME_REF_POINTS

const { toDateObj, dateFormat } = useHelpers()
const { t } = useI18n()

let isSelectMenuOpened = ref(false)
let selectedItem = ref({})
let isPickerMenuOpened = ref(false)
let items = ref([
  { title: t('today'), value: [START_OF_TODAY, NOW] },
  { title: t('yesterday'), value: [START_OF_YESTERDAY, END_OF_YESTERDAY] },
  { title: t('last2Days'), value: [NOW_MINUS_2_DAYS, NOW] },
  { title: t('lastWeek'), value: [NOW_MINUS_LAST_WEEK, NOW] },
  { title: t('last2Weeks'), value: [NOW_MINUS_LAST_2_WEEKS, NOW] },
  { title: t('lastMonth'), value: [NOW_MINUS_LAST_MONTH, NOW] },
  { title: t('customRange') },
])
let customRanges = ref([])
let width = ref(DEF_WIDTH)
let range = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})

const customRangeTxt = computed(() => {
  if (range.value.length === 2) {
    const formatType = 'yyyy-MM-dd'
    const [from, to] = range.value.map((v) => dateFormat({ value: toDateObj(v), formatType }))
    return `${from} to ${to}`
  }
  return ''
})

onBeforeMount(() => init())

function init() {
  selectedItem.value = items.value[0]
  selectItem(selectedItem.value)
}

/**
 * @param {array} values - ISO date strings or TIME_REF_POINTS
 */
function setRange(values) {
  range.value = values
}

function closeAllMenus() {
  isSelectMenuOpened.value = false
  isPickerMenuOpened.value = false
}

function selectItem(item) {
  setRange(item.value)
  width.value = DEF_WIDTH
}

function onAcceptCustomRange() {
  const [from, to] = [
    getUnixTime(customRanges.value[0]),
    getUnixTime(customRanges.value.at(-1)),
  ].sort()
  /**
   * If `to` value is today, VDatePicker returns date object which starts at the beginning
   * of today instead of now
   */
  if (isToday(fromUnixTime(to))) setRange([from, TIME_REF_POINTS.NOW])
  else setRange([from, to])
  selectedItem.value = items.value.find((item) => item.title === t('customRange'))
  closeAllMenus()
  width.value = CUSTOM_RANGE_WIDTH
}
</script>

<template>
  <VSelect
    v-model="selectedItem"
    v-model:menu="isSelectMenuOpened"
    :items="items"
    return-object
    class="date-range-select"
    :menu-props="{ contentClass: 'full-border', width: '270px' }"
    density="compact"
    hide-details
    :style="{ maxWidth: `${width}px` }"
    @update:modelValue="selectItem"
  >
    <template #prepend-inner>
      <VIcon class="mr-1 color--inherit" size="16" icon="mxs:calendar" />
    </template>
    <template #selection="{ item }">
      <span class="text-no-wrap">
        {{ item.title === $t('customRange') ? customRangeTxt : item.title }}
      </span>
    </template>
    <template #item="{ props }">
      <VListItem v-bind="props">
        <template #title="{ title }">
          <template v-if="title === $t('customRange')">
            <VMenu
              v-model="isPickerMenuOpened"
              :close-on-content-click="false"
              location="bottom"
              content-class="full-border"
            >
              <template #activator="{ props: customRangeActivatorProps }">
                <div
                  v-bind="customRangeActivatorProps"
                  class="d-flex flex-column flex-grow-1 cursor--all-pointer"
                >
                  {{ title }}
                  <VTextField
                    :value="customRangeTxt"
                    class="vuetify-input--override"
                    readonly
                    hide-details
                  >
                    <template #prepend-inner>
                      <VIcon class="mr-1 color--inherit" size="16" icon="mxs:calendar" />
                    </template>
                  </VTextField>
                </div>
              </template>
              <VDatePicker
                v-model="customRanges"
                color="primary"
                multiple="range"
                hide-header
                :elevation="0"
              >
                <template #actions>
                  <VSpacer />
                  <VBtn
                    class="px-4 text-capitalize"
                    color="primary"
                    rounded
                    variant="text"
                    @click="isPickerMenuOpened = false"
                  >
                    {{ $t('cancel') }}
                  </VBtn>
                  <VBtn
                    class="px-7 text-capitalize"
                    color="primary"
                    rounded
                    variant="flat"
                    :disabled="customRanges.length <= 1"
                    @click="onAcceptCustomRange"
                  >
                    {{ $t('ok') }}
                  </VBtn>
                </template>
              </VDatePicker>
            </VMenu>
          </template>
          <template v-else>{{ title }}</template>
        </template>
      </VListItem>
    </template>
  </VSelect>
</template>

<style lang="scss">
.date-range-select {
  .v-field__input {
    padding: 0 0 0 4px !important;
  }
}
</style>
