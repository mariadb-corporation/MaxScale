<template>
    <page-wrapper class="fill-height">
        <template v-slot:page-header>
            <div class="d-flex flex-grow-1 align-center">
                <v-btn v-if="hasActiveEtlTask" class="ml-n4" icon @click="goBack">
                    <v-icon
                        class="mr-1"
                        style="transform:rotate(90deg)"
                        size="28"
                        color="deep-ocean"
                    >
                        $vuetify.icons.mxs_arrowDown
                    </v-icon>
                </v-btn>
                <template v-if="hasActiveEtlTask">
                    <v-text-field
                        v-if="isEditing"
                        v-model="etlTaskName"
                        :rules="[
                            v =>
                                !!v ||
                                $mxs_t('errors.requiredInput', { inputName: $mxs_t('name') }),
                        ]"
                        required
                        :height="51"
                        autofocus
                        class="vuetify-input--override etl-task-name-input text-h4"
                        dense
                        outlined
                        @blur="doneEditingName"
                        @keydown.enter="doneEditingName"
                    />
                    <span
                        v-else
                        class="mxs-color-helper text-navigation text-h4"
                        @click="isEditing = true"
                    >
                        {{ etlTaskName }}
                    </span>
                    <v-btn
                        v-if="!isEditing"
                        icon
                        class="text-capitalize ml-2"
                        @click="isEditing = !isEditing"
                    >
                        <v-icon size="22" color="primary">
                            $vuetify.icons.mxs_edit
                        </v-icon>
                    </v-btn>
                </template>
                <h4 v-else class="mb-0 mxs-color-helper text-navigation text-capitalize">
                    {{ $mxs_t('dataMigration') }}
                </h4>
            </div>
        </template>
        <etl-stages v-if="hasActiveEtlTask" class="fill-height ml-6" />
        <etl-tasks v-else class="fill-height" />
    </page-wrapper>
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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import EtlTasks from '@queryEditorSrc/components/EtlTasks.vue'
import EtlStages from '@queryEditorSrc/components/EtlStages.vue'

export default {
    name: 'data-migration',
    components: {
        EtlTasks,
        EtlStages,
    },
    data() {
        return {
            isEditing: false,
        }
    },
    computed: {
        activeEtlTaskWithRelation() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        hasActiveEtlTask() {
            return !this.$typy(this.activeEtlTaskWithRelation).isEmptyObject
        },
        etlTaskName: {
            get() {
                return this.activeEtlTaskWithRelation.name
            },
            set(v) {
                EtlTask.update({
                    where: this.activeEtlTaskWithRelation.id,
                    data: { name: v },
                })
            },
        },
    },
    methods: {
        goBack() {
            EtlTask.commit(state => (state.active_etl_task_id = null))
        },
        doneEditingName() {
            this.isEditing = false
        },
    },
}
</script>

<style lang="scss">
.etl-task-name-input {
    margin-left: -8px !important;
    .v-input__slot {
        margin-bottom: 0 !important;
        padding: 0 8px 0 8px !important;
        input {
            font-size: 30px !important;
            height: 51px;
            line-height: 51px;
            max-height: 51px !important;
            letter-spacing: inherit;
        }
    }
}
</style>
