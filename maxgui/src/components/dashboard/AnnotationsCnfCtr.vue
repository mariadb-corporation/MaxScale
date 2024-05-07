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
import AnnotationCnf from '@/components/dashboard/AnnotationCnf.vue'

const props = defineProps({
  modelValue: { type: Object, required: true },
  cnfType: { type: String, required: true },
})
const emit = defineEmits(['update:modelValue'])

const { uuidv1 } = useHelpers()

let annotations = computed({
  get() {
    return props.modelValue
  },
  set(value) {
    emit('update:modelValue', value)
  },
})

const annotationsLength = computed(() => Object.keys(annotations.value).length)

function genAnnotationCnf() {
  return {
    display: true,
    yMin: 0,
    yMax: 0,
    borderColor: '#EB5757',
    borderWidth: 1,
    label: {
      backgroundColor: '#EB5757',
      color: '#FFFFFF',
      content: '',
      display: true,
      padding: 2,
    },
  }
}

function onDelete(key) {
  delete annotations.value[key]
}

function onAdd() {
  annotations.value[uuidv1()] = genAnnotationCnf()
}
</script>

<template>
  <div class="annotations-cnf-ctr w-100">
    <div class="d-flex align-center mb-4 justify-space-between">
      <p
        data-test="headline"
        class="mb-0 text-body-2 font-weight-bold text-navigation text-uppercase"
      >
        {{ cnfType }}
      </p>
      <VBtn
        v-if="annotationsLength > 0"
        color="primary"
        variant="text"
        size="small"
        data-test="add-btn"
        class="add-btn text-capitalize"
        @click="onAdd()"
      >
        + {{ $t('add') }}
      </VBtn>
    </div>
    <template v-for="(data, key) in annotations">
      <AnnotationCnf
        v-if="!$typy(annotations, key).isEmptyObject"
        :key="key"
        v-model="annotations[key]"
        class="mb-5"
        @on-delete="onDelete(key)"
      />
    </template>
    <VCard
      v-if="annotationsLength === 0"
      :height="100"
      tile
      border
      variant="text"
      class="d-flex align-center justify-center text-primary w-100"
      data-test="add-btn-block"
      @click="onAdd()"
    >
      + {{ $t('add') }}
    </VCard>
  </div>
</template>
