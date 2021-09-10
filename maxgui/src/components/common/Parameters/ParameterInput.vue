<template>
    <!-- if targetItem has expanded property, meaning it has child object, so no need to render input -->
    <span v-if="'expanded' in targetItem" class="expandable-param" />

    <!-- Handle edge case with port, address, socket custom rules-->
    <v-text-field
        v-else-if="!isListener && targetItem.id === 'address'"
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model.trim="targetItem.value"
        :name="targetItem.id"
        class="std error--text__bottom error--text__bottom--no-margin"
        :class="targetItem.type"
        single-line
        outlined
        dense
        :rules="rules.requiredAddress"
        :disabled="targetItem.disabled"
        autocomplete="off"
        @input="handleChange"
    />

    <v-text-field
        v-else-if="targetItem.id === 'socket'"
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model.trim="targetItem.value"
        :name="targetItem.id"
        class="std error--text__bottom error--text__bottom--no-margin"
        :class="targetItem.type"
        single-line
        outlined
        dense
        :rules="rules.requiredFieldEither"
        :disabled="targetItem.disabled"
        autocomplete="off"
        @input="handleChange"
    />

    <v-text-field
        v-else-if="targetItem.id === 'port'"
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model.trim.number="targetItem.value"
        :name="targetItem.id"
        class="std error--text__bottom error--text__bottom--no-margin"
        :class="targetItem.type"
        single-line
        outlined
        dense
        :rules="rules.requiredFieldEither"
        :disabled="targetItem.disabled"
        autocomplete="off"
        @keypress="preventNonNumericalVal($event)"
        @input="handleChange"
    />

    <!-- bool parameter type -->
    <v-select
        v-else-if="targetItem.type === 'bool'"
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model="targetItem.value"
        :name="targetItem.id"
        class="std mariadb-select-input error--text__bottom error--text__bottom--no-margin"
        :class="targetItem.type"
        :menu-props="{ contentClass: 'mariadb-select-v-menu', bottom: true, offsetY: true }"
        :items="[true, false]"
        outlined
        dense
        :disabled="targetItem.disabled"
        @change="handleChange"
    />

    <!-- enum_mask or enum parameter type -->
    <v-select
        v-else-if="targetItem.type === 'enum_mask' || targetItem.type === 'enum'"
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model="targetItem.value"
        :name="targetItem.id"
        class="std mariadb-select-input error--text__bottom error--text__bottom--no-margin"
        :class="targetItem.type"
        :menu-props="{ contentClass: 'mariadb-select-v-menu', bottom: true, offsetY: true }"
        :items="targetItem.enum_values"
        outlined
        dense
        :multiple="targetItem.type === 'enum_mask'"
        :disabled="targetItem.disabled"
        :rules="rules.required"
        @change="handleChange"
    >
        <template v-if="targetItem.type === 'enum_mask'" v-slot:selection="{ item, index }">
            <span v-if="index === 0" class="v-select__selection v-select__selection--comma">
                {{ item }}
            </span>
            <span
                v-if="index === 1"
                class="v-select__selection v-select__selection--comma color text-caption text-field-text "
            >
                (+{{ targetItem.value.length - 1 }} {{ $t('others') }}))
            </span>
        </template>
    </v-select>

    <!-- count, int, duration type -->
    <v-text-field
        v-else-if="
            targetItem.type === 'count' ||
                targetItem.type === 'int' ||
                targetItem.type === 'duration' ||
                targetItem.type === 'size'
        "
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model.trim.number="targetItem.value"
        :name="targetItem.id"
        class="std suffix-select-input error--text__bottom error--text__bottom--no-margin"
        :class="targetItem.type"
        single-line
        outlined
        dense
        :rules="rules.number"
        :disabled="targetItem.disabled"
        autocomplete="off"
        @keypress="
            targetItem.type === 'int' || targetItem.type === 'duration'
                ? $help.preventNonInteger($event)
                : $help.preventNonNumericalVal($event)
        "
        @input="handleChange"
    >
        <!-- duration parameter type -->
        <template v-if="targetItem.type === 'duration' || targetItem.type === 'size'" v-slot:append>
            <v-select
                v-model="chosenSuffix"
                :name="targetItem.id"
                class="std mariadb-select-input suffix-select"
                :menu-props="{
                    contentClass: 'mariadb-select-v-menu',
                    bottom: true,
                    offsetY: true,
                }"
                :clearable="targetItem.type === 'size'"
                :items="targetItem.type === 'duration' ? durationSuffixes : sizeSuffixes"
                outlined
                dense
            />
        </template>
    </v-text-field>

    <!-- password string parameter type -->
    <v-text-field
        v-else-if="targetItem.type === 'password string'"
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model.trim="targetItem.value"
        :name="targetItem.id"
        class="std std-password error--text__bottom error--text__bottom--no-margin password-string"
        outlined
        dense
        :type="isPwdVisible ? 'text' : 'password'"
        :rules="rules.required"
        autocomplete="new-password"
        :disabled="targetItem.disabled"
        @input="handleChange"
        @click:append="isPwdVisible = !isPwdVisible"
    >
        <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
            {{ isPwdVisible ? 'visibility_off' : 'visibility' }}
        </v-icon>
    </v-text-field>

    <!--others parameter types -->
    <v-text-field
        v-else
        :id="`${targetItem.id}-${targetItem.nodeId}` || targetItem.id"
        v-model.trim="targetItem.value"
        :name="targetItem.id"
        class="std error--text__bottom error--text__bottom--no-margin"
        :class="`${targetItem.type} string`"
        single-line
        outlined
        dense
        :rules="rules.required"
        :disabled="targetItem.disabled"
        autocomplete="off"
        @input="handleChange"
    />
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component accepts these optional props:
- portValue, socketValue and parentForm are passed if a server is being
  created or updated, this helps to facilitate special rules for port, socket and address parameter
