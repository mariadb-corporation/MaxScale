<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({ confirm: { type: Function, required: true } })

const doNotAskAgain = ref(false)

const store = useStore()
const confirmDelAll = computed({
  get: () => store.state.prefAndStorage.confirm_del_all_before_leave,
  set: (v) => store.commit(`prefAndStorage/SET_CONFIRM_DEL_ALL_BEFORE_LEAVE`, v),
})

async function confirmLeave() {
  await props.confirm(confirmDelAll.value)
  if (doNotAskAgain.value) store.commit(`prefAndStorage/SET_SHOW_CONFIRM_DLG_BEFORE_LEAVE`, false)
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
      <VCheckboxBtn
        v-model="confirmDelAll"
        :label="$t('disconnectAll')"
        class="ml-n2 checkbox"
        data-test="disconnect-all"
      />
    </template>
    <template #action-prepend>
      <VCheckboxBtn
        v-model="doNotAskAgain"
        :label="$t('dontAskMeAgain')"
        inline
        class="ml-n2 checkbox"
        data-test="dont-ask-me-again"
      />
    </template>
  </BaseDlg>
</template>

<style lang="scss" scoped>
.checkbox {
  font-size: 0.875rem;
}
</style>
