<template>
    <div>
        <v-tabs v-model="activeTab" height="24" class="tab-navigation-wrapper ">
            <v-tab color="primary" :href="`#${SQL_QUERY_MODES.QUERY_VIEW}`">
                <span>
                    Results
                </span>
            </v-tab>
            <v-tab color="primary" :href="`#${SQL_QUERY_MODES.PREVIEW_DATA}`">
                <span>
                    Data preview
                </span>
            </v-tab>
            <v-tabs-items v-model="activeTab" class="ml-3">
                <v-tab-item
                    :value="SQL_QUERY_MODES.QUERY_VIEW"
                    class="pt-2 query-result-fontStyle color text-small-text"
                >
                    <result-view />
                </v-tab-item>
                <v-tab-item
                    :value="SQL_QUERY_MODES.PREVIEW_DATA"
                    class="pt-2 query-result-fontStyle color text-small-text"
                >
                    <preview-data :previewDataSchemaId="previewDataSchemaId" />
                </v-tab-item>
            </v-tabs-items>
        </v-tabs>
    </div>
</template>

<script>
import { mapState } from 'vuex'
import PreviewData from './PreviewData'
import ResultView from './ResultView'
export default {
    name: 'query-result',
    components: {
        PreviewData,
        ResultView,
    },
    props: {
        previewDataSchemaId: { type: String, require: true },
    },
    data() {
        return {
            activeTab: '',
        }
    },
    computed: {
        ...mapState({
            query_result: state => state.query.query_result,
            loading_query_result: state => state.query.loading_query_result,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_sql_query_mode: state => state.query.curr_sql_query_mode,
        }),
    },
    watch: {
        curr_sql_query_mode(v) {
            if (v === this.SQL_QUERY_MODES.VIEW_DETAILS) {
                this.activeTab = this.SQL_QUERY_MODES.PREVIEW_DATA
            } else this.activeTab = v
        },
    },
}
</script>

<style lang="scss" scoped>
.query-result-fontStyle {
    font-size: 14px;
}
</style>
