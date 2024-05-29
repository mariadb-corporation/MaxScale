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
import { TOOLTIP_DEBOUNCE } from '@/constants'

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
  debounce: { type: Number, default: TOOLTIP_DEBOUNCE },
  activateOnTruncation: { type: Boolean, default: false },
  tag: { type: String, default: 'span' },
  fillHeight: { type: Boolean, default: false },
})

const helper = useHelpers()
const typy = useTypy()
const store = useStore()
const wrapper = ref(null)

const componentActivatorID = `gbl-tooltip-activator-${helper.uuidv1()}`

const id = computed(() => (props.data.activatorID ? '' : componentActivatorID))
const interactive = computed(() => typy(props.data, 'interactive').safeBoolean)

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

let debouncedMouseOver

function createDebouncedMouseOver() {
  debouncedMouseOver = helper.lodash.debounce(() => {
    let tooltipValue = tooltipData.value
    if (props.activateOnTruncation && !isTruncated()) tooltipValue = null
    store.commit('mxsApp/SET_GBL_TOOLTIP_DATA', tooltipValue)
  }, props.debounce)
}

function mouseover() {
  createDebouncedMouseOver()
  debouncedMouseOver()
}

function mouseleave() {
  debouncedMouseOver.cancel()
  debouncedMouseOver = undefined
  if (!interactive.value)
    helper.delay(TOOLTIP_DEBOUNCE).then(() => store.commit('mxsApp/SET_GBL_TOOLTIP_DATA', null))
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
    :id="id"
    ref="wrapper"
    class="text-truncate"
    :class="[fillHeight ? 'wrapper--align-middle d-block fill-height' : 'd-inline-block']"
    :style="style"
    v-on="disabled ? {} : { mouseover, mouseleave }"
  >
    <slot :value="data.txt" :activatorID="id"> {{ data.txt }}</slot>
  </component>
</template>

<style lang="scss" scoped>
.wrapper--align-middle:before {
  content: '';
  display: inline-block;
  vertical-align: middle;
  height: 100%;
}
</style>
