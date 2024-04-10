<template>
    <v-dialog
        v-model="isDlgOpened"
        overlay-color="navigation"
        overlay-opacity="0.6"
        width="unset"
        content-class="mxs-dlg"
        persistent
        :scrollable="scrollable"
        eager
        @keydown.enter="allowEnterToSubmit ? keydownHandler($event) : null"
        @input="$emit('input', $event)"
    >
        <v-card
            class="v-card--mariadb"
            :min-width="minBodyWidth"
            :max-width="isDynamicWidth ? 'unset' : minBodyWidth"
        >
            <v-card-title class="v-card-title_padding">
                <h3 class="font-weight-light mxs-color-helper text-deep-ocean">
                    {{ title }}
                </h3>
                <v-btn
                    v-if="!isForceAccept && showCloseBtn"
                    class="close"
                    data-test="close-btn"
                    icon
                    @click="close"
                >
                    <v-icon size="20" color="navigation"> $vuetify.icons.mxs_close</v-icon>
                </v-btn>
            </v-card-title>
            <v-card-text :class="bodyCtrClass">
                <div v-if="$slots.body" data-test="body-slot-ctr" class="body-padding-x">
                    <slot name="body" />
                </div>
                <v-divider v-if="hasFormDivider" class="my-6" />
                <div v-else class="mt-4" />
                <v-form
                    ref="form"
                    v-model="isFormValid"
                    :lazy-validation="lazyValidation"
                    :class="formClass"
                    data-test="form-body-slot-ctr"
                >
                    <slot name="form-body" />
                </v-form>
            </v-card-text>
            <v-card-actions
                class="v-card-actions_padding mxs-color-helper border-top-separator"
                data-test="action-ctr"
            >
                <slot name="action-prepend" />
                <v-spacer />
                <v-btn
                    v-if="!isForceAccept"
                    small
                    height="36"
                    color="primary"
                    class="cancel font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    data-test="cancel-btn"
                    @click="cancel"
                >
                    {{ $mxs_t(cancelText) }}
                </v-btn>
                <slot name="save-btn" :save="save" :isSaveDisabled="isSaveDisabled">
                    <v-btn
                        small
                        height="36"
                        color="primary"
                        class="save font-weight-medium px-7 text-capitalize"
                        rounded
                        depressed
                        :disabled="isSaveDisabled"
                        data-test="save-btn"
                        @click="save"
                    >
                        {{ $mxs_t(saveText) }}
                    </v-btn>
                </slot>
            </v-card-actions>
        </v-card>
    </v-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
 * Events
 * is-form-valid?: (boolean)
 * after-cancel?: (function)
 * after-close: (function)
 */
import { mapMutations } from 'vuex'
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'

export default {
    name: 'mxs-dlg',
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
        showCloseBtn: { type: Boolean, default: true },
        bodyCtrClass: { type: [String, Object, Array], default: 'px-0 pb-12' },
        formClass: { type: [String, Object, Array], default: 'body-padding-x' },
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
        ...mapMutations({ SET_OVERLAY_TYPE: 'mxsApp/SET_OVERLAY_TYPE' }),
        closeDialog() {
            this.$emit('input', false)
        },
        cancel() {
            this.cleanUp()
            this.closeDialog()
            this.$emit('after-cancel')
        },
        close() {
            this.closeDialog()
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
            await this.$helpers.delay(600).then(() => this.SET_OVERLAY_TYPE(null))
            this.cleanUp()
            this.closeDialog()
        },
        handleCloseImmediate() {
            this.closeDialog()
            this.SET_OVERLAY_TYPE(null)
        },
        async validateForm() {
            await this.$refs.form.validate()
        },
        async save() {
            await this.validateForm()
            if (!this.isFormValid) this.$helpers.scrollToFirstErrMsgInput()
            else {
                this.SET_OVERLAY_TYPE(OVERLAY_TRANSPARENT_LOADING)
                if (!this.hasSavingErr && this.closeImmediate) this.handleCloseImmediate()
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
.mxs-dlg {
    .close {
        position: absolute;
        top: 18px;
        right: 18px;
    }
    $paddingX: 62px;
    .v-card-title_padding {
        padding: 52px $paddingX 16px;
        h3 {
            word-break: break-word;
        }
    }
    .body-padding-x {
        padding: 0px $paddingX;
    }
    .v-card-actions_padding {
        padding: 30px $paddingX 36px;
    }
}
</style>
