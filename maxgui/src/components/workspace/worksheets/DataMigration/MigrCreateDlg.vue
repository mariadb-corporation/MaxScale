<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MIGR_DLG_TYPES } from '@/constants/workspace'

const props = defineProps({ handleSave: { type: Function, required: true } })

const store = useStore()
const { t } = useI18n()

let name = ref('')

const migr_dlg = computed(() => store.state.workspace.migr_dlg)

const isOpened = computed({
  get: () => {
    const { type, is_opened } = migr_dlg.value
    return type === MIGR_DLG_TYPES.CREATE ? is_opened : false
  },
  set: (v) => store.commit('workspace/SET_MIGR_DLG', { ...migr_dlg.value, is_opened: v }),
})

watch(isOpened, (v) => {
  if (v) name.value = t('newMigration').toUpperCase()
})

function onSave() {
  props.handleSave(name.value)
}
</script>

<template>
  <BaseDlg
    v-model="isOpened"
    :onSave="onSave"
    :title="$t('newMigration')"
    minBodyWidth="512px"
    :saveText="migr_dlg.type"
  >
    <template #form-body>
      <slot name="form-prepend" />
      <LabelField v-model="name" :label="$t('name')" required />
    </template>
  </BaseDlg>
</template>
