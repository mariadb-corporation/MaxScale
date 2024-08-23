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
import { USER_ROLE_MAP, USER_ADMIN_ACTION_MAP } from '@/constants'
const props = defineProps({
  modelValue: { type: Object, required: true },
  type: { type: String, required: true },
})
const emit = defineEmits(['update:modelValue'])

const form = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
</script>

<template>
  <div>
    <UidInput
      v-if="type === USER_ADMIN_ACTION_MAP.ADD"
      v-model="form.id"
      autofocus
      :placeholder="$t('username')"
      class="mb-3"
    />
    <PwdInput
      v-model="form.password"
      :label="$t(type === USER_ADMIN_ACTION_MAP.ADD ? 'password' : 'newPass')"
      :autofocus="type === USER_ADMIN_ACTION_MAP.UPDATE"
      class="mb-3"
    />
    <template v-if="type === USER_ADMIN_ACTION_MAP.ADD">
      <label class="label-field text-small-text label--required" for="role-sel">
        {{ $t('role') }}
      </label>
      <VSelect
        v-model="form.role"
        :items="Object.values(USER_ROLE_MAP)"
        hide-details="auto"
        :rules="[(v) => !!v || $t('errors.requiredField')]"
        id="role-sel"
      />
    </template>
  </div>
</template>
