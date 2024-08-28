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

const props = defineProps({
  queryTab: { type: Object, required: true },
  hasUnsavedChanges: { type: Boolean, required: true },
})

const { CTRL_S, META_S } = KEYBOARD_SHORTCUT_MAP

const { isFileHandleValid, saveFileToDisk } = useSaveFile()
workspace.useShortKeyListener({ handler: shortKeyHandler })

const disabled = computed(() => !props.hasUnsavedChanges || !isFileHandleValid(props.queryTab.id))

async function shortKeyHandler(key) {
  if (key === CTRL_S || key === META_S) await save()
}

async function save() {
  if (!disabled.value) await saveFileToDisk(props.queryTab)
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
      <VIcon size="20" icon="$mdiContentSaveOutline" />
    </template>
    {{ $t('saveScript') }}
    <br />
    {{ OS_CMD }} + S
  </TooltipBtn>
</template>
