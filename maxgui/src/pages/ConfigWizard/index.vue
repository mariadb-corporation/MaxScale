<template>
    <page-wrapper class="fill-height">
        <page-header />
        <v-sheet class="mt-3 fill-height d-flex flex-column">
            <v-tabs
                v-model="activeIdxStage"
                vertical
                class="v-tabs--mariadb v-tabs--mariadb--vert mt-4"
                hide-slider
                eager
            >
                <v-tab
                    v-for="(stage, type, i) in stageDataMap"
                    :key="i"
                    class="my-1 justify-space-between align-center"
                >
                    <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
                        {{ stage.label }}
                    </div>
                </v-tab>
                <v-tabs-items v-model="activeIdxStage" class="fill-height">
                    <v-tab-item
                        v-for="(item, type, i) in stageDataMap"
                        :key="i"
                        class="fill-height"
                    >
                        <overview-stage v-if="activeIdxStage === 0" @next="activeIdxStage++" />
                        <v-container v-else-if="activeIdxStage === i" fluid class="fill-height">
                            <v-row class="fill-height">
                                <v-col cols="12" md="8" class="pl-0 py-0">
                                    <obj-stage
                                        :objType="type"
                                        :stageDataMap.sync="stageDataMap"
                                        @next="activeIdxStage++"
                                    />
                                </v-col>
                                <v-col cols="12" md="4">
                                    <!-- TODO: Add component to show created object -->
                                </v-col>
                            </v-row>
                        </v-container>
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
import OverviewStage from '@rootSrc/pages/ConfigWizard/OverviewStage'
import ObjStage from '@rootSrc/pages/ConfigWizard/ObjStage'

export default {
    components: { PageHeader, OverviewStage, ObjStage },
    data() {
        return {
            activeIdxStage: 0,
            stageDataMap: {},
        }
    },
    computed: {
        ...mapState({
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
            all_modules_map: state => state.maxscale.all_modules_map,
        }),
        overviewStage() {
            return { label: this.$mxs_t('overview'), component: 'overview-stage' }
        },
    },
    async created() {
        await this.init()
    },
    methods: {
        ...mapActions({ fetchAllModules: 'maxscale/fetchAllModules' }),
        initStageMapData() {
            this.stageDataMap = Object.values(this.MXS_OBJ_TYPES).reduce(
                (map, type) => {
                    map[type] = {
                        label: this.$mxs_tc(type, 1),
                        component: 'form-ctr',
                        newObjs: [], // objects that have been recently created using the wizard
                        existingObjs: [], // existing object data received from API
                    }
                    return map
                },
                { [this.overviewStage.label]: this.overviewStage }
            )
        },
        async init() {
            this.initStageMapData()
            if (this.$typy(this.all_modules_map).isEmptyObject) await this.fetchAllModules()
        },
    },
}
</script>
