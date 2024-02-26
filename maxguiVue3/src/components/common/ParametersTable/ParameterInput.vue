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
import { DURATION_UNITS, SIZE_UNITS } from '@/constants'
import {
  typeCastingEnumMask,
  typeCastingStringList,
  parseValueWithUnit,
  convertDuration,
  convertSize,
} from '@/components/common/ParametersTable/utils'

const props = defineProps({
  item: { type: Object, required: true },
  keyInfo: { type: Object, required: true },
  creationMode: { type: Boolean, required: true },
  isListener: { type: Boolean, required: true },
  portValue: { type: [Number, String] }, // when port is empty, it's a string
  socketValue: { type: String },
})

const emit = defineEmits(['on-change'])
const typy = useTypy()
const { t } = useI18n()

const type = computed(() => typy(props.keyInfo, 'type').safeString)
const disabled = computed(() =>
  props.creationMode ? false : !typy(props.keyInfo, 'modifiable').safeBoolean
)
const required = computed(() => typy(props.keyInfo, 'mandatory').safeBoolean)
const allowMultiple = computed(() => type.value === 'enum_mask' || type.value === 'enum list')
const isPwdInput = computed(() => type.value === 'password')
const typeWithUnit = computed(() => type.value === 'duration' || type.value === 'size')
const isAddressInput = computed(() => props.item.key === 'address')
const isSocketInput = computed(() => props.item.key === 'socket')
const isPortInput = computed(() => props.item.key === 'port')
const stringInputRules = computed(() => {
  if (isAddressInput.value && !props.isListener) return rules.value.addressRequired
  if (isSocketInput.value) return rules.value.portOrSocketRequired
  return rules.value.required
})

let input = ref(null)
let isPwdVisible = ref(false)
let activeUnit = ref(null)
let typeWithUnitErrState = ref(false)

const rules = ref({
  required: [(v) => validateEmpty(v)],
  number: [(v) => validateNumber(v)],
  arrRequired: [(v) => validateEmptyArr(v)],
  addressRequired: [(v) => validateAddress(v)],
  portOrSocketRequired: [(v) => validatePortAndSocket(v)],
})

watch(
  props.item,
  (v) => {
    input.value = processParamValue(v.value)
  },
  { immediate: true }
)
watch(input, (v) => {
  emit('on-change', { key: props.item.key, value: typeCasting(v), id: props.item.id })
})

watch(activeUnit, (v, oV) => appendUnit(v, oV))

function processParamValue(v) {
  if (typeWithUnit.value) return processParamHasUnit(v)
  if (type.value === 'enum_mask') return typeCastingEnumMask({ v })
  if (type.value === 'stringlist') return typeCastingStringList({ v })
  return v
}

function processParamHasUnit(v) {
  const { unit, value } = parseValueWithUnit(String(v)) || {}
  activeUnit.value = unit || props.keyInfo.unit || null
  if (unit) return value
  return v
}

