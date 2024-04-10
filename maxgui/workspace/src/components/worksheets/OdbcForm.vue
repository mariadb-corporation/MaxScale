<template>
    <v-row class="ma-n1">
        <slot name="prepend" />
        <v-col cols="12" md="6" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('databaseType') }}
            </label>
            <v-select
                v-model="form.type"
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
                data-test="database-type-dropdown"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-timeout-input v-model.number="form.timeout" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('driver') }}
            </label>
            <v-select
                v-model="form.driver"
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
                :error-messages="driverErrMsgs"
                data-test="driver-dropdown"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-label-field
                v-model.trim="form.db"
                :label="dbNameLabel"
                :required="isDbNameRequired"
                :customErrMsg="dbNameErrMsg"
                :validate-on-blur="true"
                :disabled="isAdvanced"
                data-test="database-name"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-label-field
                v-model.trim="form.server"
                :label="$mxs_t('hostname/IP')"
                :required="true"
                :disabled="isAdvanced"
                data-test="hostname"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-label-field
                v-model.trim="form.port"
                :label="$mxs_t('port')"
                :required="true"
                :disabled="isAdvanced"
                data-test="port"
            />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-uid-input v-model.trim="form.user" :disabled="isAdvanced" name="odbc--uid" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <mxs-pwd-input v-model.trim="form.password" :disabled="isAdvanced" name="odbc--pwd" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <v-switch
                v-model="isAdvanced"
                :label="$mxs_t('advanced')"
                class="v-switch--mariadb ma-0 pt-3"
                hide-details
            />
        </v-col>
        <v-col v-if="isAdvanced" cols="12" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('connStr') }}
            </label>
            <v-textarea
                v-model="form.connection_string"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { ODBC_DB_TYPES } from '@wsSrc/constants'

export default {
    name: 'odbc-form',
    props: {
        value: { type: Object, required: true },
        drivers: { type: Array, required: true },
    },
    data() {
        return {
            isAdvanced: false,
            form: {
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
        isDbNameRequired() {
            const { type } = this.form
            if (type === 'postgresql' || this.isGeneric) return true
            return false
        },
        isGeneric() {
            return this.form.type === 'generic'
        },
        generatedConnStr() {
            const {
                driver = '',
                server = '',
                port = '',
                user = '',
                password = '',
                db = '',
            } = this.form
            return this.genConnStr({ driver, server, port, user, password, db })
        },
        driverErrMsgs() {
            return this.drivers.length ? '' : this.$mxs_t('errors.noDriversFound')
        },
        dbNameLabel() {
            return this.isGeneric ? this.$mxs_t('catalog') : this.$mxs_t('database')
        },
        dbNameErrMsg() {
            return this.isGeneric
                ? this.$mxs_t('errors.requiredCatalog')
                : this.$mxs_t('errors.requiredDb')
        },
    },
    watch: {
        generatedConnStr: {
            immediate: true,
            handler(v) {
                this.form.connection_string = v
            },
        },
        form: {
            immediate: true,
            deep: true,
            handler(v) {
                this.$emit('input', v)
            },
        },
    },
    created() {
        this.ODBC_DB_TYPES = ODBC_DB_TYPES
    },
    methods: {
        requiredRule(inputName) {
            return [val => !!val || this.$mxs_t('errors.requiredInput', { inputName })]
        },
        /**
         * @param {string} param.driver
         * @param {string} param.server
         * @param {string} param.port
         * @param {string} param.user
         * @param {string} param.password
         * @param {string} [param.db] - required if driver is PostgreSQL
         * @returns {string}  ODBC connection_string
         */
        genConnStr({ driver, server, port, user, password, db }) {
            let connStr = `DRIVER=${driver};`
            connStr += `SERVER=${server};PORT=${port};UID=${user};PWD={${password}}`
            if (db) connStr += `;DATABASE=${db}`
            return connStr
        },
    },
}
</script>
