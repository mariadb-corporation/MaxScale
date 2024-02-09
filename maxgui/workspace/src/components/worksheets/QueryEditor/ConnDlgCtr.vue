<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="`${$mxs_t('connectTo')}...`"
        :lazyValidation="false"
        minBodyWidth="512px"
        :hasSavingErr="hasSavingErr"
        hasFormDivider
        @is-form-valid="isFormValid = $event"
        v-on="$listeners"
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
                {{ $mxs_t('connect') }}
            </v-btn>
        </template>
        <template v-if="isOpened" v-slot:body>
            <v-select
                v-model="resourceType"
                :items="resourceTypes"
                name="resourceName"
                outlined
                class="mt-4 vuetify-input--override v-select--mariadb error--text__bottom resource-type-dropdown"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                hide-details="auto"
            >
                <template v-slot:selection="{ item }">
                    <div class="v-select__selection v-select__selection--comma">
                        {{ $helpers.resourceTxtTransform(item) }}
                    </div>
                </template>
                <template v-slot:item="{ item, on, attrs }">
                    <v-list-item-title v-bind="attrs" v-on="on">
                        {{ $helpers.resourceTxtTransform(item) }}
                    </v-list-item-title>
                </template>
            </v-select>
        </template>
        <template v-slot:form-body>
            <v-container class="pa-1">
                <v-row class="my-0 mx-n1">
                    <v-col v-if="resourceType" cols="12" md="12" class="pa-1">
                        <label class="field__label mxs-color-helper text-small-text label-required">
                            {{
                                $mxs_t('resourceLabelName', {
                                    resourceName: $helpers.resourceTxtTransform(resourceType),
                                })
                            }}
                        </label>
                        <mxs-select
                            v-model="selectedResource"
                            class="resource-dropdown"
                            :items="resourceItems"
                            :entityName="resourceType"
                            clearable
                            showPlaceHolder
                            required
                            :errorMessages="errRsrcMsg"
                        />
                    </v-col>

                    <v-col cols="12" md="6" class="pa-1">
                        <uid-input v-model.trim="body.user" name="db-user" />
                    </v-col>
                    <v-col cols="12" md="6" class="pa-1">
                        <pwd-input v-model.trim="body.password" name="db-password" />
                    </v-col>

                    <v-col cols="12" md="6" class="pa-1">
                        <mxs-txt-field-with-label
                            v-model.trim="body.db"
                            :label="$mxs_t('database')"
                        />
                    </v-col>
                    <v-col cols="12" md="6" class="pa-1">
                        <timeout-input v-model.number="body.timeout" />
                    </v-col>
                </v-row>
            </v-container>
        </template>
    </mxs-dlg>
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
import { mapActions, mapMutations, mapState } from 'vuex'
import QueryConn from '@wsModels/QueryConn'
import TimeoutInput from '@wkeComps/TimeoutInput.vue'
import PwdInput from '@wkeComps/PwdInput.vue'
import UidInput from '@wkeComps/UidInput.vue'

export default {
    name: 'conn-dlg-ctr',
    components: { TimeoutInput, PwdInput, UidInput },
    props: {
        value: { type: Boolean, required: true },
    },
    data() {
        return {
            resourceType: '',
            selectedResource: {},
            body: {
                user: '',
                password: '',
                db: '',
                timeout: 300,
            },
            isFormValid: false,
            resourceTypes: ['listeners', 'servers', 'services'],
            errRsrcMsg: '',
        }
    },
    computed: {
        ...mapState({
            rc_target_names_map: state => state.queryConnsMem.rc_target_names_map,
            conn_err_state: state => state.queryConnsMem.conn_err_state,
            pre_select_conn_rsrc: state => state.queryConnsMem.pre_select_conn_rsrc,
        }),
        isOpened: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        resourceItems() {
            return this.rc_target_names_map[this.resourceType] || []
        },
        hasSavingErr() {
            return this.conn_err_state
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            async handler(v) {
                if (v) {
                    let rscType = this.resourceTypes[0] // use the first one as default
                    if (this.pre_select_conn_rsrc) rscType = this.pre_select_conn_rsrc.type
                    this.resourceType = rscType
                } else {
                    this.$nextTick(() => Object.assign(this.$data, this.$options.data.apply(this)))
                    this.SET_PRE_SELECT_CONN_RSRC(null)
                }
            },
        },
        resourceType: {
            immediate: true,
            async handler(v) {
                if (v) {
                    await this.handleFetchRsrcs(v)
                    this.handleChooseDefRsrc(v)
                }
            },
        },
    },
    methods: {
        ...mapActions({
            fetchRcTargetNames: 'queryConnsMem/fetchRcTargetNames',
        }),
        ...mapMutations({
            SET_PRE_SELECT_CONN_RSRC: 'queryConnsMem/SET_PRE_SELECT_CONN_RSRC',
        }),
        async handleFetchRsrcs(rscType) {
            // fetch if it's not been fetched
            if (!this.rc_target_names_map[rscType]) await this.fetchRcTargetNames(rscType)
        },
        /**
         * This function handles automatically select default selectedResource.
         * It chooses the first item in resourceItems if pre_select_conn_rsrc has no value
         * @param {String} resourceType - resource type
         */
        handleChooseDefRsrc() {
            if (this.resourceItems.length) {
                if (this.pre_select_conn_rsrc) this.selectedResource = this.pre_select_conn_rsrc
                else this.selectedResource = this.resourceItems[0]
                this.errRsrcMsg = ''
            } else this.errRsrcMsg = ''
        },
        async onSave() {
            const { id: resourceName = null } = this.selectedResource
            await QueryConn.dispatch('openQueryEditorConn', {
                body: { target: resourceName, ...this.body },
                meta: { name: resourceName },
            })
        },
    },
}
</script>
