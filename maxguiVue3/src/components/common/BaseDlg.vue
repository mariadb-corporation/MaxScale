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

/*
 * Events
 * is-form-valid?: (boolean)
 * after-cancel?: (function)
 * after-close: (function)
 */
import { OVERLAY_TRANSPARENT_LOADING } from '@/constants/overlayTypes'

const props = defineProps({
  modelValue: { type: Boolean, required: true },
  minBodyWidth: { type: String, default: '466px' },
  isDynamicWidth: { type: Boolean, default: false },
  scrollable: { type: Boolean, default: true },
  title: { type: String, required: true },
  onSave: { type: Function, required: true },
  cancelText: { type: String, default: 'cancel' },
  saveText: { type: String, default: 'save' },
  // manually control btn disabled
  hasChanged: { type: Boolean, default: true },
  // if isForceAccept===true, cancel and close btn won't be rendered
  isForceAccept: { type: Boolean, default: false },
  lazyValidation: { type: Boolean, default: true },
  hasSavingErr: { type: Boolean, default: false },
  hasFormDivider: { type: Boolean, default: false },
  /**
   * close dialog immediately, don't wait for submit
   * Limitation: form needs to be cleared manually on parent component
   */
  closeImmediate: { type: Boolean, default: false },
  allowEnterToSubmit: { type: Boolean, default: true },
  showCloseBtn: { type: Boolean, default: true },
  bodyCtrClass: { type: [String, Object, Array], default: 'px-0 pt-0 pb-12' },
  formClass: { type: [String, Object, Array], default: 'body-padding-x' },
  attach: { type: Boolean, default: false },
})
const emit = defineEmits(['update:modelValue', 'is-form-valid', 'after-cancel', 'after-close'])

const helpers = useHelpers()
const store = useStore()
const formValidity = ref(null)
const form = ref(null)

const isDlgOpened = computed({
  get() {
    return props.modelValue
  },
  set(v) {
    emit('update:modelValue', v)
  },
})

const isSaveDisabled = computed(
  () => props.hasSavingErr || !props.hasChanged || formValidity.value === false
)

watch(formValidity, (v) => emit('is-form-valid', Boolean(v)))

function closeDialog() {
  isDlgOpened.value = false
}

function cancel() {
  cleanUp()
  closeDialog()
  emit('after-cancel')
}

function close() {
  closeDialog()
  emit('after-close')
}

async function keydownHandler() {
  if (props.allowEnterToSubmit && !isSaveDisabled.value) await save()
}

function cleanUp() {
  if (form.value) {
    form.value.reset()
    form.value.resetValidation()
  }
}

async function waitClose() {
  // wait time out for loading animation
  await helpers.delay(600).then(() => store.commit('mxsApp/SET_OVERLAY_TYPE', null))
  cleanUp()
  closeDialog()
}

function handleCloseImmediate() {
  closeDialog()
  store.commit('mxsApp/SET_OVERLAY_TYPE', null)
}

async function validateForm() {
  await form.value.validate()
}

async function save() {
  await validateForm()
  if (formValidity.value === false) helpers.scrollToFirstErrMsgInput()
  else {
    store.commit('mxsApp/SET_OVERLAY_TYPE', OVERLAY_TRANSPARENT_LOADING)
    if (!props.hasSavingErr && props.closeImmediate) handleCloseImmediate()
    await props.onSave()
    if (!props.closeImmediate) {
      if (!props.hasSavingErr) await waitClose()
      else store.commit('mxsApp/SET_OVERLAY_TYPE', null)
    }
  }
}
</script>

<template>
  <VDialog
    v-model="isDlgOpened"
    width="unset"
    content-class="base-dlg"
    persistent
    :scrollable="scrollable"
    :attach="attach"
  >
    <!-- Use tabIndex to make VCard focusable so that keydown event can be listened-->
    <VCard
      :min-width="minBodyWidth"
      :max-width="isDynamicWidth ? 'unset' : minBodyWidth"
      @keydown.enter="keydownHandler"
      tabindex="0"
    >
      <VCardTitle class="v-card-title_padding">
        <h3 class="text-h3 font-weight-light text-deep-ocean">
          {{ title }}
        </h3>
        <VBtn
          v-if="!isForceAccept && showCloseBtn"
          class="close"
          data-test="close-btn"
          density="comfortable"
          icon
          variant="text"
          @click="close"
        >
          <VIcon size="20" color="navigation" icon="mxs:close" />
        </VBtn>
      </VCardTitle>
      <VCardText :class="bodyCtrClass">
        <div v-if="$slots.body" data-test="body-slot-ctr" class="body-padding-x">
          <slot name="body" />
        </div>
        <VDivider v-if="hasFormDivider" class="my-6" />
        <div v-else class="mt-4" />
        <VForm
          ref="form"
          v-model="formValidity"
          :validate-on="lazyValidation ? 'lazy input' : 'input'"
          :class="formClass"
          data-test="form-body-slot-ctr"
        >
          <slot name="form-body" />
        </VForm>
      </VCardText>
      <VCardActions
        class="v-card-actions_padding mxs-color-helper border-top-separator"
        data-test="action-ctr"
      >
        <slot name="action-prepend" />
        <VSpacer />
        <VBtn
          v-if="!isForceAccept"
          size="small"
          :height="36"
          color="primary"
          rounded
          variant="outlined"
          class="cancel font-weight-medium px-7 text-capitalize"
          data-test="cancel-btn"
          @click="cancel"
        >
          {{ $t(cancelText) }}
        </VBtn>
        <slot name="save-btn" :save="save" :isSaveDisabled="isSaveDisabled">
          <VBtn
            size="small"
            :height="36"
            color="primary"
            rounded
            variant="flat"
            :disabled="isSaveDisabled"
            class="font-weight-medium px-7 text-capitalize"
            data-test="save-btn"
            @click="save"
          >
            {{ $t(saveText) }}
          </VBtn>
        </slot>
      </VCardActions>
    </VCard>
  </VDialog>
</template>

<style lang="scss" scoped>
.base-dlg {
  .close {
    position: absolute;
    top: 18px;
    right: 18px;
  }
  $paddingX: 62px;
  .v-card-title_padding {
    padding: 52px $paddingX 16px;
    h3 {
      word-break: break-word;
    }
  }
  .body-padding-x {
    padding: 0px $paddingX;
  }
  .v-card-actions_padding {
    padding: 30px $paddingX 36px;
  }
  .v-card-text {
    color: colors.$navigation;
    font-size: 0.875rem !important;
  }
}
</style>
