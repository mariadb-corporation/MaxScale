<template>
    <base-dialog
        v-model="isOpened"
        :onSave="onSave"
        title="Open connection"
        :hasChanged="hasChanged"
        :lazyValidation="false"
        minBodyWidth="512px"
        :onCancel="onCancel"
        :onClose="onClose"
        :hasSavingErr="hasSavingErr"
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

        <template v-slot:form-body>
            <v-container>
                <v-row>
                    <v-col cols="12" md="12" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            Target resource
                        </label>
                        <v-select
                            v-model="selectedResourceType"
                            :items="resourceTypes"
                            name="resourceName"
                            outlined
                            dense
                            class="std mariadb-select-input error--text__bottom"
                            :menu-props="{
                                contentClass: 'mariadb-select-v-menu',
                                bottom: true,
                                offsetY: true,
                            }"
                            hide-details
                            :rules="[
                                v => !!v || $t('errors.requiredInput', { inputName: 'This field' }),
                            ]"
                            required
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
                    </v-col>
                    <v-col v-if="selectedResourceType" cols="12" md="12" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            Resource name
                        </label>
                        <select-dropdown
                            class="mt-2"
                            :entityName="selectedResourceType"
                            :items="resourceItems"
                            clearable
                            showPlaceHolder
                            @get-selected-items="selectedResource = $event"
                        />
                    </v-col>

                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            Username
                        </label>
                        <v-text-field
                            id="db-user"
                            v-model="body.user"
                            :rules="rules.user"
                            class="std error--text__bottom"
                            name="db-user"
                            dense
                            outlined
                            required
                        />
                    </v-col>

                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text label-required">
                            Password
                        </label>
                        <v-text-field
                            id="db-password"
                            v-model="body.password"
                            :rules="rules.password"
                            :type="isPwdVisible ? 'text' : 'password'"
                            class="std error--text__bottom"
                            name="db-password"
                            autocomplete="new-password"
                            dense
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
                            Database
                        </label>
                        <v-text-field
                            v-model="body.db"
                            class="std error--text__bottom"
                            name="db"
                            dense
                            outlined
                            required
                        />
                    </v-col>
                    <v-col cols="12" md="6" class="pa-1">
                        <label class="field__label color text-small-text">
                            Timeout
                        </label>
                        <v-text-field
                            v-model="body.timeout"
                            type="number"
                            class="std error--text__bottom"
                            name="timeout"
                            dense
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
import { mapActions, mapState } from 'vuex'

export default {
    name: 'connection-dialog',
    props: {
        value: { type: Boolean, required: true },
        handleSave: { type: Function, required: true },
        onCancel: { type: Function, required: true },
        onClose: { type: Function, required: true },
    },
    data() {
        return {
            selectedResourceType: '',
            selectedResource: [],
            hasChanged: false,
            isPwdVisible: false,
            body: {
                user: '',
                password: '',
                db: '',
                timeout: 10,
            },
            rules: {
                user: [val => !!val || this.$t('errors.requiredInput', { inputName: 'Username' })],
                password: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: 'Password' }),
                ],
            },
            isFormValid: false,
            resourceTypes: ['servers', 'services', 'listeners'],
        }
    },
    computed: {
        ...mapState({
            rc_target_names_map: state => state.query.rc_target_names_map,
            conn_err_state: state => state.query.conn_err_state,
        }),
        isOpened: {
            get() {
                if (this.value) this.$emit('on-open')
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        resourceItems() {
            return this.rc_target_names_map[this.selectedResourceType] || []
        },
        hasSavingErr() {
            return this.conn_err_state
        },
    },
    watch: {
        selectedResourceType(v) {
            if (v) this.handleResourceSelect(v)
        },
    },
    methods: {
        ...mapActions({
            fetchRcTargetNames: 'query/fetchRcTargetNames',
        }),
        async handleResourceSelect(val) {
            // fetch if it's not been fetched
            if (!this.rc_target_names_map[val]) await this.fetchRcTargetNames(val)
        },
        async onSave() {
            const { id: resourceName = null } = this.selectedResource[0] || {}
            await this.handleSave({
                target: resourceName,
                ...this.body,
            })
        },
    },
}
</script>

<style lang="scss" scoped>
$label-size: 0.625rem;
$input-size: 0.875rem;
.field__label {
    font-size: $label-size;
}
</style>
