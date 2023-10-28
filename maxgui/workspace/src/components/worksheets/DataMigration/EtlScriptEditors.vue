<template>
    <div class="fill-height d-flex flex-column">
        <div class="script-container d-flex flex-column fill-height">
            <etl-editor
                v-model="stagingRow.select"
                class="select-script"
                :class="stagingRow.select ? '' : 'mxs-color-helper all-border-error'"
                :label="$mxs_t('retrieveDataFromSrc')"
            />
            <etl-editor
                v-model="stagingRow.create"
                class="create-script"
                :class="stagingRow.create ? '' : 'mxs-color-helper all-border-error'"
                :label="$mxs_t('createObjInDest')"
                skipRegEditorCompleters
            />
            <etl-editor
                v-model="stagingRow.insert"
                class="insert-script"
                :class="stagingRow.insert ? '' : 'mxs-color-helper all-border-error'"
                :label="$mxs_t('insertDataInDest')"
                skipRegEditorCompleters
            />
            <div class="btn-ctr d-flex flex-row">
                <span
                    v-if="isInErrState"
                    class="v-messages theme--light error--text d-inline-block mt-2"
                >
                    {{ $mxs_t('errors.scriptCanNotBeEmpty') }}
                </span>
                <v-spacer />
                <v-btn
                    v-if="hasChanged"
                    small
                    height="36"
                    color="primary"
                    class="mr-2 font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    @click="$emit('on-discard')"
                >
                    {{ $mxs_t('discard') }}
                </v-btn>
            </div>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlEditor from '@wkeComps/DataMigration/EtlEditor.vue'
export default {
    name: 'etl-script-editors',
    components: { EtlEditor },
    props: {
        /**
         * @property {string} select
         * @property {string} create
         * @property {string} insert
         */
        value: { type: Object, required: true },
        hasChanged: { type: Boolean, required: true },
    },
    computed: {
        isInErrState() {
            const { select, create, insert } = this.stagingRow
            return Boolean(!select || !create || !insert)
        },
        stagingRow: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
}
</script>
<style lang="scss" scoped>
.field__label {
    font-size: 0.875rem !important;
}
.btn-ctr {
    height: 36px;
}
</style>
<style lang="scss">
.script-container {
    .create-script {
        flex-grow: 0.5;
    }
    .select-script,
    .insert-script {
        flex-grow: 0.25;
    }
}
</style>
