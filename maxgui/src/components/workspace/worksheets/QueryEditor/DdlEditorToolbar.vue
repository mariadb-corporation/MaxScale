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
import DisableTabMovesFocusBtn from '@wkeComps/QueryEditor/DisableTabMovesFocusBtn.vue'
import FileBtnsCtr from '@wkeComps/QueryEditor/FileBtnsCtr.vue'
import { WS_KEY, WS_EDITOR_KEY } from '@/constants/injectionKeys'
import { OS_CMD, KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'

const props = defineProps({
  height: { type: Number, required: true },
  queryTab: { type: Object, required: true },
  queryTabTmp: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  sql: { type: String, required: true },
})

const emit = defineEmits(['execute', 'stop'])

const { META_D, CTRL_ENTER, META_ENTER, CTRL_SHIFT_C, META_SHIFT_C, CTRL_M } = KEYBOARD_SHORTCUT_MAP

const store = useStore()
const typy = useTypy()

const wsEvtListener = inject(WS_KEY)
const editorEvtListener = inject(WS_EDITOR_KEY)

const tab_moves_focus = computed(() => store.state.prefAndStorage.tab_moves_focus)
const isExecuting = computed(() => typy(props.queryTabTmp, 'ddl_result.is_loading').safeBoolean)
const hasKillFlag = computed(() => typy(props.queryTabTmp, 'has_kill_flag').safeBoolean)
const isQueryTabConnBusy = computed(() => typy(props.queryTabConn, 'is_busy').safeBoolean)
const isExeBtnDisabled = computed(
  () => !props.sql || !props.queryTabConn.id || isQueryTabConnBusy.value || isExecuting.value
)
let unwatch_wsEventListener, unwatch_editorKeypress

onActivated(() => {
  unwatch_wsEventListener = watch(wsEvtListener, (v) => shortKeyHandler(v.name))
  unwatch_editorKeypress = watch(editorEvtListener, (v) => shortKeyHandler(v.name))
})

onDeactivated(() => cleanUp())
onBeforeUnmount(() => cleanUp())

function cleanUp() {
  unwatch_wsEventListener()
  unwatch_editorKeypress()
}

async function shortKeyHandler(key) {
  switch (key) {
    case CTRL_ENTER:
    case META_ENTER:
      if (!isExeBtnDisabled.value) emit('execute')
      break
    case CTRL_SHIFT_C:
    case META_SHIFT_C:
      if (isExecuting.value) emit('stop')
      break
    case CTRL_M:
    case META_D:
      store.commit('prefAndStorage/SET_TAB_MOVES_FOCUS', !tab_moves_focus.value)
      break
  }
}
</script>

<template>
  <div class="d-flex align-center border-bottom--table-border" :style="{ height: `${height}px` }">
    <TooltipBtn
      v-if="isExecuting"
      square
      size="small"
      variant="text"
      color="primary"
      :disabled="hasKillFlag"
      data-test="stop-btn"
      @click="emit('stop')"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:stopped" />
      </template>
      {{ $t('stopStatements', 1) }}
      <br />
      {{ OS_CMD }} + SHIFT + C
    </TooltipBtn>
    <TooltipBtn
      v-else
      square
      size="small"
      variant="text"
      color="primary"
      :disabled="isExeBtnDisabled"
      data-test="execute-btn"
      @click="emit('execute')"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:running" />
      </template>
      {{ $t('exeStmt') }}
      <br />
      {{ OS_CMD }} + ENTER
    </TooltipBtn>
    <FileBtnsCtr :queryTab="queryTab" />
    <VSpacer />
    <DisableTabMovesFocusBtn v-if="tab_moves_focus" class="mr-1" />
  </div>
</template>
