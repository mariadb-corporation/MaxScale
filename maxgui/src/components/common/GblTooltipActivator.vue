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
import { TOOLTIP_DEBOUNCE } from '@/constants'

const props = defineProps({
  /* If data.activatorID is defined, it uses that as an external
   * activator instead of the componentActivatorID.
   */
  data: {
    type: Object,
    required: true,
    validator: (v) => {
      const keys = Object.keys(v)
      return keys.includes('txt') || keys.includes('collection')
    },
  },
  maxWidth: { type: Number, default: 0 }, // if maxWidth isn't provided, it uses clientWidth
  disabled: { type: Boolean, default: false },
  debounce: { type: Number, default: TOOLTIP_DEBOUNCE },
  activateOnTruncation: { type: Boolean, default: false },
  tag: { type: String, default: 'span' },
  fillHeight: { type: Boolean, default: false },
})

const {
  uuidv1,
  lodash: { debounce: debounceFn, isEqual },
  delay,
} = useHelpers()
const typy = useTypy()
const store = useStore()
const wrapper = ref(null)
const isMouseOvered = ref(false)

const componentActivatorID = `gbl-tooltip-activator-${uuidv1()}`

const gbl_tooltip_data = computed(() => store.state.mxsApp.gbl_tooltip_data)

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

const collectionData = computed(() => typy(props.data, 'collection').safeObjectOrEmpty)
const txt = computed(() => typy(props.data, 'txt').safeString)

watch(
  collectionData,
  (v, oV) => {
    if (isMouseOvered.value && !props.activateOnTruncation && !isEqual(v, oV))
      store.commit('mxsApp/SET_GBL_TOOLTIP_DATA', { ...gbl_tooltip_data.value, collection: v })
  },
  { deep: true }
)

watch(txt, (v, oV) => {
  if (isMouseOvered.value && !props.activateOnTruncation && v !== oV)
    store.commit('mxsApp/SET_GBL_TOOLTIP_DATA', { ...gbl_tooltip_data.value, txt: v })
})

let debouncedMouseOver

function createDebouncedMouseOver() {
  debouncedMouseOver = debounceFn(() => {
    store.commit(
      'mxsApp/SET_GBL_TOOLTIP_DATA',
      props.activateOnTruncation && !isTruncated() ? null : tooltipData.value
    )
    isMouseOvered.value = true
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
    delay(TOOLTIP_DEBOUNCE).then(() => {
      store.commit('mxsApp/SET_GBL_TOOLTIP_DATA', null)
      isMouseOvered.value = false
    })
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
    <slot :value="txt" :activatorID="id"> {{ txt }}</slot>
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
