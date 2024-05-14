<template>
    <div class="fill-height d-flex align-center" @click.stop>
        <slot v-if="isInputShown" />
        <div
            v-else
            :class="readonlyInputClass"
            :style="{
                height: `${height}px`,
                paddingTop: isSelectMenu ? '2px' : 0,
            }"
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
                    :placeholder="placeholder"
                    :name="name"
                    autocomplete="off"
                    class="mxs-field-text-size text-truncate"
                    @click.stop
                    @focus="onFocus"
                />
                <v-icon
                    v-if="isSelectMenu"
                    :disabled="disabled"
                    :color="isError ? 'error' : ''"
                    class="pl-1"
                    @click.stop
                    @focus="onFocus"
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'mxs-lazy-input',
    props: {
        value: { type: Boolean, required: true }, // model
        inputValue: { type: [String, Boolean, Array], default: null },
        placeholder: { type: String, default: '' },
        height: { type: Number, required: true },
        getInputRef: { type: Function, required: true },
        type: { type: String, default: 'text' }, // text, select, checkbox
        disabled: { type: Boolean, default: false },
        required: { type: Boolean, default: false },
        error: { type: Boolean, default: false }, // sync
        name: { type: String, required: true },
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
        readonlyInputClass() {
            const className = 'lazy-input'
            return [
                `d-flex align-center justify-space-between`,
                className,
                `${className}-type-${this.type}`,
                this.disabled ? `${className}--disabled` : '',
                this.isError ? `${className}--error` : '',
            ]
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
        async onFocus(e) {
            /**
             * https://bugs.chromium.org/p/chromium/issues/detail?id=919540&q=selectionStart&can=2
             * A workaround to get selectionStart
             */
            await this.$helpers.delay(0)
            const selectionStart = e.target.selectionStart

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
                            } else
                                this.$nextTick(() =>
                                    inputCtr.$refs.input.setSelectionRange(
                                        selectionStart,
                                        selectionStart
                                    )
                                )
                        }
                    })
                this.$emit('on-focused', e)
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.lazy-input {
    width: 100%;
    border: 1px solid $text-subtle;
    border-radius: 4px;
    position: relative;
    max-width: 100%;
    min-width: 0;
    width: 100%;
    padding: 0 11px;
    input {
        width: 100%;
        color: $navigation;
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
</style>
