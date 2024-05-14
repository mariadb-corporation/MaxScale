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
const props = defineProps({ confirm: { type: Function, required: true } })
const confirmDelAll = ref(true)

async function confirmLeave() {
  await props.confirm(confirmDelAll.value)
}
</script>

<template>
  <BaseDlg
    :title="$t('confirmations.leavePage')"
    saveText="confirm"
    minBodyWidth="624px"
    :onSave="confirmLeave"
  >
    <template #form-body>
      <p data-test="disconnect-info" class="mb-4">{{ $t('info.disconnectAll') }}</p>
      <VCheckboxBtn v-model="confirmDelAll" :label="$t('disconnectAll')" />
    </template>
  </BaseDlg>
</template>
