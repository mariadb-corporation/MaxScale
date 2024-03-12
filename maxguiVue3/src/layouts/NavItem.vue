<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({
  item: { type: Object, required: true },
  rail: { type: Boolean, required: true },
  currentPath: { type: String, required: true },
})
const { t } = useI18n()

const label = computed(() =>
  props.item.label === 'dashboards' ? t(`${props.item.label}`, 1) : t(`${props.item.label}`, 2)
)

const isActive = computed(() => {
  const rootPath = props.item.path.split('/:')[0]
  return props.currentPath.startsWith(rootPath)
})
</script>

<template>
  <VListItem class="nav-item-ctr my-2 pointer" :class="{ 'px-1 justify-center': rail }">
    <div
      class="nav-item d-flex align-center px-2"
      :class="{
        'justify-center': rail,
        'nav-item--active': isActive,
      }"
    >
      <VIcon
        class="nav-item__icon"
        :size="item.meta.size"
        :color="isActive ? 'blue-azure' : 'navigation'"
        :icon="item.meta.icon"
      />
      <span v-show="!rail" class="nav-item__label ml-4 text-capitalize text-no-wrap">
        {{ label }}
      </span>
    </div>
  </VListItem>
</template>

<style lang="scss" scoped>
.nav-item-ctr {
  height: 52px;
  .v-list-item__content {
    width: unset !important;
  }
  &:hover {
    background: #eefafd !important;
  }
  .nav-item {
    height: 40px;
    &__icon {
      height: 100%;
      margin: 0;
      align-items: center;
      justify-content: center;
    }
    &__label {
      color: colors.$navigation;
      font-size: 1rem;
    }
    &--active {
      background-color: colors.$separator;
      border-radius: 8px;
      .nav-item__label {
        color: colors.$blue-azure;
      }
    }
  }
}
</style>
