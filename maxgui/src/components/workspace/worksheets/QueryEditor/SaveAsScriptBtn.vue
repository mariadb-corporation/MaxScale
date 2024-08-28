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
import { useSaveFile } from '@/composables/fileSysAccess'
import workspace from '@/composables/workspace'
import { OS_CMD, KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'

const props = defineProps({ queryTab: { type: Object, required: true } })

const { CTRL_SHIFT_S, META_SHIFT_S } = KEYBOARD_SHORTCUT_MAP

const typy = useTypy()
const { handleSaveFileAs, getEditor } = useSaveFile()
workspace.useShortKeyHandler(shortKeyHandler)

const disabled = computed(() => !typy(getEditor(props.queryTab.id), 'sql').safeString)

async function shortKeyHandler(key) {
  if (key === CTRL_SHIFT_S || key === META_SHIFT_S) await save()
}

async function save() {
  if (!disabled.value) await handleSaveFileAs(props.queryTab)
}
</script>

<template>
  <TooltipBtn
    square
    size="small"
    variant="text"
    :disabled="disabled"
    color="primary"
    @click="save()"
  >
    <template #btn-content>
      <VIcon size="20" icon="$mdiContentSaveEditOutline" />
    </template>
    {{ $t('saveScriptAs') }}
    <br />
    {{ OS_CMD }} + SHIFT + S
  </TooltipBtn>
</template>
