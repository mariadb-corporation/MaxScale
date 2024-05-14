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
import { DDL_EDITOR_EMITTER_KEY } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })

const props = defineProps({
  selectionText: { type: String, default: '' }, // custom selection text
  isSelect: { type: Boolean, default: false },
  required: { type: Boolean, default: false },
  useCustomInput: { type: Boolean, default: false },
})

const ddlEditorEventListener = inject(DDL_EDITOR_EMITTER_KEY)

const attrs = useAttrs()
const { uuidv1 } = useHelpers()
const typy = useTypy()

const isVisible = ref(false)
const error = ref(false)
const id = `input-${uuidv1()}`

const readonlyInputClass = computed(() => [
  typy(attrs, 'disabled').isDefined && attrs.disabled ? `lazy-input--disabled` : '',
  error.value ? `lazy-input--error` : '',
])
const inputProps = computed(() => ({
  hideDetails: true,
  rules: [(v) => (props.required ? !!v : true)],
  autofocus: !error.value,
  disabled: attrs.disabled,
  error: error.value,
  density: 'compact',
  'onUpdate:focused': (v) => {
    if (!v) setInputVisibility(false)
  },
  ...(props.isSelect ? { menu: error.value ? false : isVisible.value } : {}),
  ...attrs,
}))

watch(ddlEditorEventListener, (v) => {
  if (v.event === 'validate') {
    validate()
    v.payload.callback(!error.value)
  }
})

function setInputVisibility(v) {
  isVisible.value = v
  if (!v) validate()
}

function validate() {
  if (props.required) {
    error.value = Boolean(
      props.isSelect && typy(attrs, 'multiple').isDefined
        ? !attrs.modelValue.length
        : !attrs.modelValue
    )
    if (error.value) isVisible.value = true
  }
}
</script>

<template>
  <template v-if="isVisible">
    <slot v-if="useCustomInput" :props="inputProps" />
    <VSelect v-else-if="isSelect" v-bind="inputProps" />
    <DebouncedTextField v-else v-bind="inputProps" autocomplete="off" />
  </template>
  <div
    v-else
    class="lazy-input w-100 pr-3 rounded pos--relative d-flex align-center justify-space-between"
    :class="readonlyInputClass"
  >
    <input
      :id="id"
      type="text"
      :value="selectionText || $attrs.modelValue"
      :disabled="$attrs.disabled"
      :placeholder="$attrs.placeholder"
      autocomplete="off"
      class="v-field__input px-0 text-truncate"
      @click.stop
      @focus="setInputVisibility(true)"
    />
    <VIcon
      v-if="isSelect"
      class="menu-down-icon ml-1 mr-auto"
      :disabled="$attrs.disabled"
      :color="error ? 'error' : 'navigation'"
      icon="$dropdown"
      :size="24"
      @click="setInputVisibility(true)"
    />
  </div>
</template>

<style lang="scss" scoped>
.lazy-input {
  height: 28px;
  border: thin solid colors.$text-subtle;
  max-width: 100%;
  min-width: 0;
  padding-left: 11.3px;
  input {
    width: 100%;
    line-height: 28px;
  }
  &--disabled {
    opacity: 0.5;
    input {
      color: rgba(0, 0, 0, 0.38);
    }
  }
  &--error {
    border-color: colors.$error;
  }
  .menu-down-icon {
    opacity: 0.6;
  }
}
</style>
