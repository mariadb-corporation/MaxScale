<template>
    <v-tabs v-model="activeTab" class="tab-navigation-wrapper fill-height">
        <v-tab color="primary" :href="`#${SQL_QUERY_MODES.QUERY_VIEW}`">
            <span> Results </span>
        </v-tab>
        <v-tab color="primary" :href="`#${SQL_QUERY_MODES.PREVIEW_DATA}`">
            <span>
                Data preview
            </span>
        </v-tab>
        <v-tabs-items v-model="activeTab" class="tab-items">
            <v-tab-item :value="SQL_QUERY_MODES.QUERY_VIEW" :class="tabItemClass">
                <result-tab :dynHeight="dynTabItemHeight" />
            </v-tab-item>
            <v-tab-item :value="SQL_QUERY_MODES.PREVIEW_DATA" :class="tabItemClass">
                <preview-data-tab
                    :dynHeight="dynTabItemHeight"
                    :previewDataSchemaId="previewDataSchemaId"
                />
            </v-tab-item>
        </v-tabs-items>
    </v-tabs>
</template>

<script>
import { mapState, mapActions } from 'vuex'
import PreviewDataTab from './PreviewDataTab'
import ResultTab from './ResultTab'
export default {
    name: 'query-result',
    components: {
        PreviewDataTab,
        ResultTab,
    },
    props: {
        previewDataSchemaId: { type: String, require: true },
        dynHeight: { type: Number, require: true },
    },
    data() {
        return {
            activeTab: '',
            tabItemClass: 'pt-2 px-5 query-result-fontStyle color text-small-text fill-height',
        }
    },
    computed: {
        ...mapState({
            query_result: state => state.query.query_result,
            loading_query_result: state => state.query.loading_query_result,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_sql_query_mode: state => state.query.curr_sql_query_mode,
        }),
        dynTabItemHeight() {
            // dynHeight - $tab-bar-height - pt-2 - border thickness
            return this.dynHeight - 24 - 8 - 2
        },
    },
    watch: {
        activeTab(v) {
            if (v !== this.curr_sql_query_mode) this.switchSQLMode(v)
        },
        curr_sql_query_mode(v) {
            if (v === this.SQL_QUERY_MODES.VIEW_DETAILS) {
                this.activeTab = this.SQL_QUERY_MODES.PREVIEW_DATA
            } else this.activeTab = v
        },
    },
    methods: {
        ...mapActions({
            switchSQLMode: 'query/switchSQLMode',
        }),
    },
}
</script>

<style lang="scss" scoped>
.query-result-fontStyle {
    font-size: 14px;
}
$tab-bar-height: 24px;
::v-deep.tab-navigation-wrapper {
    .v-tabs-bar {
        height: $tab-bar-height;
    }
}
::v-deep.tab-items {
    height: calc(100% - #{$tab-bar-height});
    .v-window__container {
        height: 100%;
    }
}
</style>
