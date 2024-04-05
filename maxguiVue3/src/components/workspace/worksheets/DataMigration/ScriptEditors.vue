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
import ScriptEditor from '@wkeComps/DataMigration/ScriptEditor.vue'

const props = defineProps({
  /**
   * @property {string} select
   * @property {string} create
   * @property {string} insert
   */
  modelValue: { type: Object, required: true },
  hasChanged: { type: Boolean, required: true },
})
const emit = defineEmits(['update:modelValue', 'on-discard'])

const stagingRow = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const isInErrState = computed(() => {
  const { select, create, insert } = stagingRow.value
  return !select || !create || !insert
})
</script>

<template>
  <div class="fill-height d-flex flex-column">
    <div class="script-container d-flex flex-column fill-height">
      <ScriptEditor
        v-model="stagingRow.select"
        class="select-script"
        data-test="select-script"
        :label="$t('retrieveDataFromSrc')"
      />
      <ScriptEditor
        v-model="stagingRow.create"
        class="create-script"
        data-test="create-script"
        :label="$t('createObjInDest')"
        skipRegEditorCompleters
      />
      <ScriptEditor
        v-model="stagingRow.insert"
        class="insert-script"
        data-test="insert-script"
        :label="$t('insertDataInDest')"
        skipRegEditorCompleters
      />
      <div class="btn-ctr d-flex flex-row">
        <span
          v-if="isInErrState"
          class="text-body-2 text-error d-inline-block mt-2"
          data-test="script-err-msg"
        >
          {{ $t('errors.scriptCanNotBeEmpty') }}
        </span>
        <VSpacer />
        <VBtn
          v-if="hasChanged"
          color="primary"
          rounded
          variant="outlined"
          class="mr-2 font-weight-medium px-7 text-capitalize"
          data-test="discard-btn"
          @click="emit('on-discard')"
        >
          {{ $t('discard') }}
        </VBtn>
      </div>
    </div>
  </div>
</template>

<style lang="scss" scoped>
.field__label {
  font-size: 0.875rem !important;
}
.btn-ctr {
  height: 36px;
}
.script-container {
  .create-script {
    flex-grow: 0.5;
  }
  .select-script,
  .insert-script {
    flex-grow: 0.25;
  }
}
</style>
