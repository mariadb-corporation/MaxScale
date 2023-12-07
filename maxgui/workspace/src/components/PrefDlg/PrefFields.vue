<template>
    <div class="pl-4 pr-2 overflow-y-auto pref-fields-ctr">
        <div v-for="field in numericFields" :id="field.id" :key="field.name" class="pb-4">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t(field.name) }}
            </label>
            <v-icon
                v-if="field.icon"
                size="14"
                :color="field.iconColor"
                class="ml-1 mb-1 pointer"
                @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
                @mouseleave="rmInfoTooltip"
            >
                {{ field.icon }}
            </v-icon>
            <row-limit-ctr
                v-if="field.name === 'rowLimit'"
                :height="36"
                hide-details="auto"
                class="vuetify-input--override error--text__bottom rowLimit"
                @change="preferences[field.name] = $event"
            />
            <v-text-field
                v-else
                v-model.number="preferences[field.name]"
                type="number"
                :rules="[
                    v =>
                        validatePositiveNumber({
                            v,
                            inputName: $mxs_t(field.name),
                        }),
                ]"
                class="vuetify-input--override error--text__bottom"
                :class="field.name"
                dense
                :height="36"
                hide-details="auto"
                outlined
                required
                :suffix="field.suffix"
                @keypress="$helpers.preventNonNumericalVal($event)"
            />
        </div>
        <div v-for="field in boolFields" :id="field.id" :key="field.name" class="pb-2">
            <v-checkbox
                v-model="preferences[field.name]"
                class="v-checkbox--mariadb pa-0 ma-0"
                dense
                :class="[field.name]"
                color="primary"
                hide-details="auto"
            >
                <template v-slot:label>
                    <label class="v-label pointer">{{ $mxs_t(field.name) }}</label>
                    <v-icon
                        v-if="field.icon"
                        class="ml-1 material-icons-outlined pointer"
                        size="16"
                        :color="field.iconColor"
                        @mouseenter="showInfoTooltip({ ...field, activator: `#${field.id}` })"
                        @mouseleave="rmInfoTooltip"
                    >
                        {{ field.icon }}
                    </v-icon>
                </template>
            </v-checkbox>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import RowLimitCtr from '@wkeComps/QueryEditor/RowLimitCtr.vue'
export default {
    name: 'pref-fields',
    components: { RowLimitCtr },
    props: {
        value: { type: Object, required: true },
        data: { type: Object, required: true },
    },
    computed: {
        preferences: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        numericFields() {
            return this.$typy(this.data, 'numericFields').safeArray.map(field => ({
                ...field,
                id: `activator_${this.$helpers.uuidv1()}`,
            }))
        },
        boolFields() {
            return this.$typy(this.data, 'boolFields').safeArray.map(field => ({
                ...field,
                id: `activator_${this.$helpers.uuidv1()}`,
            }))
        },
    },
    methods: {
        validatePositiveNumber({ v, inputName }) {
            if (this.$typy(v).isEmptyString)
                return this.$mxs_t('errors.requiredInput', { inputName })
            if (v <= 0) return this.$mxs_t('errors.largerThanZero', { inputName })
            if (v > 0) return true
            return false
        },
        showInfoTooltip(data) {
            this.$emit('tooltip', data)
        },
        rmInfoTooltip() {
            this.$emit('tooltip', undefined)
        },
    },
}
</script>
<style lang="scss" scoped>
.pref-fields-ctr {
    min-height: 360px;
    max-height: 520px;
}
</style>
