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
  modelValue: { type: Array, required: true },
  label: { type: String, default: '' },
  items: { type: Array, required: true }, // array of strings
  maxHeight: { type: [Number, String], default: 'unset' },
  maxWidth: { type: Number, default: 220 },
  activatorClass: { type: String, default: '' },
  returnIndex: { type: Boolean, default: false },
  changeColorOnActive: { type: Boolean, default: false },
  activatorProps: {
    type: Object,
    default: () => ({ xSmall: true, outlined: true, color: 'primary' }),
  },
  // reverse the logic, modelValue contains unselected items
  reverse: { type: Boolean, default: false },
  hideSelectAll: { type: Boolean, default: false },
  hideSearch: { type: Boolean, default: false },
})
const emit = defineEmits(['update:modelValue'])

const { ciStrIncludes, lodash, immutableUpdate } = useHelpers()
const typy = useTypy()

let filterTxt = ref('')
let isOpened = ref(false)

const itemsList = computed(() =>
  props.items.filter((str) => ciStrIncludes(`${str}`, filterTxt.value))
)

const isAllSelected = computed(() =>
  props.reverse ? props.modelValue.length === 0 : props.modelValue.length === props.items.length
)

const indeterminate = computed(() => {
  if (props.reverse) return !(props.modelValue.length === props.items.length || isAllSelected.value)
  return !(props.modelValue.length === 0 || isAllSelected.value)
})

const activatorClasses = computed(() => {
  let classes = [props.activatorClass, 'text-capitalize']
  if (props.changeColorOnActive) {
    classes.push('change-color-btn mxs-color-helper')
    if (isOpened.value) classes.push('change-color-btn--active text-primary border-primary')
  }
  return classes
})

const btnProps = computed(() => lodash.pickBy(props.activatorProps, (v, key) => key !== 'color'))

const activatorColor = computed(() => typy(props.activatorProps, 'color').safeString || 'primary')

function selectAll() {
  emit(
    'update:modelValue',
    props.returnIndex ? props.items.map((_, i) => i) : lodash.cloneDeep(props.items)
  )
}

function deselectAll() {
  emit('update:modelValue', [])
}
function toggleAll(v) {
  props.reverse === v ? deselectAll() : selectAll()
}
function deselectItem({ item, index }) {
  emit(
    'update:modelValue',
    immutableUpdate(props.modelValue, {
      $splice: [[props.modelValue.indexOf(props.returnIndex ? index : item), 1]],
    })
  )
}

function selectItem({ item, index }) {
  emit(
    'update:modelValue',
    immutableUpdate(props.modelValue, {
      $push: [props.returnIndex ? index : item],
    })
  )
}

function toggleItem({ v, item, index }) {
  props.reverse === v ? deselectItem({ item, index }) : selectItem({ item, index })
}
</script>

<template>
  <VMenu
    v-model="isOpened"
    transition="slide-y-transition"
    content-class="full-border"
    :close-on-content-click="false"
  >
    <template #activator="{ props, isActive }">
      <slot name="activator" :data="{ props, isActive, label }">
        <VBtn
          size="x-small"
          variant="outlined"
          :class="activatorClasses"
          :color="changeColorOnActive ? 'unset' : activatorColor"
          v-bind="{ ...props, ...btnProps }"
        >
          <VIcon size="12" class="mr-1" icon="mxs:filter" />
          {{ label }}
          <VIcon size="20" :class="[isActive ? 'rotate-up' : 'rotate-down']" icon="$mdiMenuDown" />
        </VBtn>
      </slot>
    </template>
    <VList :max-width="maxWidth" :max-height="maxHeight" class="filter-list">
      <template v-if="!hideSearch">
        <VListItem class="py-0 px-0">
          <VTextField
            v-model="filterTxt"
            class="filter-list__search"
            :placeholder="$t('search')"
            hide-details
          />
        </VListItem>
        <VDivider />
      </template>
      <template v-if="!hideSelectAll">
        <VListItem class="py-0 px-2" link>
          <VCheckbox
            :model-value="isAllSelected"
            class="v-checkbox--xs"
            hide-details
            :label="$t('selectAll')"
            :indeterminate="indeterminate"
            @update:modelValue="toggleAll"
          />
        </VListItem>
        <v-divider />
      </template>
      <VListItem v-for="(item, index) in itemsList" :key="`${index}`" class="py-0 px-2" link>
        <VCheckbox
          :model-value="
            reverse
              ? !modelValue.includes(returnIndex ? index : item)
              : modelValue.includes(returnIndex ? index : item)
          "
          class="v-checkbox--xs"
          hide-details
          @update:modelValue="toggleItem({ v: $event, item, index })"
        >
          <template #label>
            <GblTooltipActivator
              v-mxs-highlighter="{ keyword: filterTxt, txt: item }"
              activateOnTruncation
              :data="{ txt: String(item) }"
              :debounce="0"
              :max-width="maxWidth - 52"
            />
          </template>
        </VCheckbox>
      </VListItem>
    </VList>
  </VMenu>
</template>

<style lang="scss" scoped>
.change-color-btn {
  border-color: colors.$text-subtle;
  color: colors.$navigation;
  &:focus::before {
    opacity: 0;
  }
  .v-btn__content .v-icon {
    color: rgba(0, 0, 0, 0.54);
  }
  &--active {
    .v-btn__content .v-icon {
      color: inherit;
    }
  }
}
</style>

<style lang="scss">
.filter-list {
  overflow-y: auto;
  &__search {
    .v-field__outline {
      &__start,
      &__end {
        border: 0;
      }
    }
  }
}
</style>
