<template>
    <div class="lazy-input-ctr" @click="onFocus">
        <slot v-if="isInputShown" />
        <div
            v-else
            class="readonly-input-ctr d-flex align-center"
            :class="{
                'readonly-input-ctr--disabled': disabled,
                'readonly-input-ctr--error': isError,
                [`readonly-input-ctr-type-${type}`]: type,
            }"
            :style="{ height: `${height}px` }"
        >
            <template v-if="type === 'checkbox'">
                <v-simple-checkbox
                    :value="inputValue"
                    class="v-checkbox--mariadb-xs ma-0 pa-0"
                    :color="inputValue && !disabled ? 'primary' : ''"
                    :disabled="disabled"
                    @click="onFocus"
                />
            </template>
            <template v-else>
                <input
                    type="text"
                    :value="inputValue"
                    :disabled="disabled"
                    class="mxs-field-text-size"
                    @focus="onFocusReadonlyInput"
                />
                <v-icon
                    v-if="isSelectMenu"
                    :disabled="disabled"
                    :color="isError ? 'error' : ''"
                    class="pl-1 ml-auto"
                >
                    mdi-menu-down
                </v-icon>
            </template>
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'mxs-lazy-input',
    props: {
        value: { type: Boolean, required: true }, // model
        inputValue: { type: [String, Boolean], default: null },
        height: { type: Number, required: true },
        getInputRef: { type: Function, required: true },
        type: { type: String, default: 'text' }, // text, select, checkbox
        disabled: { type: Boolean, default: false },
        required: { type: Boolean, default: false },
        error: { type: Boolean, default: false }, // sync
    },
    computed: {
        isError: {
            get() {
                return this.error
            },
            set(v) {
                this.$emit('update:error', v)
            },
        },
        isInputShown: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        isSelectMenu() {
            return this.type === 'select'
        },
    },
    watch: {
        inputValue: {
            immediate: true,
            handler(v) {
                if (this.required) this.isError = Boolean(!v)
            },
        },
        isError: {
            immediate: true,
            handler(v) {
                if (v) this.isInputShown = true
            },
        },
    },
    methods: {
        onFocusReadonlyInput() {
            this.onFocus()
        },
        onFocus() {
            if (!this.disabled) {
                this.isInputShown = true
                if (this.type === 'checkbox') this.$emit('toggle-checkbox')
                else
                    this.$nextTick(() => {
                        const inputCtr = this.getInputRef()
                        if (inputCtr) {
                            inputCtr.focus()
                            if (this.isSelectMenu) {
                                inputCtr.activateMenu()
                                this.$nextTick(() => inputCtr.$refs.input.select())
                            }
                        }
                    })
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.lazy-input-ctr {
    width: 100%;
    .readonly-input-ctr {
        border: 1px solid $text-subtle;
        border-radius: 4px;
        padding: 0 11px;
        position: relative;
        max-width: 100%;
        min-width: 0;
        width: 100%;
        input {
            color: $navigation;
            min-width: 64px;
            &:focus {
                outline: none;
            }
        }
        &--disabled {
            opacity: 0.5;
            input {
                color: rgba(0, 0, 0, 0.38);
            }
        }
        &--error {
            border-color: $error;
        }
        &-type-checkbox {
            padding: 1px 1px 0 0;
            border: none;
        }
    }
}
</style>
