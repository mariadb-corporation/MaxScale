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
import { LOG_PRIORITIES } from '@/constants'
import DateRangePicker from '@/components/logsArchive/DateRangePicker.vue'

const { t } = useI18n()
const store = useStore()
const {
  lodash: { cloneDeep },
} = useHelpers()
const { items: allObjIds, fetch: fetchAllObjIds } = useFetchAllObjIds()
const { items: allModuleIds, fetch: fetchModuleIds } = useFetchModuleIds()

let filterAttrs = ref([])
let isSessionIdsInputFocused = ref(false)

const log_source = computed(() => store.state.logs.log_source)

const filterActivatorBtnProps = { density: 'comfortable', variant: 'outlined', color: 'primary' }

onBeforeMount(async () => await init())

async function init() {
  await Promise.all([fetchAllObjIds(), fetchModuleIds()])
  filterAttrs.value = [
    { key: 'session_ids', value: [], props: { height: 28 } },
    {
      key: 'obj_ids',
      value: [],
      props: { items: allObjIds.value, label: t('objects', 2), maxHeight: 500 },
    },
    {
      key: 'module_ids',
      value: [],
      props: { items: allModuleIds.value, label: t('module', 2), maxHeight: 500 },
    },
    {
      key: 'priorities',
      value: [],
      props: {
        items: LOG_PRIORITIES,
        label: t('priorities'),
        hideSelectAll: true,
        hideSearch: true,
      },
    },
    {
      key: 'date_range',
      value: [],
      props: { height: 28 },
    },
  ]
}

function applyFilter() {
  store.commit(
    'logs/SET_LOG_FILTER',
    // deep clone the filterAttrs to break obj reference
    cloneDeep(filterAttrs.value).reduce((res, item) => ((res[item.key] = item.value), res), {})
  )
}
</script>

<template>
  <div class="log-header d-flex flex-row align-center flex-wrap">
    <span class="text-grayed-out d-flex mr-2 align-self-end">
      {{ $t('logSource') }}: {{ log_source }}
    </span>
    <VSpacer />
    <template v-for="item in filterAttrs">
      <template v-if="item.key === 'session_ids'">
        <VCombobox
          :id="item.key"
          :key="item.key"
          v-model="item.value"
          :items="item.value"
          class="mr-2"
          hide-details
          multiple
          density="compact"
          :style="{ maxWidth: '300px' }"
          :hide-no-data="false"
          v-bind="item.props"
          @focus="isSessionIdsInputFocused = true"
          @blur="isSessionIdsInputFocused = false"
        >
          <template #prepend-inner>
            <label class="field__label text-small-text text-no-wrap ma-0" :for="item.key">
              {{ $t('sessionIDs') }}
            </label>
          </template>
          <template #no-data>
            <VListItem class="px-3">
              <i18n-t keypath="info.sessionIDsInputGuide" tag="span" scope="global">
                <kbd class="mx-1"> ENTER </kbd>
              </i18n-t>
            </VListItem>
          </template>
          <template #selection="{ item: selectedItem, index }">
            <template v-if="!isSessionIdsInputFocused">
              <span
                v-if="index === 0"
                class="d-inline-block text-truncate"
                :style="{ maxWidth: '100px' }"
              >
                {{ selectedItem.title }}
              </span>
              <span v-if="index === 1" class="text-caption text-grayed-out">
                (+{{ item.value.length - 1 }} {{ $t('others', item.value.length - 1) }})
              </span>
            </template>
          </template>
        </VCombobox>
      </template>
      <DateRangePicker
        v-else-if="item.key === 'date_range'"
        :key="item.key"
        v-model="item.value"
        v-bind="item.props"
      />
      <template v-else>
        <FilterList
          :key="item.key"
          v-model="item.value"
          activatorClass="mr-2 font-weight-regular"
          changeColorOnActive
          :activatorProps="filterActivatorBtnProps"
          v-bind="item.props"
        />
      </template>
    </template>
    <VBtn
      class="ml-2 text-capitalize font-weight-medium pa-0"
      variant="outlined"
      color="primary"
      density="comfortable"
      @click="applyFilter"
    >
      {{ $t('filter') }}
    </VBtn>
  </div>
</template>