- isListener: accepts boolean , if true, address parameter will not be required

Emits:
- $emit('on-input-change', { targetItemCloned: object, changed: boolean })
*/
export default {
    name: 'parameter-input',
    props: {
        item: { type: Object, required: true },
        portValue: { type: Number },
        socketValue: { type: String },
        parentForm: { type: Object },
        isListener: { type: Boolean, default: false },
    },
    data() {
        return {
            targetItem: {},
            rules: {
                required: [val => this.handleRequired(val)],
                number: [val => this.validateNumber(val)],
                requiredAddress: [val => this.handleRequiredAddress(val)],
                requiredFieldEither: [val => this.handleRequiredFieldEither(val)],
            },
            renderCount: 0,
            durationSuffixes: ['ms', 's', 'm', 'h'],
            sizeSuffixes: ['Ki', 'Mi', 'Gi', 'Ti', 'k', 'M', 'G', 'T'],
            chosenSuffix: null,
            isPwdVisible: false,
        }
    },
    watch: {
        'targetItem.value'() {
            this.$nextTick(() => {
                // should not trigger parent form validate on first render
                this.parentForm && this.renderCount !== 0 && this.parentForm.validate()
                this.renderCount === 0 && (this.renderCount = this.renderCount + 1)
            })
        },
        chosenSuffix: function(newSuffix, oldSuffix) {
            if (oldSuffix && this.targetItem.value !== null) {
                if (this.durationSuffixes.includes(newSuffix)) {
                    this.targetItem.value = this.durationSuffixSwapper(
                        newSuffix,
                        oldSuffix,
                        this.targetItem.value
                    )
                    this.handleChange()
                }
            }
            // newSuffix === undefined when chosenSuffix is cleared
            if (this.sizeSuffixes.includes(newSuffix) || newSuffix === undefined) {
                this.targetItem.value = this.sizeSuffixSwapper(
                    newSuffix,
                    oldSuffix,
                    this.targetItem.value
                )
                this.handleChange()
            }
        },
        item: function(newItem, oldItem) {
            if (!this.$help.lodash.isEqual(newItem, oldItem)) {
                this.targetItem = this.processItem(this.$help.lodash.cloneDeep(newItem))
            }
        },
    },

    created() {
        this.targetItem = this.processItem(this.$help.lodash.cloneDeep(this.item))
    },

    methods: {
        /**
         * @param {Object} clonedItem cloned item that needs to be processed
         * @param {String} mode mode be processed. Default is null
         * @return {Object} new clonedItem or original clonedItem
         */
        processItem(clonedItem, mode) {
            switch (clonedItem.type) {
                case 'enum_mask':
                    return this.processEnumMask(clonedItem, mode)
                case 'duration': {
                    if (clonedItem.value !== null) {
                        return this.processParamHasSuffix(clonedItem, mode)
                    } else {
                        //if there is no value, find unit props
                        this.chosenSuffix = clonedItem.unit ? clonedItem.unit : 'ms'
                        return clonedItem
                    }
                }
                case 'size': {
                    if (clonedItem.value !== null) {
                        return this.processParamHasSuffix(clonedItem, mode)
                    } else {
                        return clonedItem
                    }
                }
                default:
                    return clonedItem
            }
        },

        /**
         * Processing enum_mask type for editing
         * v-select component accepts array as value type for v-model when multiple props is enabled.
         * But when sending the values back to parent component, it will be converted
         * to a string.
         * @param {Object} item target item to be processed
         * @param {String} mode mode be processed
         * @return {Object} new processed item
         */
        processEnumMask(item, mode) {
            let result = item
            if (mode === 'reverse') {
                result.value = result.value.toString()
            } else result.value = result.value.split(',') // to array
            return result
        },

        /**
         * @param {Object} param target parameter to be processed
         * @param {String} mode mode be processed
         * @return {Object} new processed parameter
         * If mode is reverse, return new param object with value property includes chosen suffix
         */
        processParamHasSuffix(param, mode) {
            let result = param
            if (mode === 'reverse') {
                result.value = this.chosenSuffix
                    ? `${result.value}${this.chosenSuffix}`
                    : parseInt(result.value)
            } else {
                typeof result.value !== 'string' && (result.value = result.value.toString())
                let suffixInfo = this.$help.getSuffixFromValue(param, this.durationSuffixes)
                if (suffixInfo.suffix) {
                    this.chosenSuffix = suffixInfo.suffix
                    result.value = result.value.slice(0, suffixInfo.indexOfSuffix)
                } else if (param.unit) {
                    this.chosenSuffix = param.unit
                }
            }
            return result
        },

        sizeSuffixSwapper(to, from, val) {
            let currentVal = val
            switch (to) {
                case undefined:
                case 'Ki':
                case 'k':
                case 'Mi':
                case 'M':
                case 'Gi':
                case 'G':
                case 'Ti':
                case 'T': {
                    const IEC = [undefined, 'Ki', 'Mi', 'Gi', 'Ti']
                    // from === null when switching from default 'byte' suffix to new suffix
                    const prevIsSuffixIEC = IEC.includes(from) || from === null
                    const nextIsSuffixIEC = IEC.includes(to)
                    // first convert from oldSuffix to bytes or bits
                    let value
                    const IECToSI = prevIsSuffixIEC && !nextIsSuffixIEC
                    const SITOIEC = !prevIsSuffixIEC && nextIsSuffixIEC
                    // from IEC to SI or from SI to IEC
                    if (IECToSI || SITOIEC) {
                        // to bytes or bits
                        currentVal = this.$help.convertSize({
                            suffix: from,
                            isIEC: prevIsSuffixIEC,
                            val: currentVal,
                        })
                        // convert currentVal bytes to bits or bits to bytes
                        currentVal = IECToSI ? currentVal * 8 : currentVal / 8
                        // reverse from bytes or bits to target suffix
                        value = this.$help.convertSize({
                            suffix: to,
                            isIEC: nextIsSuffixIEC,
                            val: currentVal,
                            reverse: true,
                        })
                    }
                    // from IEC to IEC or from SI to SI
                    else if (
                        (prevIsSuffixIEC && nextIsSuffixIEC) ||
                        (!prevIsSuffixIEC && !nextIsSuffixIEC)
                    ) {
                        // to bytes or bits
                        currentVal = this.$help.convertSize({
                            suffix: from,
                            isIEC: prevIsSuffixIEC,
                            val: val,
                        })
                        // reverse from bytes or bits to target suffix
                        value = this.$help.convertSize({
                            suffix: to,
                            isIEC: nextIsSuffixIEC,
                            val: currentVal,
                            reverse: true,
                        })
                    }
                    return value
                }
            }
        },

        durationSuffixSwapper(newSuffix, oldSuffix, val) {
            const { convertDuration } = this.$help
            // convert to miliseconds from oldSuffix
            const ms = convertDuration({ suffix: oldSuffix, val })
            switch (newSuffix) {
                case 'ms':
                    return ms
                case 's':
                case 'm':
                case 'h': {
                    return convertDuration({
                        suffix: newSuffix,
                        val: ms,
                        toMilliseconds: false,
                    })
                }
            }
        },

        // ----------------------------------------------  Handle input change ---------------------------------------
        handleChange() {
            const self = this
            /*reverse processing item to original type*/
            let targetItemCloned = this.processItem(
                this.$help.lodash.cloneDeep(self.targetItem),
                'reverse'
            )
            const changed = !this.$help.lodash.isEqual(targetItemCloned, self.item)
            /*
                _Handling edge case, either socket or port needs to be defined,
                that leads to the issue when empty port or empty socket will be
                treated as string. But maxscale wants it null if it is empty.
            */
            if (targetItemCloned.value === '') {
                targetItemCloned.id === 'socket' ||
                    (targetItemCloned.id === 'port' && (targetItemCloned.value = null))
            }

            this.$emit('on-input-change', targetItemCloned, changed)
        },

        // ---------------------------------------------------- input validation ---------------------------------------
        validateNumber(val) {
            const isEmptyVal = this.isEmpty(val)
            // required param validation
            let moduleParamRequired = isEmptyVal && this.targetItem.mandatory
            // type validation
            const isValidInt = /^[-]?\d*$/g.test(val)
            const isValidNaturalNum = /^\d*$/g.test(val)

            if (moduleParamRequired) {
                return this.$t('errors.requiredInput', { inputName: this.targetItem.id })
            } else {
                switch (this.targetItem.type) {
                    case 'int':
                    case 'duration':
                        if ((!isValidInt && !isEmptyVal) || val === '-')
                            return this.$t('errors.nonInteger')
                        break
                    case 'count':
                        if (!isValidNaturalNum && !isEmptyVal) return this.$t('errors.negativeNum')
                        break
                }
            }
            return true
        },

        handleRequired(val) {
            // required param validation
            let moduleParamRequired = this.isEmpty(val) && this.targetItem.mandatory
            if (moduleParamRequired) {
                return this.$t('errors.requiredInput', { inputName: this.targetItem.id })
            }
            return true
        },

        // port or socket
        handleRequiredFieldEither(val) {
            const portExist = !this.isEmpty(this.portValue)
            const socketExist = !this.isEmpty(this.socketValue)

            const bothEmpty =
                (this.targetItem.id === 'port' && !val && !this.socketValue) ||
                (this.targetItem.id === 'socket' && !val && !this.portValue)

            const bothValueExist = portExist && socketExist

            if (bothEmpty || bothValueExist) {
                return this.$t('errors.portSocket')
            } else return true
        },

        // address rules if !isListener
        handleRequiredAddress(val) {
            const portExist = !this.isEmpty(this.portValue)
            const socketExist = !this.isEmpty(this.socketValue)
            const bothExist = socketExist && portExist

            const isEmptyVal = this.isEmpty(val)

            if (isEmptyVal && portExist) {
                return this.$t('errors.addressRequired')
            } else if (!isEmptyVal && socketExist && !bothExist) {
                return this.$t('errors.addressRequiredEmpty')
            }
            return true
        },

        isEmpty: val => val === '' || val === null,
    },
}
</script>

