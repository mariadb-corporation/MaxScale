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
import MemoryCell from '@/components/common/SessionsTable/MemoryCell.vue'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  hasLoading: { type: Boolean, default: true },
  extraHeaders: { type: Array, default: () => [] },
})
const emit = defineEmits(['confirm-kill', 'on-update'])

const store = useStore()
const loading = useLoading()
const itemsPerPageOptions = [5, 10, 20, 50, 100]

let pagination = ref({ page: 0, itemsPerPage: 20 })
let confDlg = ref({ isOpened: false, item: null })

const pagination_config = computed(() => store.state.sessions.pagination_config)
const isAdmin = computed(() => store.getters['users/isAdmin'])
const commonCellProps = { class: 'pr-0 pl-6' }
const headers = computed(() => {
  let items = [
    { title: 'ID', value: 'id', cellProps: commonCellProps, headerProps: commonCellProps },
    { title: 'Client', value: 'user', cellProps: commonCellProps, headerProps: commonCellProps },
    {
      title: 'Connected',
      value: 'connected',
      cellProps: { ...commonCellProps, style: { maxWidth: '150px' } },
      headerProps: commonCellProps,
    },
    { title: 'IDLE (s)', value: 'idle', cellProps: commonCellProps, headerProps: commonCellProps },
    {
      title: 'Memory',
      value: 'memory',
      cellProps: { class: 'pa-0' },
      headerProps: commonCellProps,
    },
    {
      title: 'I/O activity',
      value: 'io_activity',
      cellProps: { ...commonCellProps, style: { maxWidth: '100px' } },
      headerProps: commonCellProps,
    },
    ...props.extraHeaders,
  ]
  if (isAdmin.value)
    items.push({
      title: '',
      value: 'action',
      cellProps: { class: 'pa-0', style: { maxWidth: '32px' } },
      headerProps: { class: 'pa-0' },
    })
  return items
})

watch(
  () => confDlg.value.isOpened,
  (v) => {
    if (!v) confDlg.value.item = null
  }
)
onBeforeMount(() => {
  pagination.value = {
    page: pagination_config.value.page + 1,
    itemsPerPage: pagination_config.value.itemsPerPage,
  }
})

watch(
  pagination,
  (v) => {
    if (
      v.page - 1 !== pagination_config.value.page ||
      v.itemsPerPage !== pagination_config.value.itemsPerPage
    ) {
      // API page starts at 0, vuetify page starts at 1
      store.commit('sessions/SET_PAGINATION_CONFIG', {
        page: v.page - 1,
        itemsPerPage: v.itemsPerPage,
      })
      emit('on-update')
    }
  },
  { deep: true }
)

function onKillSession(item) {
  confDlg.value = { isOpened: true, item }
}
function confirmKill() {
  emit('confirm-kill', confDlg.value.item.id)
}
</script>

<template>
  <VDataTableServer
    v-model:page="pagination.page"
    v-model:items-per-page="pagination.itemsPerPage"
    :loading="hasLoading ? loading : false"
    :no-data-text="$t('noEntity', { entityName: $t('sessions', 2) })"
    :items-per-page-options="itemsPerPageOptions"
    :headers="headers"
    class="sessions-table"
    v-bind="$attrs"
  >
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
    <template #item="{ item, columns, index: rowIdx }">
      <tr class="v-data-table__tr">
        <CustomTblCol
          v-for="(h, i) in columns"
          :id="`row-${rowIdx}-cell-${i}`"
          :key="h.value"
          :value="item[h.value]"
          :name="h.value"
          :autoTruncate="h.autoTruncate"
          v-bind="columns[i].cellProps"
        >
          <template v-for="(_, name) in $slots" #[name]="slotData">
            <slot :name="name" v-bind="slotData" />
          </template>
          <template #[`item.memory`]="{ value }">
            <MemoryCell :data="value" class="pl-6" />
          </template>
          <template #[`item.action`]>
            <div
              class="kill-session-btn"
              :class="{
                'kill-session-btn--visible': $helpers.lodash.isEqual(confDlg.item, item),
              }"
            >
              <TooltipBtn
                density="comfortable"
                icon
                variant="text"
                size="small"
                color="error"
                @click="onKillSession(item)"
              >
                <template #btn-content>
                  <VIcon size="18" icon="mxs:unlink" :style="{ transition: 'none' }" />
                </template>
                {{ $t('killSession') }}
              </TooltipBtn>
            </div>
          </template>
        </CustomTblCol>
      </tr>
    </template>
  </VDataTableServer>
  <ConfirmDlg
    v-model="confDlg.isOpened"
    :title="$t('killSession')"
    saveText="kill"
    minBodyWidth="512px"
    :onSave="confirmKill"
  >
    <template v-if="confDlg.item" #body-prepend>
      <p class="confirmations-text">{{ $t(`confirmations.killSession`) }}</p>
      <table class="tbl-code pa-4">
        <tr v-for="(v, key) in confDlg.item" :key="key">
          <td>
            <b>{{ key }}</b>
          </td>
          <td>{{ v }}</td>
        </tr>
      </table>
    </template>
  </ConfirmDlg>
</template>

<style lang="scss" scoped>
.sessions-table {
  tbody tr {
    .kill-session-btn {
      visibility: hidden;
      &--visible {
        visibility: visible;
      }
    }
  }
  tbody tr:hover {
    .kill-session-btn {
      visibility: visible;
    }
  }
}
</style>
