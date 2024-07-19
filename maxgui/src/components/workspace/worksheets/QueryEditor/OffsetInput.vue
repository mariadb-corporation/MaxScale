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
const props = defineProps({ modelValue: { type: [Number, String] } })

const emit = defineEmits(['update:modelValue'])

const store = useStore()
const typy = useTypy()
const { t } = useI18n()

const validity = ref(true)
const input = ref(0)
const PREFIX = t('offset')

watch(input, (v) => {
  const res = validate(v)
  validity.value = res === true
  if (typy(res).isString)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [res], type: 'error' })
  if (validity.value && v !== props.modelValue) emit('update:modelValue', v)
})

watch(
  () => props.modelValue,
  (v) => {
    if (v !== input.value) setInputValue()
  },
  { immediate: true }
)

function validate(v) {
  if (typy(v).isEmptyString || (typy(v).isNumber && v >= 0)) return true
  return t('errors.negativeNum', { inputName: PREFIX })
}

function setInputValue() {
  input.value = props.modelValue
}
</script>

<template>
  <VTextField
    v-model.number="input"
    class="offset-input minimized-input"
    :class="{ 'borderless-input': validity }"
    :prefix="PREFIX"
    :min-width="70"
    :rules="[(v) => validate(v)]"
    hide-details
    @keypress="$helpers.preventNonNumericalVal($event)"
  >
    <span class="input-text">{{ input }}</span>
  </VTextField>
</template>

<style lang="scss" scoped>
.offset-input {
  :deep(.v-input__control) {
    .v-field {
      &--focused {
        .input-text {
          opacity: 0;
        }
      }
      &:not(.v-field--focused) {
        input {
          opacity: 0;
        }
      }
      input {
        transition: none;
        flex: 1 1;
        position: absolute;
        left: 0;
        right: 0;
        width: 100%;
        padding-inline: inherit;
      }
    }
  }
}
</style>