<style lang="scss" scoped>
.std.v-input.v-text-field.error--text.error--text__bottom--no-margin {
    margin: 14px 0px 6px;
    ::v-deep .v-text-field__details {
        margin: 0px;
    }
}
.std ::v-deep .v-input__control {
    min-width: 160px;
    .v-input__slot {
        margin: 0;
    }
}
.std ::v-deep .v-messages__message {
    white-space: normal;
    line-height: 16px;
}
.suffix-select ::v-deep .v-select__selections input,
.std ::v-deep .v-select__selections input {
    display: none;
}
.suffix-select-input {
    ::v-deep .v-input__append-inner {
        margin-top: 0 !important;
    }
}
.suffix-select {
    border-radius: 0px;

    ::v-deep .v-input__control {
        min-width: 60px;
        margin-right: -10px;

        .v-input__slot {
            padding: 0 0px 0 9px !important;
            height: 36px;
            .v-select__slot {
                .v-select__selection.v-select__selection--comma {
                    text-align: center;
                    width: 100%;
                }
            }

            .v-input__append-inner {
                padding-left: 0px !important;
                .v-input__icon--clear {
                    margin: 0px 2px;
                    .v-icon {
                        font-size: 16px !important;
                        color: $error !important;
                        opacity: 0.7;
                        &:hover {
                            opacity: 1;
                        }
                    }
                }
            }
        }
        fieldset {
            border-right: none;
            border-top: none;
            border-bottom: none;
        }
    }
}
</style>
