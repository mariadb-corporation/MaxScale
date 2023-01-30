<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="createEtlTask"
        :title="`${$mxs_t('newMigration')}`"
        minBodyWidth="512px"
        @is-form-valid="isFormValid = $event"
    >
        <template v-slot:actions="{ cancel, save }">
            <v-spacer />
            <v-btn
                small
                height="36"
                color="primary"
                class="cancel font-weight-medium px-7 text-capitalize"
                rounded
                outlined
                depressed
                @click="cancel"
            >
                {{ $mxs_t('cancel') }}
            </v-btn>
            <v-btn
                small
                height="36"
                color="primary"
                class="save font-weight-medium px-7 text-capitalize"
                rounded
                depressed
                :disabled="!isFormValid"
                @click="save"
            >
                {{ $mxs_t('create') }}
            </v-btn>
        </template>
        <template v-slot:form-body>
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('name') }}
            </label>
            <v-text-field
                v-model="name"
                :rules="[v => !!v || $mxs_t('errors.requiredInput', { inputName: $mxs_t('name') })]"
                required
                :height="36"
                autofocus
                class="vuetify-input--override error--text__bottom etl-task-name-input"
                dense
                outlined
            />
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'mgr-creation-dlg',
    data() {
        return {
            name: '',
            isFormValid: false,
        }
    },
    computed: {
        ...mapState({
            is_migr_dlg_opened: state => state.mxsWorkspace.is_migr_dlg_opened,
        }),
        isOpened: {
            get() {
                return this.is_migr_dlg_opened
            },
            set(v) {
                this.SET_IS_MIGR_DLG_OPENED(v)
            },
        },
    },
    watch: {
        isOpened(v) {
            if (v) this.name = 'New migration'
        },
    },
    methods: {
        ...mapMutations({
            SET_IS_MIGR_DLG_OPENED: 'mxsWorkspace/SET_IS_MIGR_DLG_OPENED',
        }),
        async createEtlTask() {
            await EtlTask.dispatch('insertEtlTask', this.name)
        },
    },
}
</script>
