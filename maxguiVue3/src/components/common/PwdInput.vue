<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
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
const isPwdVisible = ref(false)
</script>

<template>
  <LabelField
    :required="true"
    :type="isPwdVisible ? 'text' : 'password'"
    autocomplete="new-password"
    :label="$t('password')"
  >
    <template #append-inner>
      <VIcon
        :icon="isPwdVisible ? '$mdiEyeOff' : '$mdiEye'"
        size="20"
        @click="isPwdVisible = !isPwdVisible"
      />
    </template>
  </LabelField>
</template>
