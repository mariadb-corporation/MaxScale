<template>
    <!-- Use input.native to update value as a workaround
         for https://github.com/vuetifyjs/vuetify/issues/4679
    -->
    <v-combobox
        v-model.number="rowLimit"
        outlined
        dense
        class="vuetify-input--override mariadb-select-input row-limit-dropdown row-limit-dropdown--fieldset-border"
        :menu-props="{
            contentClass: 'mariadb-select-v-menu',
            bottom: true,
            offsetY: true,
        }"
        :rules="[v => validate(v)]"
        v-bind="{ ...$attrs }"
        @input.native="onInput"
        @keypress="$helpers.preventNonNumericalVal($event)"
    >
        <template v-slot:prepend-inner>
            <slot name="prepend-inner" />
        </template>
    </v-combobox>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

export default {
    name: 'row-limit',
    inheritAttrs: false,
    props: {
        value: { type: Number, required: true },
    },
    computed: {
        rowLimit: {
            get() {
                return this.value
            },
            set(v) {
                if (this.$typy(v).isNumber && this.$typy(v).safeNumber > 0) this.$emit('input', v)
            },
        },
    },
    methods: {
        validate(value) {
            const v = Number(value)
            if (this.$typy(v).isNull)
                return this.$mxs_t('errors.requiredInput', { inputName: this.$mxs_t('rowLimit') })
            else if (v <= 0) return this.$mxs_t('errors.largerThanZero', { inputName: 'Value' })
            else if (!this.$typy(v).isNumber) return this.$mxs_t('errors.nonInteger')
            return true
        },
        onInput(evt) {
            this.rowLimit = Number(evt.srcElement.value)
        },
    },
}
</script>

<style lang="scss">
.row-limit-dropdown {
    .v-input__control {
        .v-text-field__prefix {
            color: $small-text;
        }
    }
}
.row-limit-dropdown--fieldset-border {
    .v-input__control {
        fieldset {
            border: thin solid $accent-dark !important;
        }
    }
}
</style>
