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
const props = defineProps({
  /* If data.activatorID is defined, it uses that as an external
   * activator instead of the componentActivatorID.
   */
  data: {
    type: Object,
    required: true,
    validator: (value) => Object.keys(value).includes('txt'),
  },
  maxWidth: { type: Number, default: 0 }, // if maxWidth isn't provided, it uses clientWidth
  disabled: { type: Boolean, default: false },
  debounce: { type: Number, default: 150 },
  activateOnTruncation: { type: Boolean, default: false },
  tag: { type: String, default: 'span' },
})

const helper = useHelpers()
const typy = useTypy()
const store = useStore()
const wrapper = ref(null)

const componentActivatorID = `gbl-itr-tooltip-activator${helper.uuidv1()}`

const style = computed(() =>
  props.maxWidth ? { maxWidth: `${props.maxWidth}px` } : { maxWidth: '100%' }
)

const tooltipData = computed(() =>
  props.data.activatorID
    ? props.data
    : {
        ...props.data,
        activatorID: componentActivatorID,
      }
)
let debouncedMouseEnter
onMounted(() => {
  debouncedMouseEnter = helper.lodash.debounce(() => {
    let tooltipValue = tooltipData.value
    if (props.activateOnTruncation && !isTruncated()) tooltipValue = null
    store.commit('mxsApp/SET_GBL_TOOLTIP_DATA', tooltipValue)
  }, props.debounce)
})

function mouseenter() {
  debouncedMouseEnter()
}

function isTruncated() {
  return (
    typy(wrapper, 'value.scrollWidth').safeNumber > typy(wrapper, 'value.clientWidth').safeNumber
  )
}
</script>

<template>
  <component
    :is="tag"
    :id="data.activatorID || componentActivatorID"
    ref="wrapper"
    class="d-inline-block text-truncate"
    :style="style"
    v-on="disabled ? {} : { mouseenter }"
  >
    <slot> {{ $typy(data, 'txt').safeString }}</slot>
  </component>
</template>
