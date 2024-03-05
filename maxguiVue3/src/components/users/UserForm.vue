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
import { USER_ROLES, USER_ADMIN_ACTIONS } from '@/constants'
const props = defineProps({
  modelValue: { type: Object, required: true },
  type: { type: String, required: true },
})
const emit = defineEmits(['update:modelValue'])
const { t } = useI18n()

let form = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})

function rule(inputName) {
  return [(val) => !!val || t('errors.requiredInput', { inputName })]
}
</script>

<template>
  <div>
    <UidInput
      v-if="type === USER_ADMIN_ACTIONS.ADD"
      v-model="form.id"
      autofocus
      :placeholder="$t('username')"
      class="mb-3"
    />
    <PwdInput
      v-model="form.password"
      :label="$t(type === USER_ADMIN_ACTIONS.ADD ? 'password' : 'newPass')"
      :autofocus="type === USER_ADMIN_ACTIONS.UPDATE"
      class="mb-3"
    />
    <template v-if="type === USER_ADMIN_ACTIONS.ADD">
      <label class="field__label text-small-text label-required" for="role-sel">
        {{ $t('role') }}
      </label>
      <VSelect
        v-model="form.role"
        :items="Object.values(USER_ROLES)"
        hide-details="auto"
        :rules="rule($t('role'))"
        id="role-sel"
      />
    </template>
  </div>
</template>
