<template>
    <div>
        <v-tabs v-model="activeTab" height="24" class="tab-navigation-wrapper ">
            <v-tab color="primary">
                <span>
                    Results
                </span>
            </v-tab>
            <v-tab color="primary">
                <span>
                    Data preview
                </span>
            </v-tab>
            <v-tabs-items v-model="activeTab" class="ml-3">
                <v-tab-item class="pt-2 query-result-fontStyle color text-small-text">
                    Results
                </v-tab-item>
                <v-tab-item class="pt-2 query-result-fontStyle color text-small-text">
                    <span class="schema-view-title "><b>View:</b> schema.table-names</span>
                </v-tab-item>
            </v-tabs-items>
        </v-tabs>
    </div>
</template>

<script>
import { mapState } from 'vuex'
export default {
    name: 'query-result',
    data() {
        return {
            activeTab: null,
        }
    },
    computed: {
        ...mapState({
            preview_data: state => state.query.preview_data,
            loading_preview_data: state => state.query.loading_preview_data,
            data_details: state => state.query.data_details,
            loading_data_details: state => state.query.loading_data_details,
        }),
    },
    watch: {
        //TODO: handle these data
        loading_preview_data: {
            deep: true,
            handler(v) {
                /* eslint-disable no-console */
                console.log('loading_preview_data', v)
            },
        },
        preview_data: {
            deep: true,
            handler(v) {
                /* eslint-disable no-console */
                console.log('preview_data', v)
            },
        },
        loading_data_details: {
            deep: true,
            handler(v) {
                /* eslint-disable no-console */
                console.log('loading_data_details', v)
            },
        },
        data_details: {
            deep: true,
            handler(v) {
                /* eslint-disable no-console */
                console.log('data_details', v)
            },
        },
    },
}
</script>

<style lang="scss" scoped>
.query-result-fontStyle {
    font-size: 14px;
}
</style>
