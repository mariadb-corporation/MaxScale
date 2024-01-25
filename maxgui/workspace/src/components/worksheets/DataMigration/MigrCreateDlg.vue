<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="$mxs_t('newMigration')"
        minBodyWidth="512px"
        :saveText="migr_dlg.type"
    >
        <template v-slot:form-body>
            <slot name="form-prepend" />
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
import { mapMutations, mapState } from 'vuex'
import { MIGR_DLG_TYPES } from '@wsSrc/constants'

export default {
    name: 'migr-create-dlg',
    props: {
        handleSave: { type: Function, required: true },
    },
    data() {
        return { name: '' }
    },
    computed: {
        ...mapState({ migr_dlg: state => state.mxsWorkspace.migr_dlg }),
        isOpened: {
            get() {
                const { type, is_opened } = this.migr_dlg
                return type === MIGR_DLG_TYPES.CREATE ? is_opened : false
            },
            set(v) {
                this.SET_MIGR_DLG({ ...this.migr_dlg, is_opened: v })
            },
        },
    },
    watch: {
        isOpened(v) {
            if (v) this.name = this.$mxs_t('newMigration').toUpperCase()
        },
    },
    methods: {
        ...mapMutations({ SET_MIGR_DLG: 'mxsWorkspace/SET_MIGR_DLG' }),
        onSave() {
            this.handleSave(this.name)
        },
    },
}
</script>
