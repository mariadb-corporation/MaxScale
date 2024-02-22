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
  data: { type: Object, required: true },
  className: { type: [Object, Array, String] },
})

const { t } = useI18n()

const configSyncStatusLabel = computed(() => {
  switch (props.data.status) {
    case 'No configuration changes':
      return props.data.status
    case 'OK':
      return t('configSynced')
    default:
      return t('configSyncFailed')
  }
})
</script>

<template>
  <VMenu
    transition="slide-y-transition"
    :close-on-content-click="false"
    open-on-hover
    offset="0 20"
    content-class="rounded-10 with-arrow with-arrow--top-left no-border shadow-drop"
  >
    <template #activator="{ props }">
      <div class="pointer" :class="className" v-bind="props">
        <StatusIcon size="20" type="config_sync" :value="data.status" />
        <span class="grayed-out-info">{{ configSyncStatusLabel }}</span>
      </div>
    </template>
    <VSheet class="px-6 py-6">
      <p class="text-body-2 font-weight-bold text-navigation text-uppercase">
        {{ $t('configSync') }}
      </p>
      <TreeTable
        class="treeview--config-sync overflow-y-auto rounded"
        :data="data"
        hideHeader
        expandAll
        density="comfortable"
      />
    </VSheet>
  </VMenu>
</template>
<style lang="scss" scoped>
.treeview--config-sync {
  max-height: 50vh;
}
</style>
