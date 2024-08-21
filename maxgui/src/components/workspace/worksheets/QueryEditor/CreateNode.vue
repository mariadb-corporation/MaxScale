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
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { NODE_TYPE_MAP, NODE_CTX_TYPE_MAP } from '@/constants/workspace'
import { SNACKBAR_TYPE_MAP } from '@/constants'

const props = defineProps({
  activeDb: { type: String, required: true },
  disabled: { type: Boolean, required: true },
})
const emit = defineEmits(['create-node'])

const store = useStore()
const { t } = useI18n()
const { unquoteIdentifier } = useHelpers()

const { SCHEMA, TBL, VIEW, SP, FN } = NODE_TYPE_MAP
const { CREATE } = NODE_CTX_TYPE_MAP

const NODES_TO_BE_CREATED = [SCHEMA, TBL, VIEW, SP, FN]
const CREATE_OPTIONS = NODES_TO_BE_CREATED.map((type) =>
  schemaNodeHelper.genNodeOpt({
    title: schemaNodeHelper.capitalizeNodeType(type),
    type: CREATE,
    targetNodeType: type,
  })
)

/**
 * @param {Object} node - node
 * @param {Object} opt - context menu option
 */
function optionHandler(opt) {
  const targetNodeType = opt.targetNodeType
  if (targetNodeType === SCHEMA) emit('create-node', { type: targetNodeType })
  else if (props.activeDb)
    emit('create-node', {
      type: targetNodeType,
      parentNameData: { [SCHEMA]: unquoteIdentifier(props.activeDb) },
    })
  else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredActiveSchema')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
}
</script>

<template>
  <CtxMenu :items="CREATE_OPTIONS" :disabled="disabled" @item-click="optionHandler">
    <template #activator="{ props }">
      <TooltipBtn
        icon
        density="compact"
        variant="text"
        :disabled="disabled"
        color="primary"
        data-test="create-node"
        v-bind="props"
      >
        <template #btn-content>
          <VIcon size="18" icon="$mdiPlus" />
        </template>
        {{ $t('createNode') }}
      </TooltipBtn>
    </template>
  </CtxMenu>
</template>
