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
defineOptions({ inheritAttrs: false })
defineProps({ result: { type: Object, required: true } })

const store = useStore()
const query_row_limit = computed(() => store.state.prefAndStorage.query_row_limit)
</script>

<template>
  <VTooltip v-if="$typy(result, 'fields').isDefined && !result.complete" location="top">
    <template #activator="{ props }">
      <div
        class="cursor--pointer d-flex align-center text-primary"
        v-bind="{ ...props, ...$attrs }"
      >
        <VIcon size="16" color="primary" class="mr-1" icon="$mdiInformationOutline" />
        {{ $t('incomplete') }}
      </div>
    </template>
    {{
      $t('info.incompleteResultSet', [
        /**
         * statement has limit field only when it is a select statement.
         * However, sql_select_limit actually applies to any statement that
         * returns result sets. In that case, query_row_limit will be used.
         */
        $typy(result, 'statement.limit').safeNumber || query_row_limit,
      ])
    }}
  </VTooltip>
</template>
