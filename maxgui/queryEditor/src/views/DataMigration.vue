<template>
    <page-wrapper>
        <portal to="page-header">
            <div class="d-flex align-center">
                <v-btn v-if="hasActiveEtlTask" class="ml-n9" icon @click="goBack">
                    <v-icon
                        class="mr-1"
                        style="transform:rotate(90deg)"
                        size="28"
                        color="deep-ocean"
                    >
                        $vuetify.icons.mxs_arrowDown
                    </v-icon>
                </v-btn>
                <h4
                    style="line-height: normal;"
                    class="mb-0 mxs-color-helper text-navigation text-h4 text-capitalize"
                >
                    {{ hasActiveEtlTask ? $mxs_t('dataMigrationWizard') : $mxs_t('dataMigration') }}
                </h4>
            </div>
        </portal>
        <v-sheet class="d-flex flex-column fill-height">
            <data-migration-stage-ctr v-if="hasActiveEtlTask" />
            <data-migration-list v-else />
        </v-sheet>
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
import DataMigrationList from '@queryEditorSrc/components/DataMigrationList.vue'
import DataMigrationStageCtr from '@queryEditorSrc/components/DataMigrationStageCtr.vue'

export default {
    name: 'data-migration',
    components: {
        DataMigrationList,
        DataMigrationStageCtr,
    },
    computed: {
        hasActiveEtlTask() {
            return !this.$typy(EtlTask.getters('getActiveEtlTaskWithRelation')).isEmptyObject
        },
    },
    methods: {
        goBack() {
            EtlTask.commit(state => (state.active_etl_task_id = null))
        },
    },
}
</script>
