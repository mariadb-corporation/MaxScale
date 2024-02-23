<script>
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
export default {
  props: {
    items: { type: Array, required: true },
    submenuProps: {
      type: Object,
      default: () => ({
        isSubMenu: false,
        text: '',
        nestedMenuTransition: 'scale-transition',
        nestedMenuOpenDelay: 150,
      }),
    },
  },
  data() {
    return {
      menuOpen: false,
    }
  },
  computed: {
    // use this to control menu visibility when using activator
    isOpened: {
      get() {
        return this.$attrs.modelValue
      },
      set(v) {
        this.$emit('update:modelValue', v)
      },
    },
  },
  methods: {
    emitClickEvent(item) {
      this.$emit('item-click', item)
      if (this.$attrs.activator) this.isOpened = false
      else this.menuOpen = false
    },
  },
}
</script>
<template>
  <VMenu
    :model-value="$attrs.activator ? isOpened : menuOpen"
    content-class="full-border"
    :close-on-content-click="false"
    min-width="auto"
  >
    <template v-if="!$attrs.activator" #activator="{ props }">
      <VListItem
        v-if="submenuProps.isSubMenu"
        class="cursor-default text-text"
        :title="submenuProps.text"
        link
        :ripple="false"
        v-bind="props"
      >
        <template #append>
          <VIcon size="24" color="primary" icon="$mdiMenuRight" />
        </template>
      </VListItem>
      <div v-else v-bind="props" @click="$attrs.activator ? (isOpened = true) : (menuOpen = true)">
        <slot name="activator">
          <VListItem link dense class="text-text" :title="submenuProps.text" />
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
            text: item.text,
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
          :title="item.text"
          class="text-text"
          data-test="child-menu-item"
          @click="emitClickEvent(item)"
        />
      </template>
    </VList>
  </VMenu>
</template>
