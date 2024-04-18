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
import OperationsList from '@/components/details/OperationsList.vue'
import HorizOperationsList from '@/components/details/HorizOperationsList.vue'

const props = defineProps({
  item: { type: Object, required: true },
  type: { type: String, required: true },
  horizOperationList: { type: Boolean, default: true },
  operationMatrix: { type: Array, default: () => [] },
  showStateIcon: { type: Boolean, default: false },
  defFormType: { type: String, default: '' },
  stateLabel: { type: String, default: '' },
  onCountDone: { type: Function },
  onConfirm: { type: Function, required: true },
  onConfirmDlgOpened: { type: Function, default: () => null },
  showGlobalSearch: { type: Boolean, default: true },
})

const store = useStore()
const isAdmin = computed(() => store.getters['users/isAdmin'])
const goBack = useGoBack()

const confirmDlg = ref({
  modelValue: false,
  title: '',
  saveText: '',
  type: '',
  smallInfo: '',
  onSave: () => null,
})

function opHandler(op) {
  confirmDlg.value = {
    modelValue: true,
    title: op.title,
    saveText: op.saveText || op.type,
    type: op.type,
    smallInfo: op.info,
    onSave: async () => await props.onConfirm({ op, id: props.item.id }),
  }
}

watch(
  () => confirmDlg.value.modelValue,
  async (v) => {
    if (v) await props.onConfirmDlgOpened(confirmDlg.value)
  }
)
</script>

<template>
  <div>
    <portal to="view-header__left">
      <div class="d-flex align-center">
        <VBtn class="ml-n4" icon variant="text" density="comfortable" @click="goBack">
          <VIcon
            class="mr-1"
            style="transform: rotate(90deg)"
            size="28"
            color="navigation"
            icon="mxs:arrowDown"
          />
        </VBtn>
        <div class="d-inline-flex align-center">
          <GblTooltipActivator
            :data="{ txt: `${$route.params.id}` }"
            :maxWidth="300"
            activateOnTruncation
          >
            <span class="ml-1 mb-0 text-navigation text-h4 page-title" data-test="page-title">
              <slot name="page-title" :pageId="$route.params.id">
                {{ $route.params.id }}
              </slot>
            </span>
          </GblTooltipActivator>
          <VMenu
            v-if="isAdmin"
            content-class="full-border rounded bg-background"
            transition="slide-y-transition"
            offset="4"
            attach
          >
            <template #activator="{ props }">
              <VBtn
                class="ml-2 gear-btn"
                icon
                variant="text"
                density="comfortable"
                data-test="menu-activator-btn"
                v-bind="props"
              >
                <VIcon size="18" color="primary" icon="mxs:settings" />
              </VBtn>
            </template>
            <div class="d-inline-flex">
              <component
                :is="horizOperationList ? HorizOperationsList : OperationsList"
                :data="operationMatrix"
                :handler="opHandler"
              />
            </div>
          </VMenu>
        </div>
      </div>
    </portal>
    <portal to="view-header__right">
      <RefreshRate v-if="$typy(onCountDone).isFunction" :onCountDone="onCountDone" />
      <GlobalSearch v-if="showGlobalSearch" class="ml-4 d-inline-block" />
      <CreateMxsObj
        class="ml-4 d-inline-block"
        :defFormType="defFormType"
        :defRelationshipObj="{ id: $route.params.id, type }"
      />
    </portal>
    <ConfirmDlg
      :item="item"
      attach
      v-bind="confirmDlg"
      @update:modelValue="confirmDlg.modelValue = $event"
    >
      <template #body-append>
        <slot name="confirm-dlg-body-append" :confirmDlg="confirmDlg" />
      </template>
    </ConfirmDlg>
    <StatusIcon
      v-if="showStateIcon"
      size="16"
      class="ml-6 mr-2"
      :type="type"
      :value="$typy(item, 'attributes.state').safeString"
    />
    <span
      v-if="stateLabel"
      data-test="state-label"
      class="resource-state text-navigation text-body-2"
    >
      {{ stateLabel }}
    </span>
    <slot name="state-append" />
  </div>
</template>

<style lang="scss" scoped>
.page-title {
  line-height: normal;
}
</style>
