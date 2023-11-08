<template>
    <page-wrapper>
        <v-sheet class="mt-3 d-flex flex-column">
            <page-header />
            <v-tabs
                v-model="activeObjType"
                vertical
                class="v-tabs--mariadb v-tabs--mariadb--vert mt-4"
                hide-slider
                eager
            >
                <v-tab
                    v-for="(step, type) in stepMap"
                    :key="type"
                    :href="`#${type}`"
                    class="my-1 justify-space-between align-center"
                >
                    <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
                        {{ step.label }}
                    </div>
                </v-tab>
                <v-tabs-items v-model="activeObjType" class="fill-height">
                    <v-tab-item :value="activeObjType" class="fill-height pl-9">
                        <!-- TODO: Add form ctr component -->
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { mapActions, mapState } from 'vuex'
import PageHeader from '@rootSrc/pages/ConfigWizard/PageHeader'

export default {
    components: { PageHeader },
    data() {
        return {
            activeObjType: '',
        }
    },
    computed: {
        ...mapState({
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
            all_modules_map: state => state.maxscale.all_modules_map,
        }),
        stepMap() {
            const { SERVICE, SERVER, MONITOR, LISTENER, FILTER } = this.MXS_OBJ_TYPES
            return {
                [SERVER]: { label: this.$mxs_tc('servers', 1) },
                [MONITOR]: { label: this.$mxs_tc('monitors', 1) },
                [FILTER]: { label: this.$mxs_tc('filters', 1) },
                [SERVICE]: { label: this.$mxs_tc('services', 1) },
                [LISTENER]: { label: this.$mxs_tc('listeners', 1) },
            }
        },
    },
    async created() {
        await this.init()
    },
    methods: {
        ...mapActions({ fetchAllModules: 'maxscale/fetchAllModules' }),
        async init() {
            this.activeObjType = this.MXS_OBJ_TYPES.SERVER
            if (this.$typy(this.all_modules_map).isEmptyObject) await this.fetchAllModules()
        },
    },
}
</script>
