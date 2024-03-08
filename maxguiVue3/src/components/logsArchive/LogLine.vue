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
const props = defineProps({
  item: {
    type: Object,
    default: () => ({ attributes: { timestamp: '', priority: '', message: '' }, id: '' }),
  },
})

const typy = useTypy()

const attributes = computed(() => typy(props.item, 'attributes').safeObjectOrEmpty)

function logPriorityColorClasses(level) {
  return `text-${level} ${level === 'alert' ? 'font-weight-bold' : ''}`
}

function logLevelNbspGen(level) {
  switch (level) {
    case 'error':
    case 'alert':
    case 'debug':
      return '&nbsp;&nbsp;'
    case 'notice':
      return '&nbsp;'
    case 'info':
      return '&nbsp;&nbsp;&nbsp;'
    case 'warning':
      return ''
  }
}
</script>

<template>
  <code
    v-if="item"
    class="d-block mariadb-code-style text-wrap"
    :class="logPriorityColorClasses(attributes.priority)"
  >
    <span class="text-grayed-out">{{ attributes.timestamp }}&nbsp;&nbsp;</span>
    <span class="log-level d-inline-block">
      <StatusIcon size="13" type="log" :value="attributes.priority" />
      <span class="tk-azo-sans-web">&nbsp;</span>
      <span>{{ attributes.priority }}</span>
    </span>
    <span v-html="logLevelNbspGen(attributes.priority)" />
    <span class="log-level-divider text-code-color">:</span>
    <span>&nbsp;</span>
    <span>{{ attributes.message }}</span>
  </code>
</template>
