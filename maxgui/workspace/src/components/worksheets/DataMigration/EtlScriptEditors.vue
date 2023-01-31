<template>
    <div class="fill-height d-flex flex-column relative">
        <div class="script-container d-flex flex-column fill-height">
            <etl-editor
                v-model="stagingRow.select"
                class="select-script-ctr flex-grow-1"
                :label="$mxs_t('retrieveDataFromSrc')"
                :editorClass="stagingRow.select ? '' : 'mxs-color-helper all-border-error'"
            />
            <etl-editor
                v-model="stagingRow.create"
                class="create-script-ctr flex-grow-1"
                :label="$mxs_t('createObjInDest')"
                :editorClass="`${stagingRow.create ? '' : 'mxs-color-helper all-border-error'}`"
                skipRegEditorCompleters
            />
            <etl-editor
                v-model="stagingRow.insert"
                class="insert-script-ctr flex-grow-1"
                :label="$mxs_t('insertDataInDest')"
                :editorClass="stagingRow.insert ? '' : 'mxs-color-helper all-border-error'"
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
                    small
                    height="36"
                    color="primary"
                    class="mr-2 font-weight-medium px-7 text-capitalize"
                    rounded
                    outlined
                    depressed
                    :disabled="!hasRowChanged"
                    @click="$emit('on-discard')"
                >
                    {{ $mxs_t('discard') }}
                </v-btn>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    :disabled="isInErrState || !hasStagingRowChanged"
                    @click="apply"
                >
                    {{ $mxs_t('apply') }}
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
 * Change Date: 2026-11-16
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
        hasRowChanged: { type: Boolean, required: true }, // Using for the discard btn
    },
    data() {
        return {
            stagingRow: { select: '', create: '', insert: '' },
        }
    },
    computed: {
        isInErrState() {
            const { select, create, insert } = this.stagingRow
            return Boolean(!select || !create || !insert)
        },
        // Using for the apply btn
        hasStagingRowChanged() {
            return !this.$helpers.lodash.isEqual(this.value, this.stagingRow)
        },
    },
    watch: {
        value: {
            deep: true,
            immediate: true,
            handler() {
                this.stagingRow = this.$helpers.lodash.cloneDeep(this.value)
            },
        },
    },
    methods: {
        apply() {
            this.$emit('input', this.stagingRow)
        },
    },
}
</script>
<style lang="scss" scoped>
.field__label {
    font-size: 0.875rem !important;
}
.script-container {
    position: absolute;
    top: 0;
    right: 0;
    width: 100%;
    .create-script-ctr {
        min-height: 150px;
        max-height: 400px;
    }
    .select-script-ctr,
    .insert-script-ctr {
        min-height: 100px;
        max-height: 200px;
    }
    .btn-ctr {
        height: 36px;
    }
}
</style>
