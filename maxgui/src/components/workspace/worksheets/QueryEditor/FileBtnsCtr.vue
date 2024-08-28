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
import OpenScriptBtn from '@wkeComps/QueryEditor/OpenScriptBtn.vue'
import SaveScriptBtn from '@wkeComps/QueryEditor/SaveScriptBtn.vue'
import SaveAsScriptBtn from '@wkeComps/QueryEditor/SaveAsScriptBtn.vue'
import { useSaveFile } from '@/composables/fileSysAccess'

const props = defineProps({ queryTab: { type: Object, required: true } })

const store = useStore()
const { checkUnsavedChanges } = useSaveFile()

const hasFileSystemRWAccess = computed(() => store.getters['fileSysAccess/hasFileSystemRWAccess'])
const hasFileSystemReadOnlyAccess = computed(
  () => store.getters['fileSysAccess/hasFileSystemReadOnlyAccess']
)
const hasUnsavedChanges = computed(() => checkUnsavedChanges(props.queryTab))
</script>

<template>
  <div>
    <OpenScriptBtn
      :queryTab="queryTab"
      :hasUnsavedChanges="hasUnsavedChanges"
      :hasFileSystemReadOnlyAccess="hasFileSystemReadOnlyAccess"
    />
    <SaveScriptBtn
      v-if="hasFileSystemRWAccess"
      :queryTab="queryTab"
      :hasUnsavedChanges="hasUnsavedChanges"
    />
    <SaveAsScriptBtn :queryTab="queryTab" />
  </div>
</template>
