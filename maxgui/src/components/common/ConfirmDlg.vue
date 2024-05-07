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
defineProps({
  type: { type: String, default: '' }, //check confirmations in en.json
  item: { type: Object, default: null }, // required when type is defined
  smallInfo: { type: String, default: '' },
})
</script>

<template>
  <BaseDlg>
    <template #form-body>
      <slot name="confirm-text">
        <i18n-t
          v-if="!$typy(item).isNull && type"
          data-test="confirmations-text"
          :keypath="`confirmations.${type}`"
          tag="p"
          scope="global"
          class="pb-4"
        >
          <template #default>
            <b>{{ item.id }}</b>
          </template>
        </i18n-t>
      </slot>
      <slot name="body-prepend"></slot>
      <small v-if="smallInfo"> {{ smallInfo }} </small>
      <slot name="body-append"></slot>
    </template>
    <!-- Pass on all named slots -->
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
  </BaseDlg>
</template>
