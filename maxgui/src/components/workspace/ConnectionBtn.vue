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
const props = defineProps({ activeConn: { type: Object, required: true } })

const typy = useTypy()
const { t } = useI18n()

const connectedServerName = computed(() => typy(props.activeConn, 'meta.name').safeString)
const btnTxt = computed(() => connectedServerName.value || t('connect'))
</script>

<template>
  <TooltipBtn
    variant="text"
    color="primary"
    size="small"
    density="compact"
    class="px-2"
    :tooltipProps="{ disabled: !connectedServerName }"
  >
    <template #btn-content>
      <VIcon size="14" color="primary" icon="$mdiServer" class="mr-1" />
      {{ btnTxt }}
    </template>
    {{ $t('changeConn') }}
  </TooltipBtn>
</template>
