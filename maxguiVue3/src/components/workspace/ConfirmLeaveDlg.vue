<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
defineProps({ confirm: { type: Function, required: true } })
let confirmDelAll = ref(true)
</script>

<template>
  <BaseDlg
    :title="$t('confirmations.leavePage')"
    saveText="confirm"
    minBodyWidth="624px"
    :onSave="async () => await confirm(confirmDelAll)"
  >
    <template #form-body>
      <p data-test="disconnect-info" class="mb-4">{{ $t('info.disconnectAll') }}</p>
      <VCheckboxBtn v-model="confirmDelAll" :label="$t('disconnectAll')" />
    </template>
  </BaseDlg>
</template>
