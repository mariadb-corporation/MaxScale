<template>
    <v-row class="ma-n1">
        <slot name="prepend" />
        <v-col cols="12" md="6" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('databaseType') }}
            </label>
            <v-select
                v-model="src.type"
                :items="ODBC_DB_TYPES"
                item-text="text"
                item-value="id"
                name="databaseType"
                outlined
                class="vuetify-input--override v-select--mariadb error--text__bottom"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                :placeholder="$mxs_t('selectDbType')"
                :rules="requiredRule($mxs_t('databaseType'))"
                hide-details="auto"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <timeout-input v-model.number="src.timeout" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('driver') }}
            </label>
            <v-select
                v-model="src.driver"
                :items="drivers"
                item-text="id"
                item-value="id"
                name="driver"
                outlined
                class="vuetify-input--override v-select--mariadb error--text__bottom"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                :placeholder="$mxs_t('selectOdbcDriver')"
                :rules="requiredRule($mxs_t('driver'))"
                hide-details="auto"
                :disabled="isAdvanced"
                :error-messages="drivers.length ? '' : $mxs_t('errors.noDriversFound')"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-txt-field-with-label
                v-model.trim="src.db"
                :label="isGeneric ? $mxs_t('catalog') : $mxs_t('database')"
                :required="shouldRequireField"
                :customErrMsg="
                    isGeneric ? $mxs_t('errors.requiredCatalog') : $mxs_t('errors.requiredDb')
                "
                :validate-on-blur="true"
                :disabled="isAdvanced"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-txt-field-with-label
                v-model.trim="src.server"
                :label="$mxs_t('hostname/IP')"
                :required="true"
                :disabled="isAdvanced"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-txt-field-with-label
                v-model.trim="src.port"
                :label="$mxs_t('port')"
                :required="true"
                :disabled="isAdvanced"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <uid-input v-model.trim="src.user" :disabled="isAdvanced" name="odbc--uid" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <pwd-input v-model.trim="src.password" :disabled="isAdvanced" name="odbc--pwd" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <v-switch
                v-model="isAdvanced"
                label="Advanced"
                class="v-switch--mariadb ma-0 pt-3"
                hide-details
            />
        </v-col>
        <v-col v-if="isAdvanced" cols="12" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('connStr') }}
            </label>
            <v-textarea
                v-model="src.connection_string"
                class="v-textarea--mariadb vuetify-input--override error--text__bottom"
                auto-grow
                outlined
                rows="1"
                row-height="15"
                :disabled="!isAdvanced"
                :rules="requiredRule($mxs_t('connStr'))"
            />
        </v-col>
    </v-row>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import PwdInput from '@wkeComps/PwdInput.vue'
import UidInput from '@wkeComps/UidInput.vue'
import TimeoutInput from '@wkeComps/TimeoutInput.vue'
import queryHelper from '@wsSrc/store/queryHelper'
import { mapState } from 'vuex'

export default {
    name: 'odbc-form',
    components: { PwdInput, UidInput, TimeoutInput },
    props: {
        value: { type: Object, required: true },
        drivers: { type: Array, required: true },
    },
    data() {
        return {
            isAdvanced: false,
            src: {
                type: '',
                timeout: 30,
                driver: '',
                server: '',
                port: '',
                user: '',
                password: '',
                db: '',
                connection_string: '',
            },
        }
    },
    computed: {
        ...mapState({
            ODBC_DB_TYPES: state => state.mxsWorkspace.config.ODBC_DB_TYPES,
        }),
        shouldRequireField() {
            const { type } = this.src
            if (type === 'postgresql' || this.isGeneric) return true
            return false
        },
        isGeneric() {
            return this.src.type === 'generic'
        },
        generatedConnStr() {
            const {
                driver = '',
                server = '',
                port = '',
                user = '',
                password = '',
                db = '',
            } = this.src
            return queryHelper.genConnStr({ driver, server, port, user, password, db })
        },
    },
    watch: {
        generatedConnStr: {
            immediate: true,
            handler(v) {
                this.src.connection_string = v
            },
        },
        src: {
            immediate: true,
            deep: true,
            handler(v) {
                this.$emit('input', v)
            },
        },
    },
    methods: {
        requiredRule(inputName) {
            return [val => !!val || this.$mxs_t('errors.requiredInput', { inputName })]
        },
    },
}
</script>
