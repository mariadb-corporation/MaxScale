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
  modelValue: { type: String, required: true },
  label: { type: String, required: true },
  skipRegEditorCompleters: { type: Boolean, default: false },
})
const emit = defineEmits(['update:modelValue'])

const isFullScreen = ref(false)

const sql = computed({ get: () => props.modelValue, set: (v) => emit('update:modelValue', v) })
</script>

<template>
  <div
    class="script-editor"
    :class="[
      isFullScreen ? 'script-editor--fullscreen' : 'relative rounded',
      sql ? '' : 'script-editor--error',
    ]"
  >
    <code
      class="script-editor__header d-flex justify-space-between align-center mariadb-code-style pl-12 pr-2 py-1"
    >
      <span class="editor-comment"> -- {{ label }} </span>
      <TooltipBtn
        data-test="min-max-btn"
        icon
        variant="text"
        color="primary"
        density="compact"
        @click="isFullScreen = !isFullScreen"
      >
        <template #btn-content>
          <VIcon size="22" :icon="`$mdiFullscreen${isFullScreen ? 'Exit' : ''}`" />
        </template>
        {{ isFullScreen ? $t('minimize') : $t('maximize') }}
      </TooltipBtn>
    </code>
    <SqlEditor
      v-model="sql"
      class="script-editor__body"
      :options="{ contextmenu: false, wordWrap: 'on' }"
      :skipRegCompleters="skipRegEditorCompleters"
    />
  </div>
</template>

<style lang="scss" scoped>
.field__label {
  font-size: 0.875rem !important;
}
.script-editor {
  margin-bottom: 16px;
  border: thin solid #e8eef1;
  background-color: colors.$light-gray;
  &--fullscreen {
    height: 100%;
    width: 100%;
    z-index: 2;
    position: absolute;
    top: 0;
    right: 0;
    bottom: 0;
    left: 0;
    &:not(.script-editor--error) {
      border-color: transparent;
    }
  }
  &--error {
    border-color: colors.$error;
  }
  $header-height: 36px;
  &__header {
    height: $header-height;
    .editor-comment {
      color: #60a0b0;
    }
  }
  &__body {
    position: absolute;
    top: $header-height;
    right: 0;
    bottom: 0;
    left: 0;
  }
  :deep(.script-editor__body) {
    .margin,
    .monaco-editor-background {
      background-color: colors.$light-gray !important;
    }
  }
}
</style>
