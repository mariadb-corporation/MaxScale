<template>
    <div>
        Results
        <!-- TODO: Show data table here -->
    </div>
</template>

<script>
import { mapState } from 'vuex'

export default {
    name: 'result-view',
    computed: {
        ...mapState({
            query_result: state => state.query.query_result,
            loading_query_result: state => state.query.loading_query_result,
        }),
    },
    watch: {
        /* TODO: handle these data
        loading_query_result(v) {
            console.log('loading_query_result', v)
        },
        query_result: {
            deep: true,
            handler(v) {
                console.log('query_result', v)
            },
        }, */
    },
}
</script>

<style lang="scss" scoped>
.query-result-fontStyle {
    font-size: 14px;
}
</style>
