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
defineProps({
  items: { type: Array, required: true },
  type: { type: String, required: true },
})
const emit = defineEmits(['get-form-data'])
const { validateRequired } = useValidationRule()

const form = ref({ user: '', password: '', timeout: 30, target: null })
watch(form, (v) => emit('get-form-data', v), { deep: true, immediate: true })
</script>

<template>
  <VRow class="ma-n1">
    <VCol cols="12" class="pa-1">
      <h3 class="text-h3 text-navigation font-weight-light wizard-stage-ctr__title">
        {{ $t('destination') }}
      </h3>
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <label class="label-field text-small-text text-capitalize label--required" for="object-id">
        {{ $t(type, 1) }}
      </label>
      <VSelect
        id="object-id"
        v-model="form.target"
        :items="items"
        item-title="id"
        item-value="id"
        hide-details="auto"
        :placeholder="$t('select', [$t(type, 1)])"
        :rules="[validateRequired]"
        :error-messages="items.length ? '' : $t('noEntityAvailable', [$t(type, 2)])"
      />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <TimeoutInput v-model="form.timeout" />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <UidInput v-model="form.user" name="db-user" />
    </VCol>
    <VCol cols="12" md="6" class="pa-1">
      <PwdInput v-model="form.password" name="db-password" />
    </VCol>
  </VRow>
</template>
