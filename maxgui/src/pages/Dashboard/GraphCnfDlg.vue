<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="$mxs_t('configuration')"
        :lazyValidation="false"
        :hasChanged="hasChanged"
        minBodyWidth="800px"
        bodyCtrClass="px-0 pb-4"
        formClass="px-10 py-0"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:form-body>
            <v-tabs
                v-model="activeGraphName"
                vertical
                class="v-tabs--mariadb v-tabs--mariadb--vert fill-height"
                hide-slider
                eager
            >
                <v-tab
                    v-for="(_, key) in dsh_graphs_cnf"
                    :key="key"
                    :href="`#${key}`"
                    class="justify-space-between align-center"
                >
                    <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
                        {{ key }}
                    </div>
                </v-tab>
                <v-tabs-items
                    v-if="!$typy(graphsCnf).isEmptyObject"
                    v-model="activeGraphName"
                    class="fill-height"
                >
                    <v-tab-item :value="activeGraphName" class="fill-height">
                        <div class="pl-4 pr-2 overflow-y-auto graph-cnf-ctr">
                            <div
                                v-for="(data, cnfType) in graphsCnf[activeGraphName]"
                                :key="cnfType"
                                class="fill-height overflow-hidden"
                            >
                                <annotations-cnf-ctr
                                    v-if="cnfType === 'annotations'"
                                    v-model="graphsCnf[activeGraphName][cnfType]"
                                    :cnfType="cnfType"
                                />
                            </div>
                        </div>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { mapMutations, mapState } from 'vuex'
import AnnotationsCnfCtr from '@src/pages/Dashboard/AnnotationsCnfCtr'

export default {
    name: 'graph-cnf-dlg',
    components: { AnnotationsCnfCtr },
    inheritAttrs: false,
    data() {
        return {
            activeGraphName: 'load',
            graphsCnf: {},
        }
    },
    computed: {
        ...mapState({
            dsh_graphs_cnf: state => state.persisted.dsh_graphs_cnf,
        }),
        isOpened: {
            get() {
                return this.$attrs.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.dsh_graphs_cnf, this.graphsCnf)
        },
    },
    watch: {
        isOpened: {
            immediate: true,
            handler(v) {
                if (v) this.graphsCnf = this.$helpers.lodash.cloneDeep(this.dsh_graphs_cnf)
                else this.graphsCnf = {}
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_DSH_GRAPHS_CNF: 'persisted/SET_DSH_GRAPHS_CNF',
        }),
        async onSave() {
            this.SET_DSH_GRAPHS_CNF(this.$helpers.lodash.cloneDeep(this.graphsCnf))
        },
    },
}
</script>
<style lang="scss" scoped>
.graph-cnf-ctr {
    min-height: 360px;
    max-height: 420px;
}
</style>
