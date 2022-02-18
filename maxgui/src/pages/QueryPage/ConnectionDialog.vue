<template>
    <base-dialog
        v-model="isOpened"
        :onSave="onSave"
        :title="`${$t('connectTo')}...`"
        :hasChanged="hasChanged"
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
                {{ $t('cancel') }}
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
                {{ $t('connect') }}
            </v-btn>
        </template>
        <template v-if="isOpened" v-slot:body>
            <v-select
                v-model="selectedResourceType"
                :items="resourceTypes"
                name="resourceName"
                outlined
                class="mt-4 std mariadb-select-input error--text__bottom resource-type-dropdown"
                :menu-props="{
                    contentClass: 'mariadb-select-v-menu',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                hide-details="auto"
            >
                <template v-slot:selection="{ item }">
                    <div class="v-select__selection v-select__selection--comma">
                        {{ $help.resourceTxtTransform(item) }}
                    </div>
                </template>
                <template v-slot:item="{ item, on, attrs }">
                    <div class="v-list-item__title" v-bind="attrs" v-on="on">
                        {{ $help.resourceTxtTransform(item) }}
                    </div>
                </template>
            </v-select>
        </template>
        <template v-slot:form-body>
            <v-container class="pa-1">
                <v-row class="my-0 mx-n1">
                    <v-col v-if="selectedResourceType" cols="12" md="12" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            {{
                                $t('resourceLabelName', {
                                    resourceName: $help.resourceTxtTransform(selectedResourceType),
                                })
                            }}
                        </label>
                        <select-dropdown
                            v-model="selectedResource"
                            class="resource-dropdown"
                            :items="resourceItems"
                            :entityName="selectedResourceType"
                            clearable
                            showPlaceHolder
                            required
                            :errorMessages="errRsrcMsg"
                        />
                    </v-col>

                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('username') }}
                        </label>
                        <v-text-field
                            id="db-user"
                            v-model="body.user"
                            :rules="rules.user"
                            class="std error--text__bottom user"
                            name="db-user"
                            autocomplete="new-username"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                        />
                    </v-col>

                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('password') }}
                        </label>
                        <v-text-field
                            id="db-password"
                            v-model="body.password"
                            :rules="rules.password"
                            :type="isPwdVisible ? 'text' : 'password'"
                            class="std error--text__bottom password"
                            name="db-password"
                            autocomplete="new-password"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                        >
                            <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
                                {{ isPwdVisible ? 'visibility_off' : 'visibility' }}
                            </v-icon>
                        </v-text-field>
                    </v-col>
                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text">
                            {{ $t('database') }}
                        </label>
                        <v-text-field
                            v-model="body.db"
                            class="std error--text__bottom"
                            name="db"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                        />
                    </v-col>
                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text">
                            {{ $t('timeout') }}
                        </label>
                        <v-text-field
                            v-model.number="body.timeout"
                            type="number"
                            class="std error--text__bottom timeout"
                            name="timeout"
                            dense
                            :height="36"
                            hide-details="auto"
                            outlined
                            required
                        />
                    </v-col>
                </v-row>
            </v-container>
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapMutations, mapState } from 'vuex'

export default {
    name: 'connection-dialog',
    props: {
        value: { type: Boolean, required: true },
        connOptions: { type: Array, required: true },
        handleSave: { type: Function, required: true },
    },
    data() {
        return {
            selectedResourceType: '',
            selectedResource: {},
            hasChanged: false,
            isPwdVisible: false,
            body: {
                user: '',
                password: '',
                db: '',
                timeout: 300,
            },
            rules: {
                user: [
                    val =>
                        !!val ||
                        this.$t('errors.requiredInput', { inputName: this.$t('username') }),
                ],
                password: [
                    val =>
                        !!val ||
                        this.$t('errors.requiredInput', { inputName: this.$t('password') }),
                ],
            },
            isFormValid: false,
            resourceTypes: ['listeners', 'servers', 'services'],
            errRsrcMsg: '',
        }
    },
    computed: {
        ...mapState({
            rc_target_names_map: state => state.query.rc_target_names_map,
            conn_err_state: state => state.query.conn_err_state,
            pre_select_conn_rsrc: state => state.query.pre_select_conn_rsrc,
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
            const selectedRsrcType = this.selectedResourceType
            // Get list of resource name that have been connected
            const connectedResourceNames = this.connOptions.reduce((acc, item) => {
                if (item.type === selectedRsrcType) {
                    acc.push(item.name)
                }
                return acc
            }, [])
            const allRsrcs = this.rc_target_names_map[selectedRsrcType] || []

            // Keep only resources that have not been connected
            const availRsrcs = allRsrcs.reduce((acc, rsrc) => {
                if (!connectedResourceNames.includes(rsrc.id)) {
                    acc.push(rsrc)
                }
                return acc
            }, [])
            return availRsrcs
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
                    this.selectedResourceType = rscType
                } // reset to initial state and bind this context
                else {
                    this.$nextTick(() => Object.assign(this.$data, this.$options.data.apply(this)))
                    this.SET_PRE_SELECT_CONN_RSRC(null)
                }
            },
        },
        selectedResourceType: {
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
            fetchRcTargetNames: 'query/fetchRcTargetNames',
        }),
        ...mapMutations({
            SET_PRE_SELECT_CONN_RSRC: 'query/SET_PRE_SELECT_CONN_RSRC',
        }),
        async handleFetchRsrcs(rscType) {
            // fetch if it's not been fetched
            if (!this.rc_target_names_map[rscType]) await this.fetchRcTargetNames(rscType)
        },
        /**
         * This function handles automatically select default selectedResource.
         * It chooses the first item in resourceItems if pre_select_conn_rsrc has no value
         * @param {String} selectedResourceType - resource type
         */
        handleChooseDefRsrc(selectedResourceType) {
            if (this.resourceItems.length) {
                if (this.pre_select_conn_rsrc) this.selectedResource = this.pre_select_conn_rsrc
                else this.selectedResource = this.resourceItems[0]
                this.errRsrcMsg = ''
            } else
                this.errRsrcMsg = this.$t('errors.existingRsrcConnection', {
                    resourceType: selectedResourceType,
                })
        },
        async onSave() {
            const { id: resourceName = null } = this.selectedResource
            await this.handleSave({
                body: { target: resourceName, ...this.body },
                resourceType: this.selectedResourceType,
            })
        },
    },
}
</script>
