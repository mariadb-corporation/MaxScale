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
import QueryConn from '@wsModels/QueryConn'
import { TOOLTIP_DEBOUNCE } from '@/constants'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  hasLoading: { type: Boolean, default: true },
  extraHeaders: { type: Array, default: () => [] },
})
const emit = defineEmits(['confirm-kill', 'on-update'])

const store = useStore()
const loading = useLoading()
const typy = useTypy()
const itemsPerPageOptions = [5, 10, 20, 50, 100]

const pagination = ref({ page: 0, itemsPerPage: 20 })
const confDlg = ref({ isOpened: false, item: null })
const hoveredCell = ref(null)

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
    {
      title: 'Queries',
      value: 'queries',
      cellProps: { class: 'pa-0' },
      headerProps: commonCellProps,
    },
    ...props.extraHeaders,
  ]
  if (isAdmin.value)
    items.push({
      title: '',
      value: 'action',
      cellProps: { class: 'pa-0' },
      headerProps: { class: 'pa-0' },
    })
  return items
})
const workspaceConnThreadIds = computed(() =>
  QueryConn.all().map((conn) => typy(conn, 'attributes.thread_id').safeNumber)
)

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
    :no-data-text="$t('noEntity', [$t('sessions', 2)])"
    :items-per-page-options="itemsPerPageOptions"
    :headers="headers"
    class="sessions-table"
    v-bind="$attrs"
  >
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
    <template #item="{ item, columns }">
      <tr class="v-data-table__tr">
        <CustomTblCol
          v-for="(h, i) in columns"
          :key="h.value"
          :value="item[h.value]"
          :name="h.value"
          :autoTruncate="h.autoTruncate"
          v-bind="columns[i].cellProps"
        >
          <template v-for="(_, name) in $slots" #[name]="slotData">
            <slot :name="name" v-bind="slotData" />
          </template>
          <template #[`item.user`]="{ value }">
            <div class="d-inline-flex">
              {{ value }}
              <GblTooltipActivator
                v-if="workspaceConnThreadIds.includes(Number(item.id))"
                :data="{ txt: $t('info.connCreatedByWs') }"
                fillHeight
                class="cursor--pointer"
              >
                <VIcon class="ml-1" icon="mxs:workspace" size="18" color="navigation" />
              </GblTooltipActivator>
            </div>
          </template>
          <template #[`item.memory`]="{ value }">
            <div
              :id="`memory-cell-${item.id}`"
              class="d-flex pl-6 cursor--pointer fill-height align-center"
              @mouseover="hoveredCell = { data: value, activatorID: `memory-cell-${item.id}` }"
            >
              {{ value.total }}
            </div>
          </template>
          <template #[`item.queries`]="{ value }">
            <div
              :id="`queries-cell-${item.id}`"
              class="d-flex pl-6 cursor--pointer fill-height align-center"
              @mouseover="hoveredCell = { data: value, activatorID: `queries-cell-${item.id}` }"
            >
              {{ $t('queries', { n: value.length }) }}
            </div>
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
                color="error"
                @click="onKillSession(item)"
              >
                <template #btn-content>
                  <VIcon size="18" icon="mxs:unlink" :style="{ transition: 'none' }" />
                </template>
                {{ $t('killSessions') }}
              </TooltipBtn>
            </div>
          </template>
        </CustomTblCol>
      </tr>
    </template>
  </VDataTableServer>
  <ConfirmDlg
    v-model="confDlg.isOpened"
    :title="$t('killSessions')"
    saveText="kill"
    minBodyWidth="512px"
    :onSave="confirmKill"
  >
    <template v-if="confDlg.item" #body-prepend>
      <p class="confirmations-text">{{ $t(`confirmations.killSessions`) }}</p>
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
  <VMenu
    v-if="$typy(hoveredCell, 'activatorID').isDefined"
    :key="hoveredCell.activatorID"
    open-on-hover
    :close-on-content-click="false"
    :activator="`#${hoveredCell.activatorID}`"
    location="right"
    transition="slide-y-transition"
    content-class="shadow-drop text-navigation pa-4 text-body-2 bg-background rounded-10"
    :open-delay="TOOLTIP_DEBOUNCE"
  >
    <TreeTable
      v-for="(dataItem, index) in $typy(hoveredCell.data).isArray
        ? hoveredCell.data
        : [hoveredCell.data]"
      :key="index"
      :data="dataItem"
      hideHeader
      expandAll
      density="compact"
      class="text-body-2 my-2"
    />
  </VMenu>
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