// Auto convert value
function appendUnit(newUnit, oldUnit) {
  const ms = convertDuration({ unit: oldUnit, v: input.value })
  if (type.value === 'duration')
    switch (newUnit) {
      case 'ms':
        input.value = ms
        break
      case 's':
      case 'm':
      case 'h':
        input.value = convertDuration({ unit: newUnit, v: ms, toMilliseconds: false })
        break
    }
  else if (type.value === 'size')
    switch (newUnit) {
      case undefined:
      case 'Ki':
      case 'k':
      case 'Mi':
      case 'M':
      case 'Gi':
      case 'G':
      case 'Ti':
      case 'T': {
        let value = input.value,
          currentVal = value
        const IEC = [undefined, 'Ki', 'Mi', 'Gi', 'Ti']
        // oldUnit === null when switching from default 'byte' unit to new unit
        const prevIsSuffixIEC = IEC.includes(oldUnit) || oldUnit === null
        const nextIsSuffixIEC = IEC.includes(newUnit)
        // first convert from oldUnit to bytes or bits

        const IECToSI = prevIsSuffixIEC && !nextIsSuffixIEC
        const SITOIEC = !prevIsSuffixIEC && nextIsSuffixIEC
        // from IEC to SI or from SI to IEC
        if (IECToSI || SITOIEC) {
          // to bytes or bits
          currentVal = convertSize({
            unit: oldUnit,
            isIEC: prevIsSuffixIEC,
            v: currentVal,
          })
          // convert currentVal bytes to bits or bits to bytes
          currentVal = IECToSI ? currentVal * 8 : currentVal / 8
          // reverse from bytes or bits to target unit
          value = convertSize({
            unit: newUnit,
            isIEC: nextIsSuffixIEC,
            v: currentVal,
            reverse: true,
          })
        }
        // from IEC to IEC or from SI to SI
        else if ((prevIsSuffixIEC && nextIsSuffixIEC) || (!prevIsSuffixIEC && !nextIsSuffixIEC)) {
          // to bytes or bits
          currentVal = convertSize({
            unit: oldUnit,
            isIEC: prevIsSuffixIEC,
            v: input.value,
          })
          // reverse from bytes or bits to target unit
          value = convertSize({
            unit: newUnit,
            isIEC: nextIsSuffixIEC,
            v: currentVal,
            reverse: true,
          })
        }
        input.value = value
      }
    }
  return `${input.value}${typy(activeUnit, 'value').safeString}`
}

function typeCasting(v) {
  switch (type.value) {
    case 'duration':
    case 'size':
      return `${v}${typy(activeUnit, 'value').safeString}`
    case 'enum_mask':
      return typeCastingEnumMask({ v, reverse: true }) // convert back to string
    case 'stringlist':
      return typeCastingStringList({ v, reverse: true }) // convert back to array
    default:
      return v
  }
}

function isEmpty(v) {
  return v === '' || v === undefined || v === null
}

function validateEmpty(v) {
  if (isEmpty(v) && required.value) return t('errors.requiredInput', { inputName: props.item.key })
  return true
}

function validateEmptyArr(v) {
  if (typy(v).safeArray.length < 1 && required.value)
    return t('errors.requiredInput', { inputName: props.item.key })
  return true
}

function validateNumber(v) {
  const isEmptyVal = isEmpty(v)
  // type validation
  const isValidInt = /^[-]?\d*$/g.test(v)
  const isValidNaturalNum = /^\d*$/g.test(v)

  if (isEmpty(v) && required.value) {
    typeWithUnitErrState.value = true
    return t('errors.requiredInput', { inputName: props.item.key })
  } else {
    typeWithUnitErrState.value = true
    switch (type.value) {
      case 'int':
      case 'duration':
        if ((!isValidInt && !isEmptyVal) || v === '-') return t('errors.nonInteger')
        break
      case 'count':
        if (!isValidNaturalNum && !isEmptyVal) return t('errors.negativeNum')
        break
    }
  }
  typeWithUnitErrState.value = false
  return true
}

function checkPortAndSocketExistence() {
  const portExist = !isEmpty(props.portValue)
  const socketExist = !isEmpty(props.socketValue)
  return { portExist, socketExist }
}

function validateAddress(v) {
  const { portExist, socketExist } = checkPortAndSocketExistence()

  const bothExist = socketExist && portExist

  const isEmptyVal = isEmpty(v)
  if (isEmptyVal && portExist) return t('errors.addressRequired')
  else if (!isEmptyVal && socketExist && !bothExist) return t('errors.addressRequiredEmpty')
  return true
}

function validatePortAndSocket(v) {
  const { portExist, socketExist } = checkPortAndSocketExistence()
  // 0 is treated as falsy
  const bothEmpty =
    (isPortInput.value && !v && !socketExist) || (isSocketInput.value && !v && !portExist)

  const bothValueExist = portExist && socketExist

  if (bothEmpty || bothValueExist) return t('errors.portSocket')

  return true
}
</script>

