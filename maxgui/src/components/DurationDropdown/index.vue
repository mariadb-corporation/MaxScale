<template>
    <div class="duration-dropdown d-inline-block">
        <label
            v-if="label"
            class="d-block field__label mxs-color-helper text-small-text label-required"
            :class="{ 'label-required': required }"
        >
            {{ label }}
        </label>
        <v-text-field
            v-model.trim="input.value"
            class="vuetify-input--override error--text__bottom duration-dropdown__num-input v-text-field--with-bottom-details"
            type="text"
            single-line
            outlined
            required
            dense
            :rules="rules"
            :disabled="disabled"
            v-bind="{ ...$attrs }"
            @keypress="$helpers.preventNonNumericalVal($event)"
        >
            <template v-slot:append>
                <v-select
                    v-model="input.suffix"
                    class="vuetify-input--override v-select--mariadb duration-dropdown__num-input--select"
                    :height="$attrs.height"
                    :menu-props="{
                        contentClass: 'v-select--menu-mariadb',
                        bottom: true,
                        offsetY: true,
                    }"
                    :items="DURATION_SUFFIXES"
                    outlined
                    dense
                    :disabled="disabled"
                />
            </template>
        </v-text-field>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { DURATION_SUFFIXES } from '@src/constants'

export default {
    name: 'duration-dropdown',
    inheritAttrs: false,
    props: {
        duration: { type: String, required: false }, // e.g. 1ms 1s 1m 1h
        label: { type: String, default: '' },
        disabled: { type: Boolean, default: false },
        required: { type: Boolean, default: false },
        validationHandler: { type: Function, required: true },
    },
    data() {
        return {
            input: { value: null, suffix: 'm' },
            rules: [val => this.validationHandler(val, this.input.suffix)],
        }
    },
    watch: {
        input: {
            deep: true,
            handler(v) {
                const newDuration = `${v.value}${v.suffix}`
                if (newDuration !== this.duration) this.$emit('change', newDuration)
            },
        },
        'input.suffix'() {
            // trigger validation handler
            this.rules = []
            this.$nextTick(() => {
                this.rules = [val => this.validationHandler(val, this.input.suffix)]
            })
        },
        duration: {
            handler(v) {
                this.parseDuration(v)
            },
        },
    },
    created() {
        this.DURATION_SUFFIXES = DURATION_SUFFIXES
    },
    mounted() {
        if (this.duration) this.parseDuration(this.duration)
    },
    methods: {
        parseDuration(duration) {
            this.input.suffix = duration.replace(/[0-9]|(null)+/g, '')
            this.input.value = duration.replace(this.input.suffix, '')
        },
    },
}
</script>

<style lang="scss" scoped>
$input-size: 0.875rem;
.duration-dropdown {
    width: 100%;
    .duration-dropdown__label {
        font-size: 0.6875rem;
    }
    ::v-deep .v-input--is-disabled {
        opacity: 0.5;
    }
    ::v-deep .duration-dropdown__num-input {
        .v-input__append-inner {
            margin-top: 0 !important;
        }
        .v-input__control {
            .v-input__slot {
                padding: 0 0px 0 12px;
            }
        }
        &--select {
            border-radius: 0px;
            .v-select__selections input {
                display: none;
            }
            .v-input__control {
                min-width: 20px;

                .v-input__slot {
                    padding: 0 0px 0 9px !important;
                    .v-select__slot {
                        .v-select__selection.v-select__selection--comma {
                            text-align: center;
                            width: 100%;
                        }
                    }
                    .v-input__append-inner {
                        padding-left: 0px !important;
                    }
                }
                fieldset {
                    border-right: none;
                    border-top: none;
                    border-bottom: none;
                }
            }
        }
    }
}
</style>
