<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const store = useStore()
const snackbar_message = computed(() => store.state.mxsApp.snackbar_message)
const isOpened = computed(() => snackbar_message.value.status)
const type = computed(() => snackbar_message.value.type)
const text = computed(() => snackbar_message.value.text)

function close() {
  store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { ...snackbar_message, status: false })
}

const icon = computed(() => {
  switch (type.value) {
    case 'info':
      return 'mxs:statusInfo'
    case 'error':
      return 'mxs:alertError'
    case 'warning':
      return 'mxs:alertWarning'
    default:
      return 'mxs:alertSuccess'
  }
})

const iconColor = computed(() => {
  switch (type.value) {
    case 'info':
    case 'error':
    case 'warning':
      return 'white'
    default:
      return 'success'
  }
})
</script>

<template>
  <VSnackbar
    :model-value="isOpened"
    :color="type"
    :key="text"
    :timeout="6000"
    multi-line
    location="bottom right"
  >
    <div style="width: 100%" class="d-inline-flex align-center justify-center">
      <VIcon class="mr-4" size="22" :color="iconColor" :icon="icon" />
      <div class="d-flex flex-column text-white">
        <span v-for="(item, i) in text" :key="i"> {{ item }} </span>
      </div>
      <VSpacer />
      <VBtn density="comfortable" variant="text" icon class="ml-4" color="white" @click="close">
        <VIcon size="24" icon="$mdiClose" />
      </VBtn>
    </div>
  </VSnackbar>
</template>