<template>
  <!-- VSelect no longer accepts boolean items, so items must be an array of objects -->
  <VSelect
    v-if="type === 'bool'"
    v-model="input"
    :items="[
      { title: 'true', value: true },
      { title: 'false', value: false },
    ]"
    :disabled="disabled"
    hide-details="auto"
  />

  <VSelect
    v-else-if="type === 'enum' || allowMultiple"
    v-model="input"
    :items="keyInfo.enum_values"
    :multiple="allowMultiple"
    :disabled="disabled"
    hide-details="auto"
    :rules="type === 'enum' ? rules.required : rules.arrRequired"
  >
    <template v-if="allowMultiple" #selection="{ item, index }">
      <template v-if="index === 0">
        {{ item.title }}
      </template>
      <span v-if="index === 1" class="text-caption text-grayed-out">
        (+{{ input.length - 1 }} {{ $t('others') }})
      </span>
    </template>
  </VSelect>

  <VTextarea
    v-else-if="type === 'stringlist'"
    v-model="input"
    auto-grow
    rows="1"
    row-height="15"
    autocomplete="off"
    hide-details="auto"
    :disabled="disabled"
    :rules="rules.required"
  />

  <div
    v-else-if="type === 'count' || type === 'int' || typeWithUnit"
    class="d-flex align-center w-100"
  >
    <VTextField
      v-model.trim.number="input"
      :disabled="disabled"
      autocomplete="off"
      hide-details="auto"
      :class="{ 'text-field--with-unit': typeWithUnit }"
      :rules="isPortInput ? rules.portOrSocketRequired : rules.number"
      @keypress="
        type === 'int' || type === 'duration'
          ? $helpers.preventNonInteger($event)
          : $helpers.preventNonNumericalVal($event)
      "
    />
    <VSelect
      v-if="typeWithUnit"
      v-model="activeUnit"
      class="unit-select"
      :style="{ maxWidth: type === 'size' ? '65px' : '60px' }"
      :clearable="type === 'size'"
      :items="type === 'duration' ? DURATION_UNITS : SIZE_UNITS"
      hide-details="auto"
      :error="typeWithUnitErrState"
      :error-messages="typeWithUnitErrState ? 'error' : ''"
      :disabled="disabled"
    >
      <template #message="{ message }">
        <span class="d-none">{{ message }}</span>
      </template>
    </VSelect>
  </div>

  <VTextField
    v-else
    v-model.trim="input"
    :disabled="disabled"
    :autocomplete="isPwdInput ? 'new-password' : 'off'"
    hide-details="auto"
    :type="isPwdInput ? (isPwdVisible ? 'text' : 'password') : 'text'"
    :rules="stringInputRules"
  >
    <template v-if="isPwdInput" #append-inner>
      <VIcon
        size="20"
        :icon="isPwdVisible ? '$mdiEyeOff' : '$mdiEye'"
        @click="isPwdVisible = !isPwdVisible"
      />
    </template>
  </VTextField>
</template>

<style lang="scss">
.text-field--with-unit {
  .v-field {
    .v-field__outline {
      &__end {
        border-right: none;
        border-top-right-radius: 0px !important;
        border-bottom-right-radius: 0px !important;
      }
    }
  }
}
.unit-select {
  .v-field {
    padding: 0 !important;
    .v-field__input {
      padding: 0 0 0 8px !important;
    }
    .v-field__append-inner {
      .v-icon {
        margin-left: 0 !important;
      }
    }
    .v-field__clearable {
      width: 16px;
      margin: 0px;
      .v-icon {
        font-size: 16px;
      }
    }
    .v-field__outline {
      &__start {
        border-top-left-radius: 0px !important;
        border-bottom-left-radius: 0px !important;
      }
    }
  }
}
</style>
