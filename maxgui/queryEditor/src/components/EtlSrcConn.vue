<template>
    <v-col cols="12" md="6">
        <b>{{ $mxs_t('source') }}</b>
        <p v-if="!drivers.length" class="mxs-color-helper text-error mt-2">
            {{ $mxs_t('errors.noDriversFound') }}
        </p>
        <template v-else>
            <v-row class="my-0 mx-n1">
                <v-col cols="12" md="6" class="pa-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_t('driver') }}
                    </label>
                    <v-select
                        v-model="driver"
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
                        :rules="[
                            v =>
                                !!v ||
                                $mxs_t('errors.requiredInput', {
                                    inputName: $mxs_t('driver'),
                                }),
                        ]"
                        hide-details="auto"
                        :disabled="isAdvanced"
                    />
                </v-col>
                <v-col cols="12" md="6" class="pa-1">
                    <db-input
                        v-model.trim="db"
                        :required="shouldRequireDb"
                        :customErrMsg="$mxs_t('errors.requiredDb')"
                        :validate-on-blur="true"
                        :disabled="isAdvanced"
                    />
                </v-col>
                <v-col cols="12" md="6" class="pa-1">
                    <mxs-txt-field-with-label
                        v-model.trim="server"
                        :label="$mxs_t('hostname/IP')"
                        :required="true"
                        :disabled="isAdvanced"
                    />
                </v-col>
                <v-col cols="12" md="6" class="pa-1">
                    <mxs-txt-field-with-label
                        v-model.trim="port"
                        :label="$mxs_t('port')"
                        :required="true"
                        :disabled="isAdvanced"
                    />
                </v-col>
                <v-col cols="12" md="6" class="pa-1">
                    <uid-input v-model.trim="user" :disabled="isAdvanced" />
                </v-col>
                <v-col cols="12" md="6" class="pa-1">
                    <pwd-input v-model.trim="password" :disabled="isAdvanced" />
                </v-col>
            </v-row>
            <v-switch v-model="isAdvanced" label="Advanced" class="v-switch--mariadb" />
            <v-row class="mx-n1">
                <v-col cols="12" class="pa-1">
                    <label class="field__label mxs-color-helper text-small-text label-required">
                        {{ $mxs_t('connStr') }}
                    </label>
                    <v-textarea
                        v-model="connStr"
                        class="v-textarea--mariadb vuetify-input--override error--text__bottom"
                        auto-grow
                        outlined
                        rows="1"
                        row-height="15"
                        :disabled="!isAdvanced"
                        :rules="[
                            val =>
                                !!val ||
                                $mxs_t('errors.requiredInput', { inputName: $mxs_t('connStr') }),
                        ]"
                    />
                </v-col>
            </v-row>
        </template>
    </v-col>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import UidInput from './UidInput.vue'
import PwdInput from './PwdInput.vue'
import DbInput from './DbInput.vue'
import queryHelper from '@queryEditorSrc/store/queryHelper'

export default {
    name: 'etl-src-conn',
    components: { UidInput, PwdInput, DbInput },
    props: {
        value: { type: String, required: true }, // connection_string
        drivers: { type: Array, required: true },
    },
    data() {
        return {
            isAdvanced: false,
            driver: '',
            server: '',
            port: '',
            user: '',
            password: '',
            db: '',
        }
    },
    computed: {
        connStr: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        driverMap() {
            return this.$helpers.lodash.keyBy(this.drivers, 'id')
        },
        shouldRequireDb() {
            const driver = this.driverMap[this.driver]
            /**
             * PostgreSQL requires defining a database when creating a connection
             * There could be either an ANSI or Unicode version of PostgreSQL ODBC driver, so
             * checking the `driver` path if it includes `psqlodbc` would probably be enough to
             * know it's a PostgreSQL driver.
             */
            if (this.$typy(driver, 'attributes.driver').safeString.includes('psqlodbc')) return true
            return false
        },
        generatedConnStr() {
            const { driver = '', server = '', port = '', user = '', password = '', db = '' } = this
            return queryHelper.genConnStr({ driver, server, port, user, password, db })
        },
    },
    watch: {
        generatedConnStr: {
            immediate: true,
            handler(v) {
                this.connStr = v
            },
        },
    },
}
</script>
