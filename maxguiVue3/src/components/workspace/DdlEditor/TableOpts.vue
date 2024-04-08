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
import CharsetCollateSelect from '@wsComps/DdlEditor/CharsetCollateSelect.vue'

const props = defineProps({
  modelValue: { type: Object, required: true },
  engines: { type: Array, required: true },
  defDbCharset: { type: String, required: true },
  charsetCollationMap: { type: Object, required: true },
  schemas: { type: Array, default: () => [] },
  isCreating: { type: Boolean, required: true },
})
const emit = defineEmits(['update:modelValue', 'after-expand', 'after-collapse'])

const typy = useTypy()
const { t } = useI18n()
const { doubleRAF } = useHelpers()

const isExtraInputShown = ref(true)

const title = computed(() => (props.isCreating ? t('createTbl') : t('alterTbl')))
const tblOpts = computed({ get: () => props.modelValue, set: (v) => emit('update:modelValue', v) })
const defCollation = computed(
  () => typy(props.charsetCollationMap, `[${tblOpts.value.charset}].defCollation`).safeString
)

function setDefCollation() {
  // Use default collation of selected charset
  tblOpts.value.collation = defCollation.value
}

function requiredRule(inputName) {
  return [(val) => !!val || t('errors.requiredInput', { inputName })]
}

function beforeEnter(el) {
  requestAnimationFrame(() => {
    if (!el.style.height) el.style.height = '0px'
    el.style.display = null
  })
}

function enter(el) {
  doubleRAF(() => (el.style.height = `${el.scrollHeight}px`))
}

function afterEnter(el) {
  el.style.height = null
  emit('after-expand')
}

function beforeLeave(el) {
  requestAnimationFrame(() => {
    if (!el.style.height) el.style.height = `${el.offsetHeight}px`
  })
}

function leave(el) {
  doubleRAF(() => (el.style.height = '0px'))
}

function afterLeave(el) {
  el.style.height = null
  emit('after-collapse')
}
</script>

<template>
  <div class="tbl-opts px-1 py-1">
    <div class="d-flex flex-row align-end">
      <VContainer fluid class="pa-0">
        <VRow class="ma-0">
          <VCol cols="6" class="py-0 px-1">
            <label class="label-field text-small-text label--required" for="name">
              {{ title }}
            </label>
            <DebouncedTextField
              v-model="tblOpts.name"
              id="name"
              :rules="requiredRule($t('name'))"
              hide-details="auto"
              density="compact"
              autocomplete="off"
            />
          </VCol>
          <VCol cols="6" class="py-0 px-1">
            <label class="label-field text-small-text label--required" for="schemas">
              {{ $t('schemas', 1) }}
            </label>
            <VCombobox
              v-model="tblOpts.schema"
              id="schemas"
              :items="schemas"
              :rules="requiredRule($t('schemas', 1))"
              :disabled="!isCreating"
              hide-details="auto"
              density="compact"
            />
          </VCol>
        </VRow>
      </VContainer>
      <VBtn variant="text" density="compact" icon @click="isExtraInputShown = !isExtraInputShown">
        <VIcon
          :class="[isExtraInputShown ? 'rotate-up' : 'rotate-down']"
          color="navigation"
          icon="$mdiChevronDown"
          size="28"
        />
      </VBtn>
    </div>
    <transition
      enter-active-class="enter-active"
      leave-active-class="leave-active"
      @before-enter="beforeEnter"
      @enter="enter"
      @after-enter="afterEnter"
      @before-leave="beforeLeave"
      @leave="leave"
      @after-leave="afterLeave"
    >
      <div v-show="isExtraInputShown">
        <VContainer fluid class="ma-0 pa-0" :style="{ width: 'calc(100% - 28px)' }">
          <VRow class="ma-0">
            <VCol cols="6" md="2" class="py-0 px-1">
              <label class="label-field text-small-text label--required" for="table-engine">
                {{ $t('engine') }}
              </label>
              <VSelect
                v-model="tblOpts.engine"
                id="table-engine"
                :items="engines"
                density="compact"
                hide-details="auto"
              />
            </VCol>
            <VCol cols="6" md="2" class="py-0 px-1">
              <label class="label-field text-small-text label--required" for="charset">
                {{ $t('charset') }}
              </label>
              <CharsetCollateSelect
                v-model="tblOpts.charset"
                id="charset"
                :items="Object.keys(charsetCollationMap)"
                :defItem="defDbCharset"
                :rules="requiredRule($t('charset'))"
                @update:modelValue="setDefCollation"
              />
            </VCol>
            <VCol cols="6" md="2" class="py-0 px-1">
              <label class="label-field text-small-text label--required" for="collation">
                {{ $t('collation') }}
              </label>
              <CharsetCollateSelect
                v-model="tblOpts.collation"
                id="collation"
                :items="$typy(charsetCollationMap, `[${tblOpts.charset}].collations`).safeArray"
                :defItem="defCollation"
                :rules="requiredRule($t('collation'))"
              />
            </VCol>
            <VCol cols="12" md="6" class="py-0 px-1">
              <label class="label-field text-small-text" for="comment">
                {{ $t('comment') }}
              </label>
              <DebouncedTextField
                v-model="tblOpts.comment"
                id="comment"
                density="compact"
                hide-details="auto"
              />
            </VCol>
          </VRow>
        </VContainer>
      </div>
    </transition>
  </div>
</template>

<style lang="scss" scoped>
.enter-active,
.leave-active {
  overflow: hidden;
  transition: height 0.2s ease-out;
}
</style>
