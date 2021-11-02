<template>
    <v-dialog
        v-model="isDlgOpened"
        overlay-color="navigation"
        overlay-opacity="0.6"
        width="unset"
        content-class="base-dialog"
        persistent
        :scrollable="scrollable"
        eager
        @keydown.enter="allowEnterToSubmit ? keydownHandler($event) : null"
        @input="$emit('input', $event)"
    >
        <v-card
            class="v-card-custom"
            :min-width="minBodyWidth"
            :max-width="isDynamicWidth ? 'unset' : minBodyWidth"
        >
            <v-card-title class="v-card-title_padding">
                <h3 class="font-weight-light color text-deep-ocean">
                    {{ title }}
                </h3>
                <v-btn v-if="!isForceAccept" class="close" icon @click="close">
                    <v-icon size="20" color="navigation"> $vuetify.icons.close</v-icon>
                </v-btn>
            </v-card-title>
            <v-card-text class="v-card-text_padding">
                <slot name="body"></slot>
                <v-divider v-if="hasFormDivider" class="divider" />
                <v-form
                    ref="form"
                    v-model="isFormValid"
                    :lazy-validation="lazyValidation"
                    class="mt-4"
                >
                    <slot name="form-body"></slot>
                </v-form>
            </v-card-text>
            <v-card-actions class="v-card-actions_padding color border-top-reflection">
                <slot :cancel="cancel" :save="save" :isSaveDisabled="isSaveDisabled" name="actions">
                    <v-spacer></v-spacer>
                    <v-btn
                        v-if="!isForceAccept"
                        small
                        height="36"
                        color="primary"
                        class="cancel font-weight-medium px-7 text-capitalize"
                        rounded
                        outlined
                        depressed
                        @click="cancel"
                    >
                        {{ $t(cancelText) }}
                    </v-btn>
                    <v-btn
                        small
                        height="36"
                        color="primary"
                        class="save font-weight-medium px-7 text-capitalize"
                        rounded
                        depressed
                        :disabled="isSaveDisabled"
                        @click="save"
                    >
                        {{ $t(saveText) }}
                    </v-btn>
                </slot>
            </v-card-actions>
        </v-card>
    </v-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Events
 * is-form-valid?: (boolean)
 * on-cancel?: (function)
 * on-close?: (function)
 * after-cancel?: (function)
 * after-close: (function)
 */
import { mapMutations } from 'vuex'
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'

export default {
    name: 'base-dialog',
    props: {
        value: { type: Boolean, required: true },
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
    },
    data() {
        return {
            isFormValid: true,
        }
    },
    computed: {
        isDlgOpened: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        isSaveDisabled() {
            return this.hasSavingErr || !this.hasChanged || !this.isFormValid
        },
    },
    watch: {
        isFormValid(v) {
            this.$emit('is-form-valid', v)
        },
    },
    methods: {
        ...mapMutations(['SET_OVERLAY_TYPE']),
        async closeDialog() {
            this.$emit('input', false)
            /** Workaround to ensure dialog is closed completely to emit after close events
             * https://github.com/vuetifyjs/vuetify/blob/master/packages/vuetify/src/components/VDialog/VDialog.sass
             */
            await this.$help.delay(300) // wait until dialog transition is done
        },
        async cancel() {
            this.$emit('on-cancel')
            this.cleanUp()
            await this.closeDialog()
            this.$emit('after-cancel')
        },
        async close() {
            this.$emit('on-close')
            await this.closeDialog()
            this.$emit('after-close')
        },
        async keydownHandler() {
            if (this.isFormValid && this.hasChanged) await this.save()
        },
        cleanUp() {
            if (this.$refs.form) {
                this.$refs.form.reset()
                this.$refs.form.resetValidation()
            }
        },
        async waitClose() {
            // wait time out for loading animation
            await this.$help.delay(600).then(() => this.SET_OVERLAY_TYPE(null))
            this.cleanUp()
            await this.closeDialog()
        },
        async handleCloseImmediate() {
            await this.closeDialog()
            this.SET_OVERLAY_TYPE(null)
        },
        async save() {
            await this.$refs.form.validate()
            if (!this.isFormValid) {
                let invalidEles = document.getElementsByClassName('v-messages__message')
                return invalidEles[0].scrollIntoView({
                    behavior: 'smooth',
                    block: 'center',
                    inline: 'start',
                })
            } else {
                this.SET_OVERLAY_TYPE(OVERLAY_TRANSPARENT_LOADING)
                if (!this.hasSavingErr && this.closeImmediate) await this.handleCloseImmediate()
                await this.onSave()
                if (!this.closeImmediate) {
                    if (!this.hasSavingErr) await this.waitClose()
                    else this.SET_OVERLAY_TYPE(null)
                }
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.base-dialog {
    .close {
        position: absolute;
        top: 18px;
        right: 18px;
    }
    $paddingLeft: 62px;
    .v-card-title_padding {
        padding: 52px $paddingLeft 16px;
        h3 {
            word-break: break-word;
        }
    }
    .v-card-text_padding {
        padding: 0px $paddingLeft 48px;
    }
    .v-card-actions_padding {
        padding: 30px $paddingLeft 36px;
    }
    .divider {
        max-width: calc(100% + 124px);
        width: calc(100% + 124px);

        margin: 24px 0px 24px -62px;
    }
}
</style>
