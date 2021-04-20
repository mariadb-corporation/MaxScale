<template>
    <div>
        <div v-if="previewDataSchemaId" class="schema-view-title">
            <span><b>Table:</b> {{ previewDataSchemaId }}</span>
            <v-btn-toggle v-model="activeView" class="ml-4">
                <v-btn :value="SQL_QUERY_MODES.PREVIEW_DATA" x-small text color="primary">
                    Data
                </v-btn>
                <v-btn :value="SQL_QUERY_MODES.VIEW_DETAILS" x-small text color="primary">
                    Details
                </v-btn>
            </v-btn-toggle>
        </div>
        <span v-else>
            On the left sidebar, hover on the table name then click option icon (
            <v-icon size="12" color="deep-ocean">more_horiz</v-icon> ) and choose either
            <b>Preview Data</b> or
            <b>View Details</b>
            to generate data
        </span>

        <!-- TODO: Show data table here -->
    </div>
</template>

<script>
import { mapState, mapActions } from 'vuex'
export default {
    name: 'preview-data',
    props: {
        previewDataSchemaId: { type: String, require: true },
    },
    data() {
        return {
            activeView: '',
        }
    },
    computed: {
        ...mapState({
            preview_data: state => state.query.preview_data,
            loading_preview_data: state => state.query.loading_preview_data,
            data_details: state => state.query.data_details,
            loading_data_details: state => state.query.loading_data_details,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_sql_query_mode: state => state.query.curr_sql_query_mode,
        }),
    },
    watch: {
        curr_sql_query_mode(v) {
            this.activeView = v
        },
        activeView(v) {
            this.switchSQLMode(v)
        },
        /* TODO: handle these data
        loading_preview_data(v) {
            console.log('loading_preview_data', v)
        },
        preview_data: {
            deep: true,
            handler(v) {
                console.log('preview_data', v)
            },
        },
        loading_data_details(v) {
            console.log('loading_data_details', v)
        },
        data_details: {
            deep: true,
            handler(v) {
                console.log('data_details', v)
            },
        },
        */
    },
    created() {
        this.activeView = this.curr_sql_query_mode
    },
    methods: {
        ...mapActions({
            switchSQLMode: 'query/switchSQLMode',
        }),
    },
}
</script>
