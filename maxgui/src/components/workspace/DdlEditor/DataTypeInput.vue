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
import LazyInput from '@wsComps/DdlEditor/LazyInput.vue'
</script>

<template>
  <LazyInput
    item-props
    item-title="value"
    item-value="value"
    :return-object="false"
    required
    isSelect
    useCustomInput
  >
    <template #default="{ props }">
      <VCombobox v-bind="props">
        <template #item="{ item, props }">
          <VDivider v-if="item.raw.type === 'divider'" />
          <VListSubheader
            v-else-if="item.raw.type === 'subheader'"
            :title="item.value"
            class="font-weight-medium text-caption text-primary"
            :style="{ minHeight: '28px', paddingInlineStart: '8px !important' }"
          />
          <VListItem v-else v-bind="props" />
        </template>
      </VCombobox>
    </template>
  </LazyInput>
</template>
