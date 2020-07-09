<template>
    <v-dialog
        v-model="computeShowDialog"
        overlay-color="#424F62"
        overlay-opacity="0.6"
        width="unset"
        content-class="base-dialog"
        persistent
        :scrollable="scrollable"
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
                    <v-icon size="20" color="#424F62"> $vuetify.icons.close</v-icon>
                </v-btn>
            </v-card-title>
            <v-card-text class="v-card-text_padding">
                <v-form ref="form" v-model="isFormValid" lazy-validation class="mt-4">
                    <slot name="body"></slot>
                </v-form>
            </v-card-text>
            <v-card-actions class="v-card-actions_padding color border-top-reflection">
                <v-spacer></v-spacer>
                <slot :cancel="cancel" :save="save" name="actions">
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
                        :disabled="isSaveDisabled || !isFormValid"
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations } from 'vuex'
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'

export default {
    name: 'base-dialog',
    props: {
        minBodyWidth: { type: String, default: '466px' },
        isDynamicWidth: { type: Boolean, default: false },
        scrollable: { type: Boolean, default: true },
        title: { type: String, required: true },
        // passed when using v-model
        value: { type: Boolean, required: true },
        /* These functions are requires since the computeShowDialog depends on the value props.
        The open/close of the dialog completely controlled by the parent component.
        */
        onClose: { type: Function, required: true },
        onCancel: { type: Function, required: true },
        onSave: { type: Function, required: true },

        cancelText: { type: String, default: 'cancel' },
        saveText: { type: String, default: 'save' },
        // manually control btn disabled
        isSaveDisabled: { type: Boolean, default: false },
        // if isForceAccept===true, cancel and close btn won't be rendered
        isForceAccept: { type: Boolean, default: false },
    },
    data() {
        return {
            isOpen: false,
            isFormValid: true,
        }
    },
    computed: {
        computeShowDialog: {
            // get value from props
            get() {
                return this.value
            },
            // set the value to isOpen property in data
            set(value) {
                this.isOpen = value
            },
        },
    },

    methods: {
        ...mapMutations(['showOverlay', 'hideOverlay']),
        cancel() {
            this.$refs.form.reset()
            this.$refs.form.resetValidation()
            this.onCancel && this.onCancel()
        },
        close() {
            this.onClose && this.onClose()
        },

        async save() {
            let self = this
            await !self.$refs.form.validate()

            if (!self.isFormValid) {
                let invalidEles = document.getElementsByClassName('v-messages__message')
                return invalidEles[0].scrollIntoView({
                    behavior: 'smooth',
                    block: 'center',
                    inline: 'start',
                })
            } else {
                if (self.onSave) {
                    self.showOverlay(OVERLAY_TRANSPARENT_LOADING)
                    await self.onSave()
                    if (self.$refs.form) {
                        self.$refs.form.reset()
                        self.$refs.form.resetValidation()
                    }
                    // wait time out for loading animation
                    await self.$help.delay(600).then(() => self.hideOverlay())
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
    }
    .v-card-text_padding {
        padding: 0px $paddingLeft 48px;
    }
    .v-card-actions_padding {
        padding: 30px $paddingLeft 36px;
    }
}
</style>
