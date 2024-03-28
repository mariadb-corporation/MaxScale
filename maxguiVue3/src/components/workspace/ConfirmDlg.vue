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
const store = useStore()
const confirm_dlg = computed(() => store.state.mxsWorkspace.confirm_dlg)
const isOpened = computed({
  get: () => confirm_dlg.value.is_opened,
  set: (v) => store.commit('mxsWorkspace/SET_CONFIRM_DLG', { ...confirm_dlg.value, is_opened: v }),
})
const i18n_interpolation = computed(() => confirm_dlg.value.i18n_interpolation)
</script>

<template>
  <BaseDlg
    v-model="isOpened"
    minBodyWidth="768px"
    :title="confirm_dlg.title"
    :closeImmediate="true"
    :lazyValidation="false"
    :onSave="confirm_dlg.on_save"
    :cancelText="confirm_dlg.cancel_text"
    :saveText="confirm_dlg.save_text"
    @after-cancel="confirm_dlg.after_cancel"
  >
    <template #form-body>
      <i18n-t :keypath="i18n_interpolation.keypath" tag="p" scope="global" class="pb-4">
        <b v-for="(value, i) in i18n_interpolation.values" :key="i">{{ value }}</b>
      </i18n-t>
    </template>
  </BaseDlg>
</template>
