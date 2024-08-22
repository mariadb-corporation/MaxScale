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
import { OS_CMD, IS_MAC_OS } from '@/constants/workspace'
const store = useStore()

const isTabMoveFocus = computed({
  get: () => store.state.prefAndStorage.tab_moves_focus,
  set: (v) => store.commit('prefAndStorage/SET_TAB_MOVES_FOCUS', v),
})
</script>
<template>
  <TooltipBtn
    v-if="isTabMoveFocus"
    class="text-capitalize"
    variant="text"
    color="primary"
    size="small"
    density="comfortable"
    @click="isTabMoveFocus = !isTabMoveFocus"
  >
    <template #btn-content>
      {{ $t('tabMovesFocus') }}
    </template>
    {{ $t('disableAccessibilityMode') }}
    <br />
    {{ OS_CMD }} {{ IS_MAC_OS ? '+ SHIFT' : '' }} + M
  </TooltipBtn>
</template>
