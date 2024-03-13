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
  node: { type: Object, required: true },
  width: { type: Number, default: 290 },
  lineHeight: { type: String, default: '18px' },
  bodyWrapperClass: { type: String, default: '' },
  expandOnMount: { type: Boolean, default: false },
  extraInfoSlides: { type: Array, default: () => [] },
})
const emit = defineEmits(['node-height', 'get-expanded-node'])

const carouselDelimiterHeight = 20

let isExpanded = ref(false)
let defHeight = ref(0)
let activeInfoSlideIdx = ref(0)
let graphNodeRef = ref(null)

const hasExtraInfo = computed(() => Boolean(props.extraInfoSlides.length))
const lineHeightNum = computed(() => Number(props.lineHeight.replace('px', '')))

// Determine maximum number of new lines will be shown
const maxNumOfExtraLines = computed(() => {
  let max = 0
  props.extraInfoSlides.forEach((slide) => {
    let numOfLines = Object.keys(slide).length
    if (numOfLines > max) max = numOfLines
  })
  return max
})

onMounted(() => {
  nextTick(() => {
    computeHeight()
    if (props.expandOnMount && hasExtraInfo.value) toggleExpand(props.node)
  })
})
onBeforeUnmount(() => emit('get-expanded-node', { type: 'destroy', id: props.node.id }))

function computeHeight() {
  defHeight.value = graphNodeRef.value.clientHeight
  emit('node-height', defHeight.value)
}

function getExpandedNodeHeight() {
  return defHeight.value + maxNumOfExtraLines.value * lineHeightNum.value + carouselDelimiterHeight
}

function toggleExpand(node) {
  let height = defHeight.value
  isExpanded.value = !isExpanded.value
  // calculate the new height of the card before it's actually expanded
  if (isExpanded.value) height = getExpandedNodeHeight()
  emit('get-expanded-node', { type: 'update', id: node.id })
  emit('node-height', height)
}
</script>

<template>
  <div ref="graphNodeRef" class="d-flex flex-column">
    <VCard flat border class="node-card fill-height" :width="width - 2">
      <slot name="node-heading" />
      <VDivider />
      <div
        v-if="$slots['node-body'] || hasExtraInfo"
        class="text-navigation d-flex justify-center flex-column px-3 py-1"
        :class="bodyWrapperClass"
      >
        <slot name="node-body" />
        <VExpandTransition>
          <div
            v-if="isExpanded && hasExtraInfo"
            class="node-text--expanded-content mx-n3 mb-n2 px-3 pt-0 pb-2"
          >
            <VCarousel
              v-model="activeInfoSlideIdx"
              class="extra-info-carousel"
              :show-arrows="false"
              hide-delimiter-background
              :height="maxNumOfExtraLines * lineHeightNum + carouselDelimiterHeight"
            >
              <VCarouselItem v-for="(slide, i) in extraInfoSlides" :key="i">
                <div class="mt-4">
                  <div
                    v-for="(value, key) in slide"
                    :key="`${key}`"
                    class="text-no-wrap d-flex"
                    :style="{ lineHeight }"
                  >
                    <span class="mr-2 font-weight-bold text-capitalize">
                      {{ $t(key) }}
                    </span>
                    <GblTooltipActivator
                      :data="{ txt: String(value) }"
                      :debounce="0"
                      activateOnTruncation
                    />
                  </div>
                </div>
              </VCarouselItem>
            </VCarousel>
          </div>
        </VExpandTransition>
      </div>
    </VCard>
    <VBtn
      v-if="hasExtraInfo"
      size="small"
      height="16"
      width="52"
      class="toggle-btn ma-0 pa-0"
      variant="outlined"
      color="card-border-color"
      @click="toggleExpand(node)"
    >
      <VIcon
        class="absolute"
        :class="[isExpanded ? 'rotate-up' : 'rotate-down']"
        size="20"
        color="primary"
        icon="$mdiChevronDown"
      />
    </VBtn>
  </div>
</template>

<style lang="scss" scoped>
.node-card {
  font-size: 12px;
  .node-text--expanded-content {
    background: colors.$separator;
    box-sizing: content-box;
    .extra-info-carousel:deep(.v-carousel__controls) {
      top: 0;
      height: 20px;
      .v-carousel__controls__item {
        background: white;
        width: 32px;
        height: 6px;
        border-radius: 4px;
        &::after {
          top: -7px;
          opacity: 0;
          width: 32px;
          height: 20px;
          pointer-events: all;
        }
        i {
          display: none;
        }
      }
      .v-btn--active {
        background: colors.$electric-ele;
      }
    }
  }
}
.toggle-btn {
  left: 50%;
  transform: translateX(-50%);
  background: white;
}
</style>
