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
export default {
    name: 'query-result',
    data() {
        return {
            activeTab: null,
        }
    },
}
</script>

<style lang="scss" scoped>
.query-result-fontStyle {
    font-size: 14px;
}
</style>
