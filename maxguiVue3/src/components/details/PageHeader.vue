<script setup>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import ObjViewHeaderLeft from '@/components/details/ObjViewHeaderLeft.vue'
import IconGroupWrapper from '@/components/details/IconGroupWrapper.vue'

const props = defineProps({
  item: { type: Object, required: true },
  type: { type: String, required: true },
  operationMatrix: { type: Array, default: () => [] }, // 2d array
  showStateIcon: { type: Boolean, default: false },
  defFormType: { type: String, default: '' },
  stateLabel: { type: String, default: '' },
  onCountDone: { type: Function },
  onConfirm: { type: Function, required: true },
})

let confirmDlg = ref({
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
    title: op.text,
    saveText: op.saveText || op.type,
    type: op.type,
    smallInfo: op.info,
    onSave: async () => await props.onConfirm({ op, id: props.item.id }),
  }
}
</script>

<template>
  <div>
    <ObjViewHeaderLeft>
      <template #setting-menu>
        <slot>
          <IconGroupWrapper
            v-for="(operations, i) in operationMatrix"
            :key="i"
            :multiIcons="operations.length > 1"
          >
            <template #default="{ props }">
              <TooltipBtn
                v-for="op in operations"
                :key="op.text"
                :tooltipProps="{ location: 'bottom', transition: 'fade-transition' }"
                variant="text"
                :disabled="op.disabled"
                v-bind="props"
                @click="opHandler(op)"
              >
                <template #btn-content>
                  <VIcon :size="op.iconSize" :icon="op.icon" :color="op.color" />
                </template>
                {{ op.text }}
              </TooltipBtn>
            </template>
          </IconGroupWrapper>
        </slot>
      </template>
    </ObjViewHeaderLeft>
    <portal to="view-header__right">
      <RefreshRate v-if="$typy(onCountDone).isFunction" :onCountDone="onCountDone" />
      <GlobalSearch class="ml-4 d-inline-block" />
      <CreateMxsObj
        class="ml-4 d-inline-block"
        :defFormType="defFormType"
        :defRelationshipObj="{ id: $route.params.id, type }"
      />
    </portal>
    <ConfirmDlg
      :item="item"
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
    <span v-if="stateLabel" class="resource-state text-navigation text-body-2">
      {{ stateLabel }}
    </span>
    <slot name="state-append" />
  </div>
</template>
