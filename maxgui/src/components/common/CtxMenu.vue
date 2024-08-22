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
defineProps({
  items: { type: Array, required: true },
  submenuProps: {
    type: Object,
    default: () => ({
      isSubMenu: false,
      title: '',
      nestedMenuTransition: 'scale-transition',
      nestedMenuOpenDelay: 150,
    }),
  },
})
const emit = defineEmits(['update:modelValue', 'item-click'])
const attrs = useAttrs()

const isSubMenuOpened = ref(false)

// use this to control menu visibility when using activator
const isOpened = computed({
  get: () => attrs.modelValue,
  set: (v) => emit('update:modelValue', v),
})

function emitClickEvent(item) {
  emit('item-click', item)
  toggleMenuVisibility(false)
}

function toggleMenuVisibility(v) {
  if (attrs.activator) isOpened.value = v
  else isSubMenuOpened.value = v
}
</script>
<template>
  <VMenu
    :model-value="attrs.activator ? isOpened : isSubMenuOpened"
    content-class="full-border"
    :close-on-content-click="false"
    min-width="auto"
    @update:modelValue="toggleMenuVisibility"
  >
    <template v-if="!$attrs.activator" #activator="{ props }">
      <VListItem
        v-if="submenuProps.isSubMenu"
        class="cursor--default"
        :title="submenuProps.title"
        link
        :ripple="false"
        v-bind="props"
      >
        <template #append>
          <VIcon size="24" color="primary" icon="$mdiMenuRight" />
        </template>
      </VListItem>
      <div
        v-else
        v-bind="props"
        :class="{ 'pointer-events--none': $attrs.disabled }"
        @click="$attrs.activator ? (isOpened = true) : (isSubMenuOpened = true)"
      >
        <slot name="activator">
          <VListItem link dense :title="submenuProps.title" />
        </slot>
      </div>
    </template>
    <VList>
      <template v-for="(item, index) in items">
        <VDivider v-if="item.divider" :key="index" />
        <CtxMenu
          v-else-if="item.children"
          :key="`CtxMenu-${index}`"
          :items="item.children"
          :submenuProps="{
            isSubMenu: true,
            title: item.title,
            nestedMenuTransition: 'scale-transition',
            nestedMenuOpenDelay: 150,
          }"
          location="right"
          :transition="submenuProps.nestedMenuTransition"
          :open-delay="submenuProps.nestedMenuOpenDelay"
          :open-on-hover="true"
          @item-click="emitClickEvent"
        />
        <VListItem
          v-else
          :key="`VListItem-${index}`"
          dense
          link
          :disabled="item.disabled"
          :title="item.title"
          data-test="child-menu-item"
          @click="emitClickEvent(item)"
        />
      </template>
    </VList>
  </VMenu>
</template>
