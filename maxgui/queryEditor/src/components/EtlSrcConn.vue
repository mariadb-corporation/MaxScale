<template>
    <v-row class="ma-n1">
        <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light mt-n3">
            {{ $mxs_t('source') }}
        </h3>
        <v-col cols="12" class="pa-1">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('databaseType') }}
            </label>
            <v-select
                v-model="src.type"
                :items="ETL_SUPPORT_DB_TYPES"
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
                :rules="requiredRule($mxs_t('driver'))"
                hide-details="auto"
                :disabled="isAdvanced"
                :error-messages="drivers.length ? '' : $mxs_t('errors.noDriversFound')"
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
            <uid-input v-model.trim="user" :disabled="isAdvanced" name="etl-src-uid" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <pwd-input v-model.trim="password" :disabled="isAdvanced" name="etl-src-pwd" />
        </v-col>
        <v-col cols="12" md="6" class="pa-1">
            <v-switch
                v-model="isAdvanced"
                label="Advanced"
                class="v-switch--mariadb ma-0 pt-3"
                hide-details
            />
        </v-col>
        <v-col cols="12" class="pa-1">
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
import { mapState } from 'vuex'

export default {
    name: 'etl-src-conn',
    components: { UidInput, PwdInput, DbInput },
    props: {
        /**
         * @property {string} type - database type
         * @property {string} connection_string
         */
        value: { type: Object, required: true }, // connection_string
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
        ...mapState({
            ETL_SUPPORT_DB_TYPES: state => state.mxsWorkspace.config.ETL_SUPPORT_DB_TYPES,
        }),
        src: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        shouldRequireDb() {
            if (this.src.type === 'postgresql') return true
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
                this.src.connection_string = v
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
