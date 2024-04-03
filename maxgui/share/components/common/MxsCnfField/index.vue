<template>
    <div :id="field.id" class="cnf-field" :class="{ 'pb-4': type !== 'boolean' }">
        <label
            v-if="type !== 'boolean'"
            class="field__label mxs-color-helper text-small-text label-required"
            :class="{ 'field__label-variable': field.isVariable }"
        >
            {{ field.label }}
        </label>
        <v-icon
            v-if="field.icon && type !== 'boolean'"
            size="14"
            :color="field.iconColor"
            class="ml-1 mb-1 pointer"
            @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
            @mouseleave="rmInfoTooltip"
            @click="onIconClick"
        >
            {{ field.icon }}
        </v-icon>
        <slot v-if="$slots[customInputSlotName]" :name="customInputSlotName" />
        <template v-else>
            <v-select
                v-if="type === 'enum'"
                v-model="inputValue"
                :items="field.enumValues"
                outlined
                class="vuetify-input--override v-select--mariadb error--text__bottom"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="height"
                hide-details="auto"
            >
                <template v-slot:selection="{ item }">
                    <div class="v-select__selection v-select__selection--comma text-capitalize">
                        {{ $mxs_tc(item, 1) }}
                    </div>
                </template>
                <template v-slot:item="{ item }">
                    <v-list-item-title class="text-capitalize">
                        {{ $mxs_tc(item, 1) }}
                    </v-list-item-title>
                </template>
            </v-select>
            <v-checkbox
                v-else-if="type === 'boolean'"
                v-model="inputValue"
                class="v-checkbox--mariadb"
                dense
                color="primary"
                hide-details="auto"
            >
                <template v-slot:label>
                    <label class="v-label pointer">{{ field.label }}</label>
                    <v-icon
                        v-if="field.icon"
                        class="ml-1 material-icons-outlined pointer"
                        size="16"
                        :color="field.iconColor"
                        @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
                        @mouseleave="rmInfoTooltip"
                        @click="onIconClick"
                    >
                        {{ field.icon }}
                    </v-icon>
                </template>
            </v-checkbox>
            <v-text-field
                v-else-if="type === 'positiveNumber' || type === 'nonNegativeNumber'"
                v-model.number="inputValue"
                type="number"
                :rules="[v => validateNumber({ v, inputName: field.label })]"
                class="vuetify-input--override error--text__bottom"
                dense
                :height="height"
                hide-details="auto"
                outlined
                :required="required"
                :suffix="field.suffix"
                @keypress="$helpers.preventNonNumericalVal($event)"
            />
            <v-text-field
                v-else-if="type === 'string' || isColorInput"
                v-model="inputValue"
                class="vuetify-input--override error--text__bottom"
                dense
                :height="height"
                hide-details="auto"
                outlined
                :required="required"
                :rules="[
                    v =>
                        isColorInput
                            ? validateColor(v)
                            : !!v || $mxs_t('errors.requiredInput', { inputName: field.label }),
                ]"
                :max-length="isColorInput ? 7 : -1"
            >
                <template v-if="isColorInput" v-slot:append>
                    <span
                        v-if="inputValue"
                        :style="{ backgroundColor: inputValue }"
                        class="pa-2 rounded mxs-color-helper all-border-table-border"
                    />
                </template>
            </v-text-field>
        </template>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
field: {
  id: string,
  label: string,
  icon?: string,
  iconColor?: string,
  enumValues?: array, enum items for type enum
  href?: string, external link.
  isVariable?: boolean, if true, label won't be capitalized
  suffix?: string
}
type: positiveNumber, nonNegativeNumber, boolean, enum, string, color
*/
export default {
    name: 'mxs-cnf-field',
    props: {
        value: { type: [String, Number, Boolean], required: true },
        field: { type: Object, required: true },
        type: { type: String, required: true },
        required: { type: Boolean, default: true },
        height: { type: Number, default: 36 },
    },
    computed: {
        inputValue: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        customInputSlotName() {
            return `${this.field.id}-input`
        },
        isColorInput() {
            return this.type === 'color'
        },
    },
    methods: {
        validateNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString)
                return this.$mxs_t('errors.requiredInput', { inputName })
            if (this.type === 'positiveNumber') {
                if (v <= 0) return this.$mxs_t('errors.largerThanZero', { inputName })
                if (v > 0) return true
            } else if (v >= 0) return true
            return this.$mxs_t('errors.negativeNum')
        },
        showInfoTooltip(data) {
            if (!this.field.href) this.$emit('tooltip', data)
        },
        rmInfoTooltip() {
            if (!this.field.href) this.$emit('tooltip', undefined)
        },
        onIconClick() {
            if (this.field.href) window.open(this.field.href, '_blank', 'noopener,noreferrer')
        },
        validateColor(v) {
            return this.$helpers.validateHexColor(v) || this.$mxs_t('errors.hexColor')
        },
    },
}
</script>

<style lang="scss" scoped>
.field__label-variable {
    &::first-letter {
        text-transform: lowercase;
    }
}
</style>
